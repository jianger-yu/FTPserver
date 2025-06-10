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
#include <pwd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "ThreadPool.hpp"
#include "server/server.h"

#define MAX_EVENTS 1024     //监听上限数
#define BUFLEN 4096
#define SERV_PORT 1145
#define MAX_PORT 65535      //端口上限
#define DATASENDIP "127.0.0.1"
#define FTPFILEROAD "/home/jianger/codes/FTPserver/server/FTPfile"
#define max_road 4096

struct my_dr{
    char name[256];
    long unsigned int ino;
};

class readctor{
private:

typedef struct event{
    int fd;         //待监听的文件描述符
    int events;     //对应的监听事件
    void*arg;       //泛型参数
    void (readctor::*call_back)(int fd, int events, void * arg); //回调函数
    int status;     //是否在红黑树上，1->在，0->不在
    //读入的信息及长度
    char buf[BUFLEN];
    int len;
    //用于监听的文件描述符
    int lisfd;
    int lisdatafd;
    //用于给客户端传输数据的文件描述符
    int datafd;
    //用于通信的地址结构
    struct sockaddr_in skaddr;
    socklen_t skaddr_len;
    //用于控制顺序的锁和条件变量及控制原子
    pthread_mutex_t pthlock;
    pthread_cond_t pthcond;

    //用于给数据传输线程通知有新任务的锁
    pthread_mutex_t datalock;
    pthread_cond_t datacond;

    pthread_mutex_t splock;
    pthread_cond_t spcond;


    //true--线程已准备好， false--线程未准备好
    bool pthready;
    bool dataready;
    bool spready;
    bool lockinit;
    long last_active;   //记录最后加入红黑树的时间值

    bool poolrs;//true---已经加入线程池
    bool poolda;//true---已经开启数据传输线程
}event;

    struct EventContext {
        event* ev;
        readctor* obj;
    };
    
    int epfd;   //红黑树的句柄
    event r_events[MAX_EVENTS + 1];
    pthread_pool pthpool;
    pthread_mutex_t event_mutex; // 事件锁，用于修改红黑树的公共区域
    std::queue<EventContext*> evq;

    //删除树上节点
    void eventdel(event * ev);
    
    //监听回调
    void acceptconn(int lfd,int tmp, void * arg);

    //获取一个端口与数据传输线程
    unsigned short getport(event * ev);
    static void data_pth(readctor::event* ev, unsigned short port, readctor* th);
    
    //获取需发送的字符串
    void getsendstr(event* ev,unsigned short dataport, std::string &str);

    void PASV(event * ev);
    void LIST(event * ev);
    void STOR(event * ev);
    void RETR(event * ev);
    //处理回调
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
    event* ev = (event*) arg;
    struct sockaddr_in caddr;
    socklen_t len = sizeof caddr;
    int cfd, i;
    
    ev->poolrs = false;

    printf("监听回调 准备抢 event_mutex\n");
    pthread_mutex_lock(&event_mutex); // 加锁
    printf("监听回调 抢到 event_mutex\n");

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
    printf("监听回调 解开 event_mutex\n");


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
        else if (n == 0) {
            len = 0;
            break; // 对端关闭
        }
        else if (errno != EAGAIN && errno != EWOULDBLOCK) return false;
        if(len == 0) break;
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

unsigned short readctor::getport(event* ev){
    unsigned short i;
    for(i = 1025; i <= 65535; i++){
        if(i <= 10) i = 1025;
        ev -> lisfd = socket(AF_INET, SOCK_STREAM, 0);

        ev->skaddr.sin_family = AF_INET;
        ev->skaddr.sin_port = htons(i);
        ev->skaddr.sin_addr.s_addr = inet_addr(DATASENDIP);
        ev->skaddr_len = sizeof ev->skaddr;

        int ret = bind(ev->lisfd,(struct sockaddr*)&ev->skaddr, sizeof ev->skaddr);
        if(ret == -1) {
            printf("bind: port:%hu  ip:%s  error:%s\n",i,DATASENDIP,strerror(errno));
            //sleep(3);
            continue;
        }
        ret = listen(ev->lisfd, 128);
        if(ret == -1) {
            printf("listen error: %s\n",strerror(errno));
            close(ev->lisfd);
            continue;
        }
        else break;
    }
  return i;
}


void readctor::getsendstr(event* ev,unsigned short dataport, std::string &str){
    str.clear();
    str = "227 entering passive mode (";
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(ev->skaddr.sin_addr), ip_str, INET_ADDRSTRLEN);
    std::string ipstr = ip_str;
    for(int i = 0; i < ipstr.size(); i++){
        if(ipstr[i] == '.') str.push_back(',');
        else str.push_back(ipstr[i]);
    }
    int p1 = dataport/256, p2 = dataport%256;
    char portstr[30];
    sprintf(portstr,",%d,%d)",p1,p2);
    ipstr.clear();
    ipstr = portstr;
    str += ipstr;
}

void readctor::PASV(event * ev){
    printf("new PASV command\n");
    //①用户请求被动模式，应开一个数据传输线程
    //②将生成的端口号告知客户端控制线程，返回 227 entering passive mode (h1,h2,h3,h4,p1,p2)
    //  其中端口号为 p1*256+p2，IP 地址为 h1.h2.h3.h4。
    
    //获取端口号
    unsigned short dataport = getport(ev);
    printf("getport return port is : %d\n",dataport);
    printf("senddata  PASV 准备抢 pthlock\n");
    pthread_mutex_lock(&ev->pthlock);
    printf("senddata  PASV 抢到 pthlock\n");

    ev->pthready = false;
    //开数据传输线程，监听该端口
    pthpool.PushTask(readctor::data_pth, ev, dataport,this);
    //阻塞等待数据传输线程开始监听
    while(!ev->pthready){
        pthread_cond_wait(&ev->pthcond, &ev->pthlock);
    }
    pthread_mutex_unlock(&ev->pthlock);
    printf("senddata  PASV 解开 pthlock\n");
    //获取需发送的字符串
    std::string str;
    getsendstr(ev, dataport, str);
    printf("getsendstr return string is :%s\n",str.c_str());
    //对客户端发送字符串
    sendMsg(str, ev->fd);
}

bool cmp(char *a,char*b)//返回a时间是否新于b时间
{
    struct stat sta,stb;
    if(lstat(a,&sta)==-1){//判断错误返回值
        perror("sta");
        exit(1);
    }
    if(lstat(b,&stb)==-1){//判断错误返回值
        perror("stb");
        exit(1);
    }
    time_t mta = sta.st_mtime;
    struct tm * ta = localtime(&mta);//得到a的时间信息
    int yea=ta->tm_year,mo=ta->tm_mon,da=ta->tm_mday,hou=ta->tm_hour,mi=ta->tm_min;//存储a的时间信息
    time_t mtb = stb.st_mtime;
    struct tm * tb = localtime(&mtb);//得到b的时间信息
    //通过a存储的时间信息和b的时间信息比较
    if(yea!=tb->tm_year) return yea  >  tb->tm_year;
    if(mo!=tb->tm_mon)   return mo   >  tb->tm_mon;
    if(da!=tb->tm_mday)  return da   >  tb->tm_mday;
    if(hou!=tb->tm_hour) return hou  >  tb->tm_hour;
    if(mi!=tb->tm_min)   return mi   >  tb->tm_min;
    return false;
}
//使用快速排序对其排序
void msort(struct my_dr *arr,int l,int r)
{
    if(l>=r) return ;
    int i = l - 1, j = r + 1, x = (l + r) >> 1;
    char road[max_road],roadx[max_road];
    sprintf(roadx,"%s/%s",FTPFILEROAD,arr[x].name);
    while(i<j)
    {
        do {
            i++;
            sprintf(road,"%s/%s",FTPFILEROAD,arr[i].name);//构建绝对路径
        } 
        while(cmp(road,roadx));
        do {
            j--;
            sprintf(road,"%s/%s",FTPFILEROAD,arr[j].name);//构建绝对路径
        } 
        while(cmp(roadx,road));
        if(i<j) //swap(arr[i],arr[j])
        {
            struct my_dr tmp= arr[i];
            arr[i]=arr[j];
            arr[j]=tmp;
        }
    }
    msort(arr, l, j);
    msort(arr, j + 1, r);
}

void readctor::LIST(event * ev){
    DIR* dp;
    struct dirent* di;
    struct my_dr *arr=(struct my_dr *)malloc(sizeof(struct my_dr )*20);//开辟动态数组
    if(arr == NULL){//处理错误返回值
        perror("malloc");
        exit(1);
    }
    int cnt = 0,size = 20;
    //打开目录
    dp = opendir(FTPFILEROAD);
    if(dp == NULL){//处理错误返回值
        perror("opendir");
        exit(1);
    }
    //读目录
    while((di = readdir(dp)) != NULL){
        if(cnt>=size){//扩容
            size*=2;
            arr=(struct my_dr *)realloc(arr,sizeof(struct my_dr )*size);
        }
        strcpy(arr[cnt].name,di->d_name);//数组存储
        arr[cnt++].ino=di->d_ino;
    }
    //给客户端发文件数量
    std::string str;
    char buf[4096];
    sprintf(buf,"%d",cnt);
    msort(arr,0,cnt-1);   //按时间排序
    str = buf;
    sendMsg(str,ev->datafd);
    str.clear();
    for(int i = 0; i < cnt; i++){
        if(strcmp(arr[i].name,".") == 0 || strcmp(arr[i].name,"..") == 0) continue;
        //对于每个文件，取出文件名、文件大小、上传时间
        struct stat st;
        char road[max_road];
        sprintf(road,"%s/%s",FTPFILEROAD,arr[i].name);//构建绝对路径
        if(lstat(road,&st)==-1){//获取文件状态
            perror("stat");//处理错误返回值
            return;
        }
        //准备需输出的信息
        memset(road, 0, sizeof road);
        time_t mtime = st.st_mtime;
        struct tm * stm = localtime(&mtime);
        sprintf(road,"%-12s%15ldb %24d月 %2d日 %02d:%02d\n",arr[i].name,st.st_size,stm->tm_mon+1,stm->tm_mday,stm->tm_hour,stm->tm_min);
        printf("%-12s%15ldb %24d月 %2d日 %02d:%02d\n",arr[i].name,st.st_size,stm->tm_mon+1,stm->tm_mday,stm->tm_hour,stm->tm_min);
        //发送给客户端
        str = road;
        sendMsg(str,ev->datafd);
        str.clear();
    }
}

void readctor::RETR(event * ev){
    printf("RETR run! ev->datafd = %d\n", ev->datafd);

    std::string str;
    printf("准备从 datafd[%d] 接收文件名...\n", ev->datafd);
    int ret = recvMsg(str, ev->datafd);
    if(ret == -1){//失败处理
        printf("RETR error :%s\n",strerror(errno));
        exit(1);
    }
    printf("读到内容：%s\n",str.c_str());
    DIR* dp;
    struct dirent* di;
    struct my_dr *arr=(struct my_dr *)malloc(sizeof(struct my_dr )*20);//开辟动态数组
    if(arr == NULL){//处理错误返回值
        perror("malloc");
        exit(1);
    }
    int cnt = 0,size = 20;
    //打开目录
    dp = opendir(FTPFILEROAD);
    if(dp == NULL){//处理错误返回值
        perror("opendir");
        exit(1);
    }
    //读目录
    while((di = readdir(dp)) != NULL){
        if(cnt>=size){//扩容
            size*=2;
            arr=(struct my_dr *)realloc(arr,sizeof(struct my_dr )*size);
        }
        strcpy(arr[cnt].name,di->d_name);//数组存储
        arr[cnt++].ino=di->d_ino;
    }
    //在arr中寻找与用户输入一致的文件名
    bool key = false; //true表示成功找到
    int i;
    for(i = 0; i < cnt; i++){
        if(strcmp(arr[i].name, str.c_str()) == 0){
            key = true;
            printf("找到对应文件\n");
            break;  
        }
    }
    if(key == false){
        sendMsg("no search file.", ev->datafd);
        printf("no search file.\n");
        free(arr);
        closedir(dp);
        return;
    }
    //获取文件状态
    struct stat st;
    char road[max_road];
    sprintf(road,"%s/%s",FTPFILEROAD,arr[i].name);//构建绝对路径
    if(lstat(road,&st)==-1){//获取文件状态
        perror("stat");//处理错误返回值
        return;
    }
    //判断需要分几次传输
    char buf[4096];//一次发送4k
    int tmp = st.st_size / sizeof buf;
    //if(st.st_size % 4 != 0) tmp++;
    printf("文件大小:%ld  发送文件块数tmp == %d, endrt == %ld\n",st.st_size,tmp,(st.st_size + 4096)%4096);
    //打开文件，依次输出
    FILE* file = fopen(road, "rb");
    if (file == NULL) {
        perror("fopen");
        sendMsg("450 Requested file action not taken.", ev->datafd);
        free(arr);
        closedir(dp);
        return;
    }
    //发送需要传输的次数
    str.clear();
    char tmps[30];
    sprintf(tmps,"%dand%ld",tmp,(st.st_size + 4096)%4096);
    str = tmps;
    sendMsg(str, ev->datafd);

    size_t bytesRead;
    while ((bytesRead = fread(buf, 1, sizeof(buf), file)) > 0) {
        str.clear();
        str.assign(buf, bytesRead); 
        sendMsg(str, ev->datafd);
        //memset(buf, 0, sizeof buf);
    }
    /*while(1){
        str.clear();
        printf("准备读取回应\n");
        int ret = recvMsg(str, ev->datafd);
        printf("收到回应：%s\n",str.c_str());
        if(strcmp(str.c_str(), "success") == 0) break;
    }*/

    //释放资源
    fclose(file);
    free(arr);
    closedir(dp);
}



void readctor::STOR(event * ev){
    char fname[1000];
    char path[1024];
    std::string str;
    //读文件名
    int ret = recvMsg(str, ev->datafd);
    strcpy(fname, str.data());
    printf("读到文件名:%s\n", fname);
    if(ret == -1){//失败处理
        printf("RETR error :%s\n",strerror(errno));
        exit(1);
    }

    //读需要分几次传输
    int tmp;
    long endrt;
    str.clear();
    ret = recvMsg(str, ev->datafd);
    sscanf(str.c_str(),"%dand%ld",&tmp, &endrt);
    printf("tmp == %d, endrt == %ld\n", tmp, endrt);
    sprintf(path, "FTPfile/%s", fname);
    FILE * file = fopen(path, "wb");

    //开始传输文件
    for(int j = 0; j <= tmp; j++){
        if(j == tmp && endrt == 0) j++;
        str.clear();
        recvMsg(str, ev->datafd);
        if(j < tmp)
            fwrite(str.data(), 1, 4096, file);
        else
            fwrite(str.data(), 1, endrt, file);
    }
    fflush(file);
    printf("上传成功\n");

}

void readctor::data_pth(readctor::event * ev,unsigned short port, readctor* th){
    int tmp = 1;
    printf("datapth run start\n");

    printf("data_pth 准备抢 pthlock\n");
    pthread_mutex_lock(&ev->pthlock);
    printf("data_pth 抢到 pthlock\n");

    ev->pthready = true;
    pthread_cond_signal(&ev->pthcond);
    pthread_mutex_unlock(&ev->pthlock); // 解锁
    printf("data_pth 解开 pthlock\n");
    
    printf("datafd[%d]: 阻塞等待客户端连接(port:%d)\n",ev->lisfd, port);

    ev->datafd = accept(ev->lisfd,NULL, NULL);
    if(ev->datafd == -1){
        printf("accept [%d] error:%s\n",ev->lisfd, strerror(errno));
        return;
    }
    else printf("已成功连接!\n");
    
    ev->dataready = false;
    while(1){

        //上三把锁
        printf("data_pth 准备抢 pthlock\n");
        pthread_mutex_lock(&ev->pthlock);

        pthread_mutex_lock(&ev->splock);

        printf("data_pth 准备抢 datalock\n");
        pthread_mutex_lock(&ev->datalock);
        printf("data_pth 抢到 datalock (ev->dataready:%d)\n",ev->dataready);

        while(!ev->dataready){
            pthread_cond_wait(&ev->datacond, &ev->datalock);
        }
        //出循环，说明已经有任务
        if(strcmp(ev->buf,"EXIT")  == 0){
            tmp = 1;
            printf("EXIT run\n");
            close(ev->datafd);
            ev->dataready = false;

            //解数据锁
            pthread_mutex_unlock(&ev->datalock);
            //已经运行完，通知处理回调函数
            pthread_mutex_unlock(&ev->pthlock); // 解锁
            printf("data_pth EXIT 解开所有锁\n");
        }
        else if(strcmp(ev->buf,"LIST")  == 0){
            tmp = 0;
            printf("-------------------th:%p",th);
            th->LIST(ev);
            //已经运行完，通知处理回调函数
            pthread_mutex_unlock(&ev->pthlock); // 解锁
            //解数据锁
            pthread_mutex_unlock(&ev->datalock);
            printf("data_pth LIST 解开所有锁\n");
        }
        else if(strcmp(ev->buf,"RETR")  == 0){
            th->RETR(ev);
            tmp = 0;
            //解数据锁
            pthread_mutex_unlock(&ev->datalock);
            //已经运行完，通知处理回调函数
            pthread_mutex_unlock(&ev->pthlock); // 解锁
            printf("data_pth RETR 解开所有锁\n");
        }
        else if(strcmp(ev->buf,"STOR") == 0){
            th->STOR(ev);
            tmp = 0;
            //解数据锁
            pthread_mutex_unlock(&ev->datalock);
            //已经运行完，通知处理回调函数
            pthread_mutex_unlock(&ev->pthlock); // 解锁
            printf("data_pth STOR 解开所有锁\n");
        }
        while(!ev->spready){
            pthread_cond_wait(&ev->spcond, &ev->splock);
        }
        ev->spready = false;
        pthread_mutex_unlock(&ev->splock);
        printf("data_pth LIST 解开splock\n");
        if(tmp) return;
        ev->dataready = false;
    }
}


//处理回调
void readctor::senddata(int fd,int tmp, void * arg){
    event * ev = (event*)arg;
    printf("处理回调被执行,ev->buf:%s\n",ev->buf);
    if(strcmp(ev->buf,"PASV") == 0){
        PASV(ev);
    }
    else{
        //给数据传输线程发信号，有新事件需要处理
        printf("senddata 准备抢 datalock\n");
        pthread_mutex_lock(&ev->datalock);
        printf("senddata 抢到 datalock\n");

        ev->dataready = true;
        pthread_cond_signal(&ev->datacond);
        pthread_mutex_unlock(&ev->datalock); // 解锁
        printf("senddata 解开 datalock\n");

        printf("senddata 准备抢 pthlock\n");
        pthread_mutex_lock(&ev->pthlock);//抢线程预备锁，确保数据传输线程运行完
        printf("senddata 抢到 pthlock\n");//发信号通知抢到了
        pthread_mutex_lock(&ev->splock);
        ev->spready = true;
        pthread_cond_signal(&ev->spcond);
        pthread_mutex_unlock(&ev->splock);


        ev->pthready = false;
        pthread_mutex_unlock(&ev->pthlock);//解锁
        printf("senddata 解开 pthlock\n");

    }
    printf("senddata 准备抢 event_mutex\n");
    pthread_mutex_lock(&event_mutex); // 修改红黑树公共区域，加事件锁
    printf("senddata 抢到 event_mutex\n");
   
    eventdel(ev);
    
    memset(ev->buf, 0, sizeof ev->buf);
    eventset(ev,fd,&readctor::recvdata,ev);
    eventadd(EPOLLIN, ev);   

    pthread_mutex_unlock(&event_mutex); // 解锁
    printf("senddata 解除 event_mutex\n");

}


//读回调
void readctor::recvdata(int fd, int events, void*arg){
    event *ev = (event *) arg;
    int len;
    std::string str;
    printf("recvdata 准备 recvMsg\n");
    int ret = recvMsg(str, fd);

    printf("recvdata 准备抢 event_mutex\n");
    pthread_mutex_lock(&event_mutex); // 加锁
    printf("recvdata 抢到 event_mutex\n");

    if(ret == -1){//失败处理
        close(ev->fd);
        printf("recvMsg[fd = %d] error[%d]:%s\n",fd,errno,strerror(errno));
        pthread_mutex_unlock(&event_mutex); // 解锁
        return;
    }
    memset(ev->buf, 0, sizeof ev->buf);
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
        printf("recv[fd = %d] str.size() == [%d] error[%d]:%s\n",fd,len,errno,strerror(errno));
    }

    pthread_mutex_unlock(&event_mutex); // 解锁
    printf("recvdata 解除 event_mutex\n");

}

//初始化事件
void readctor::eventset(event * ev, int fd, void (readctor::* call_back)(int ,int , void *), void * arg){
    ev -> fd = fd;
    ev -> call_back = call_back;
    ev -> arg = arg;

    ev -> events = 0;
    ev -> status = 0; 

    if(ev->lockinit == false){
        pthread_mutex_init(&ev->pthlock, NULL);
        pthread_cond_init(&ev->pthcond, NULL);

        pthread_mutex_init(&ev->datalock, NULL);
        pthread_cond_init(&ev->datacond, NULL);
        
        pthread_mutex_init(&ev->splock, NULL);
        pthread_cond_init(&ev->spcond, NULL);

        ev->lockinit = true;
    }

    ev->poolrs = false;
    
    /*
    ev->pthlock = PTHREAD_MUTEX_INITIALIZER;
    ev->pthcond = PTHREAD_COND_INITIALIZER;
    
    ev->datalock = PTHREAD_MUTEX_INITIALIZER;
    ev->datacond = PTHREAD_COND_INITIALIZER;
    */
   
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
        return;
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
    int opt = 1;
    if (setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {//端口复用
        perror("setsockopt failed");
        close(lfd);
        return ;
    }
    fcntl(lfd, F_SETFL, O_NONBLOCK);

    memset(&addr, 0, sizeof addr);
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    bind(lfd,(sockaddr *)&addr, sizeof addr);

    listen(lfd, 100);

    eventset(&r_events[MAX_EVENTS], lfd, &readctor::acceptconn, &r_events[MAX_EVENTS]);
    eventadd(EPOLLIN, &r_events[MAX_EVENTS]);
}



void readctor::readctorinit(unsigned short port){
    pthread_mutex_init(&event_mutex,NULL);
    signal(SIGPIPE, SIG_IGN); 
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
            printf("epoll_wait error :%s\n",strerror(errno));
            continue;
        }

        for(i = 0; i < nfd; i++){
            event *ev = (event *) events[i].data.ptr;
            //读事件，调用读回调
            if(!ev->poolrs){
                ev->poolrs = true;
                if((events[i].events & EPOLLIN) && (ev -> events & EPOLLIN)){
                    struct EventContext* ctx = (struct EventContext*)malloc(sizeof (struct EventContext));
                    ctx->ev = ev;
                    ctx->obj = this;
                    evq.push(ctx);
                    pthpool.PushTask(event_callback_wrapper, ctx);
                }
                //写事件，调用写回调
                if((events[i].events & EPOLLOUT) && (ev -> events & EPOLLOUT)){
                    struct EventContext* ctx = (struct EventContext*)malloc(sizeof (struct EventContext));
                    ctx->ev = ev;
                    ctx->obj = this;
                    evq.push(ctx);
                    pthpool.PushTask(event_callback_wrapper, ctx);
                }
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
