#include <signal.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <errno.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <ctype.h>
#include <time.h>
#include <queue>
#include "ThreadPool.hpp"

#define MAX_EVENTS 1024     //监听上限数
#define BUFLEN 4096
#define SERV_PORT 1145



class readctor{
private:

typedef struct event{
    int fd;         //待监听的文件描述符
    int events;     //对应的监听事件
    void*arg;       //泛型参数
    void (readctor::*call_back)(int fd, int events, void * arg); //回调函数
    int status;     //是否在红黑树上，1->在，0->不在
    char buf[BUFLEN];
    int len;
    long last_active;   //记录最后加入红黑树的时间值
}event;

    struct EventContext {
        event* ev;
        readctor* obj;
    };
    
    int epfd;   //红黑树的句柄
    event r_events[MAX_EVENTS + 1];
    pthread_pool pthpool;
    pthread_mutex_t event_mutex; // ac锁
    std::queue<EventContext*> evq;

    //删除树上节点
    void eventdel(event * ev);
    
    //监听回调
    void acceptconn(int lfd,int tmp, void * arg);

    //写回调
    void senddata(int fd,int tmp, void * arg);
    
    //读回调
    void recvdata(int fd, int events, void*arg);

    //初始化事件
    void eventset(event * ev, int fd, void (readctor::* call_back)(int ,int , void *), void * arg);

    //添加文件描述符到红黑树
    void eventadd(int events, event * ev);

    //初始化监听socket
    void InitListenSocket(unsigned short port);

    void readctorinit(unsigned short port);

    //线程池执行的需要执行任务
    static void event_callback_wrapper(struct EventContext * arg) {
        struct EventContext* ctx = arg;
        (ctx->obj->*(ctx->ev->call_back))(ctx->ev->fd, ctx->ev->events, ctx->ev->arg);
    }

public:
    // 无参构造函数
    readctor();  
    // 带参构造函数
    readctor(unsigned short port);

    ~readctor();
};

void readctor::eventdel(event * ev){
    struct epoll_event epv = {0,{0}};
    if(ev -> status != 1)  //不在红黑树上
        return;
    
    epv.data.ptr = NULL;
    ev -> status = 0;
    epoll_ctl(epfd, EPOLL_CTL_DEL, ev->fd, &epv);
    return ;
}

//监听回调
void readctor::acceptconn(int lfd,int tmp, void * arg){
    struct sockaddr_in caddr;
    socklen_t len = sizeof caddr;
    int cfd, i;
    pthread_mutex_lock(&event_mutex); // 加锁
    if((cfd = accept(lfd, (struct sockaddr *)&caddr,&len)) == -1){
        if(errno != EAGAIN && errno != EINTR){
            //暂时不做出错处理
        }
        printf("accept, %s",strerror(errno));
        return;
    }
    do{
        for(i = 0; i < MAX_EVENTS ; i ++)       //从r_events中找到一个空闲位置
            if(r_events[i].status == 0)
                break;

        if(i == MAX_EVENTS){
            printf("max connect limit[%d]\n",MAX_EVENTS);
            break;
        }

        int flag = 0;
        if((flag = fcntl(cfd, F_SETFL, O_NONBLOCK)) < 0){       //将cfd也设置为非阻塞
            printf("fcntl nonblocking failed, %s\n",strerror(errno));
            break;
        }

        eventset(&r_events[i], cfd, &readctor::recvdata, &r_events[i]);
        eventadd(EPOLLIN, &r_events[i]);
    }while(0);

    pthread_mutex_unlock(&event_mutex); // 解锁

    printf("new connect [%s:%d][time:%ld], pos[%d]\n",
    inet_ntoa(caddr.sin_addr),ntohs(caddr.sin_port), r_events[i].last_active, i);
    return;
}

bool send_all(int sockfd,const void * buf,size_t len){
  const char*p = static_cast<const char*>(buf);
  while(len > 0){
    int n = send(sockfd,p,len,0);
    if(n <= 0) return false;
    p += n;
    len -= n;
  }
  return true;
}

bool recv_all(int sockfd,void * buf,size_t len){
  char* p = static_cast<char*>(buf);
  int n;
  while(len > 0){
    do {
        n = recv(sockfd,p,len,0);
        if (n > 0) { p += n; len -= n; }
        else if (n == 0) break; // 对端关闭
        else if (errno != EAGAIN && errno != EWOULDBLOCK) return false; 
    }
    while(n > 0);
    //if(!(errno == EAGAIN || errno == EWOULDBLOCK)) return false; 
  }
  return true;
}

int sendMsg(std::string msg,int sockfd_) {
  uint32_t len = htonl(msg.size());
  if(!send_all(sockfd_,&len,sizeof len)) return -1;
  if(!send_all(sockfd_,msg.data(),msg.size())) return -1;
  return 0;
}

int recvMsg(std::string& msg,int sockfd_) {
  uint32_t len, slen;
  if(!recv_all(sockfd_,&len,sizeof len)) return -1;
  slen = ntohl(len);
  msg.clear();
  msg.resize(slen);
  if(!recv_all(sockfd_,msg.data(),slen)) return -1;
  return 0;
}

//写回调
void readctor::senddata(int fd,int tmp, void * arg){
    event * ev = (event*)arg;
    int len;
    std::string str = ev->buf;
    int ret = sendMsg(str, fd);

    pthread_mutex_lock(&event_mutex); // 加锁
    if(ret == -1){
        close(ev->fd);
        printf("send[fd = %d] , len = %ld, error %s\n",fd,str.size(),strerror(errno));
    }
    eventdel(ev);
    
    printf("send[fd = %d],[%ld]%s\n",fd,str.size(),ev->buf);

    eventset(ev,fd,&readctor::recvdata,ev);
    eventadd(EPOLLIN, ev);   

    pthread_mutex_unlock(&event_mutex); // 解锁

}


//读回调
void readctor::recvdata(int fd, int events, void*arg){
    event *ev = (event *) arg;
    int len;
    std::string str;
    int ret = recvMsg(str, fd);

    pthread_mutex_lock(&event_mutex); // 加锁
    if(ret == -1){
        close(ev->fd);
        printf("recv[fd = %d] error[%d]:%s\n",fd,errno,strerror(errno));
        return;
    }
    strcpy(ev->buf,str.c_str());
    len = str.size();

    eventdel(ev);//将该节点从红黑树摘除

    if(len > 0){
        ev->len = len;
        ev->buf[len] ='\0';
        printf("C[%d]:%s",fd,ev->buf);

        eventset(ev,fd,&readctor::senddata,ev);    //设置该fd对应的回调函数为senddata
        eventadd(EPOLLOUT, ev);         //将fd加入红黑树中，监听其写事件

    } else if(len == 0){//对端已关闭
        close(ev->fd);
        printf("[fd = %d] pos[%ld], closed\n", fd, ev-r_events);
    }else{
        close(ev->fd);
        printf("recv[fd = %d] error[%d]:%s\n",fd,errno,strerror(errno));
    }

    pthread_mutex_unlock(&event_mutex); // 解锁
}

//初始化事件
void readctor::eventset(event * ev, int fd, void (readctor::* call_back)(int ,int , void *), void * arg){
    ev -> fd = fd;
    ev -> call_back = call_back;
    ev -> arg = arg;

    ev -> events = 0;
    ev -> status = 0; 
    ev -> last_active = time(NULL);     //调用eventset函数的时间
    return;
}

//添加文件描述符到红黑树
void readctor::eventadd(int events, event * ev){
    //事件处理采用ET模式
    int combined_events = events | EPOLLET;
    //events |= EPOLLET;
    struct epoll_event epv = { 0 , { 0 }};
    int op = EPOLL_CTL_MOD;
    epv.data.ptr = ev;
    epv.events = ev -> events = combined_events;

    if(ev -> status == 0){      //若ev不在树内
        op = EPOLL_CTL_ADD;
        ev -> status = 1;
    }
    else{
        if(epoll_ctl(epfd,op,ev -> fd, &epv) < 0)
            printf("epoll_ctl  mod is error :[fd = %d], events[%d]\n", ev->fd, combined_events);
        else
            printf("epoll_ctl mod sccess on [fd = %d], [op = %d] events[%0X]\n",ev->fd, op, combined_events);
    }
    if(epoll_ctl(epfd, op, ev -> fd, &epv) < 0)
        printf("epoll_ctl is error :[fd = %d], events[%d]\n", ev->fd, combined_events);
    else
        printf("epoll_ctl sccess on [fd = %d], [op = %d] events[%0X]\n",ev->fd, op, combined_events);
}

//初始化监听socket
void readctor::InitListenSocket(unsigned short port){
    struct sockaddr_in addr;
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(lfd, F_SETFL, O_NONBLOCK);

    memset(&addr, 0, sizeof addr);
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    bind(lfd,(sockaddr *)&addr, sizeof addr);

    listen(lfd, 20);

    eventset(&r_events[MAX_EVENTS], lfd, &readctor::acceptconn, &r_events[MAX_EVENTS]);
    eventadd(EPOLLIN, &r_events[MAX_EVENTS]);
}



void readctor::readctorinit(unsigned short port){
    pthread_mutex_init(&event_mutex,NULL);
    epfd = epoll_create(MAX_EVENTS + 1);            //定义最大节点数为MAX_EVENTS + 1的红黑树
    if(epfd <= 0)
        printf("epfd create is error, epfd : %d\n", epfd);
    InitListenSocket(port);                         //初始化套接字

    struct epoll_event events[MAX_EVENTS + 1];      //保存已经满足就绪事件的文件描述符
    printf("server running port:[%d]\n", port);
    int chekckpos = 0, i;

    while(1){
        //↓↓↓超时验证
        long now = time(NULL);
        for(i = 0; i < 100; i++, chekckpos++){       //一次循环检验100个，chekckpos控制检验对象
            if(chekckpos == MAX_EVENTS)
                chekckpos = 0;
            if(r_events[chekckpos].status != 1)      //不在红黑树上，继续循环
                continue;
            
            long duration = now -r_events[chekckpos].last_active;   //计算客户端不活跃的时间
            if(duration >= 60){
                printf("[fd = %d] timeout\n", r_events[chekckpos].fd);
                pthread_mutex_lock(&event_mutex); // 加锁
                eventdel(&r_events[chekckpos]);
                close(r_events[chekckpos].fd);
                pthread_mutex_unlock(&event_mutex); // 加锁
            }
        }
        //↑↑↑超时验证
        //监听红黑树epfd，将满足的事件的文件描述符加至events数组中，1秒没有文件满足，则返回0
        int nfd = epoll_wait(epfd, events, MAX_EVENTS + 1, 1000); 
        if(nfd < 0){
            printf("epoll_wait error\n");
            break;
        }

        for(i = 0; i < nfd; i++){
            event *ev = (event *) events[i].data.ptr;
            //读事件，调用读回调
            if((events[i].events & EPOLLIN) && (ev -> events & EPOLLIN)){
                //struct EventContext ctx = {ev,this};
                struct EventContext* ctx = (struct EventContext*)malloc(sizeof (struct EventContext));
                ctx->ev = ev;
                ctx->obj = this;
                evq.push(ctx);
                pthpool.PushTask(event_callback_wrapper, ctx);
            }
            //写事件，调用写回调
            if((events[i].events & EPOLLOUT) && (ev -> events & EPOLLOUT)){
                /*auto ctx = new EventContext{ev, this};
                pthpool.PushTask(event_callback_wrapper, ctx);*/
                struct EventContext* ctx = (struct EventContext*)malloc(sizeof (struct EventContext));
                ctx->ev = ev;
                ctx->obj = this;
                evq.push(ctx);
                pthpool.PushTask(event_callback_wrapper, ctx);
            }
        }
    }
}

// 无参构造函数
readctor::readctor(){
    unsigned short port = SERV_PORT;
    readctorinit(port);
}          
// 带参构造函数
readctor::readctor(unsigned short port){
    readctorinit(port);
}   

readctor::~readctor() {
    pthread_mutex_destroy(&event_mutex);
    // 1. 停止线程池并等待所有任务完成
    pthpool.~pthread_pool(); // 调用线程池析构函数（若线程池未自动管理，可显式停止）
    
    // 2. 关闭 epoll 句柄
    if (epfd > 0) {
        close(epfd);
        epfd = -1;
    }

    // 3. 关闭所有客户端套接字（遍历 r_events）
    for (int i = 0; i < MAX_EVENTS + 1; i++) {
        event& ev = r_events[i];
        if (ev.status == 1 && ev.fd > 0) { // 若在红黑树上且fd有效
            eventdel(&ev); // 从epoll中删除
            close(ev.fd); // 关闭套接字
            ev.fd = -1;
            ev.status = 0;
        }
    }

    // 4. 清理监听套接字（位于 r_events[MAX_EVENTS]）
    event& listen_ev = r_events[MAX_EVENTS];
    if (listen_ev.fd > 0) {
        eventdel(&listen_ev);
        close(listen_ev.fd);
        listen_ev.fd = -1;
    }

    //释放队列
    while(!evq.empty()){
        free(evq.front());
        evq.pop();
    }
}
