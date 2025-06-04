#include "client.h"
#include "../socket/socket.h"
#include "../EpollReactor.hpp"

//定义数据通信伪客户端
FTPClient dataclient;

FTPClient::FTPClient()
    : fd_(socket(AF_INET, SOCK_STREAM, 0)),
      socket_(std::make_unique<Socket>(fd_)),pasv(false){}

FTPClient::~FTPClient() {
  close(fd_);
}

FTPClient client;
FTPClient * clip = NULL;

bool FTPClient::connectToHost(const char* ip, unsigned short port) {
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  int ret = inet_pton(AF_INET,ip,&addr.sin_addr.s_addr);
  if(ret <= 0) return false;
  
  ret = connect(fd_,(struct sockaddr *)&addr,sizeof addr);
  if(ret == -1) return false;
  return true;
}

std::string str;
void *read_clit(void * fd){
    while(1){
        ((Socket*)fd)->recvMsg(str);
        for(int i = 0; i < str.size(); i++) printf("%c",str[i]);
        printf("\n");
    }
    return NULL;
}

void FTPClient::menu(){
  printf("==========================================================\n");
  printf("           欢迎进入FTP服务器,请选择要进行的操作\n");
  if(this->pasv == false)
    printf("   1.PASV  ------------(请求被动模式，获取数据传输端口)\n");
  else
    printf("   1.EXIT  ------------(回到默认模式)\n");
  printf("   2.LIST  ------------(获取文件列表)\n");
  printf("   3.STOR  ------------(上传文件)\n");
  printf("   4.RETR  ------------(下载文件)\n");
  if(this->pasv == false){
    printf("                                        当前不为被动模式\n");
    printf("   tip:使用2~4命令需要进入被动模式                       \n");
  }
  else{
    printf("                                          已进入被动模式\n");
    printf("   tip:成功进入被动模式，可以进行文件传输\n");
  }
  printf("==========================================================\n");
  printf("请输入命令:>");
  fflush(stdout); // 手动刷新标准输出缓冲区
}


//转换ip及端口号,形如：227 entering passive mode (h1,h2,h3,h4,p1,p2)，其中端口号为 p1*256+p2，IP 地址为 h1.h2.h3.h4。
void FTPClient::trans(std::string &str, std::string &newip, short &newport){
  short p1 = 0, p2 = 0;
  //转化ip
  int i = 0, cnt = 0;
  while(str[i++] != '(');
  while(cnt < 4){
    if(str[i] == ','){
      cnt++;
      if(cnt <= 3)
        newip.push_back('.');
    } else  {
      newip.push_back(str[i]);
    }
    i++;
  }
  //转化port
  while(str[i] != ','){
    p1 = 10*p1 + str[i] - '0';
    i++;
  }
  i++;//指向p2
  while(str[i] != ')'){
    p2 = 10*p2 + str[i] - '0';
    i++;
  }
  newport = p1*256 + p2;
}

void sig_catch(int sig){
  if(sig == SIGALRM){
    clip->~FTPClient();
    FTPClient newcli;
    clip = &newcli;
    if(newcli.connectToHost("127.0.0.1", 2100)==false){
      printf("连接FTP服务器2100端口失败,error:%s\n",strerror(errno));
      exit(1);
    }
    newcli.PASV();
    newcli.ctlthread();
  }
}

bool FTPClient::PASV(){
  printf("连接中 ...\n");
  if(pasv == true) return true;
  Socket* sock = this->getSocket();
  std::string str;
  str = "PASV";
  
  //发送PASV命令
  int ret = sock->sendMsg(str);
  if(ret == -1){
    printf("send 'PASV' command error:%s\n",strerror(errno));
    exit(1);
  }
  str.clear();
  //alarm(2);
  struct sigaction act;
  act.sa_flags = 0;
  act.sa_handler = sig_catch;
  sigemptyset(&(act.sa_mask));
  sigaction(SIGALRM,&act,NULL);

  //读取ip及端口号,形如：227 entering passive mode (h1,h2,h3,h4,p1,p2)，其中端口号为 p1*256+p2，IP 地址为 h1.h2.h3.h4。
  ret = sock->recvMsg(str);
  //alarm(0);
  //printf("recv string :%s\n",str.c_str());
  short newport = 0;
  std::string newip;

  //将数据转化为可用的变量
  trans(str, newip, newport);
  //printf("ip:%s  port:%d\n",newip.c_str(), newport);

  
  //连接主机
  if(false == dataclient.connectToHost(newip.c_str(), newport)){
    printf("连接至主机失败！！ error:%s\n", strerror(errno));
    sleep(5);
    exit(1);
  }
  else{
    //printf("连接至主机成功！！\n");
    //sleep(10);
  }

  pasv = true;
  return true;
}

bool FTPClient::EXIT(){
  Socket* sock = this->getSocket();
  std::string str = "EXIT";
  int ret = sock->sendMsg(str);
  if(ret == -1){
    printf("send 'EXIT' command error:%s\n",strerror(errno));
    exit(1);
  }
  str.clear();
  
  dataclient.~FTPClient();
  dataclient.reinitialize();
  this->pasv = false;
  return true;
}

bool FTPClient::LIST(){
  Socket* sock = this->getSocket();
  std::string str = "LIST";
  int ret = sock->sendMsg(str);
  if(ret == -1){
    printf("send 'LIST' command error:%s\n",strerror(errno));
    exit(1);
  }
  str.clear();

  //读取文件数
  Socket* datasock = dataclient.getSocket();
  ret = datasock->recvMsg(str);
  if(ret == -1) printf("recv file count error:%s\n",strerror(errno));
  int cnt;
  sscanf(str.c_str(),"%d",&cnt);
  str.clear();
  cnt -= 2;
  system("clear");
  printf("==========================================================\n");
  printf("                   服务器内文件清单\n");
  printf("%-12s%27s %33s\n","文件名","文件大小","上传时间");
  //依次读取固定量个文件
  for(int i = 0; i < cnt; i++){
    ret = datasock->recvMsg(str);
    if(ret == -1) printf("recv file information error:%s\n",strerror(errno));
    printf("%s",str.c_str());
    str.clear();
  }

  return true;
}

void FTPClient::ctlthread(void){
  Socket sk(*getSocket());
  char buf[1024];
  std::string str;
  int lscnt = 0;
  while(1){
    if(lscnt == 0)
      system("clear");
    else lscnt--;
    menu();
    memset(buf,0,sizeof buf);
    int ret = read(STDIN_FILENO,buf,sizeof buf);
    if(ret <= 0){
      printf("输入错误:%s\n",strerror(errno));
      exit(1);
    }
    buf[ret - 1] = '\0';
    str = buf;
    if(strcmp(buf,"PASV") == 0){
      if(this->pasv == false)
        this->PASV();
      continue;
    }
    else if(strcmp(buf,"EXIT") == 0){
      if(this->pasv == true)
        this->EXIT();
      continue;
    }
    else if(strcmp(buf,"LIST") == 0){
      if(this->pasv == true && lscnt == 0){
        this->LIST();
        if(!lscnt) lscnt++;
      }
      continue;
    }
  } 
}

int main(){
  while(client.connectToHost("127.0.0.1", 2100)==false){
    printf("连接FTP服务器2100端口失败,error:%s\n",strerror(errno));
    printf("正在重新连接...\n");
    sleep(1);
  }
  clip = &client;
  client.ctlthread();


  return 0;
}

//pthread_t tid = 0;
//pthread_create(&tid, NULL, read_clit,(void *)client.getSocket());