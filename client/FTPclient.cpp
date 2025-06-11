#include "client.h"
#include <algorithm>
#include "../socket/socket.h"
#include "../EpollReactor.hpp"

//定义数据通信伪客户端
FTPClient dataclient;

FTPClient::FTPClient()
    : fd_(socket(AF_INET, SOCK_STREAM, 0)),
      socket_(std::make_unique<Socket>(fd_)),pasv(false),tip(0){}

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
  printf("\033[0;36m==========================================================\033[0m\n");
  printf("           \033[0;33m欢迎进入FTP服务器,请选择要进行的操作\033[0m\n");
  if(this->pasv == false)
    printf("   \033[0;32m1.PASV  ------------(请求被动模式，获取数据传输端口)\033[0m\n");
  else
    printf("   \033[0;32m1.EXIT  ------------(回到默认模式)\033[0m\n");
  printf("   \033[0;32m2.LIST  ------------(获取文件列表)\033[0m\n");
  printf("   \033[0;32m3.STOR  ------------(上传文件)\033[0m\n");
  printf("   \033[0;32m4.RETR  ------------(下载文件)\033[0m\n");
  printf("   \033[0;32m5.QUIT  ------------(退出服务器)\033[0m\n");
  if(this->pasv == false){
    printf("                                        \033[0;31m当前不为被动模式\033[0m\n");
    printf("   \033[0;32mtip:使用2~4命令需要进入被动模式\033[0m                       \n");
  }
  else{
    printf("                                          \033[0;34m已进入被动模式\033[0m\n");
    printf("   \033[0;32mtip:成功进入被动模式，可以进行文件传输\033[0m\n");
  }
  printf("\033[0;36m==========================================================\033[0m\n");
  if(tip == 1) printf("\033[0;32m文件下载成功,已为您退出数据连接\033[0m\n");
  else if(tip == 2) printf("\033[0;32m文件成功上传至服务器,已为您退出数据连接\033[0m\n");
  else if(tip == 3) printf("\033[0;31m未在服务器列表找到该文件,已为您退出数据连接\033[0m\n");
  printf("\033[0;32m请输入命令:>\033[0m");
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
  sock->recvMsg(str);
  //alarm(0);
  printf("recv string :%s\n",str.c_str());
  short newport = 0;
  std::string newip;

  //将数据转化为可用的变量
  trans(str, newip, newport);
  printf("ip:%s  port:%d\n",newip.c_str(), newport);

  
  //连接主机
  if(false == dataclient.connectToHost(newip.c_str(), newport)){
    printf("连接至主机失败！！ error:%s\n", strerror(errno));
    sleep(5);
    exit(1);
  }
  else{
    printf("连接至主机成功！！\n");
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
  printf("\033[0;36m==========================================================\033[0m\n");
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

bool FTPClient::RETR(){
  Socket* sock = this->getSocket();
  std::string str = "RETR";
  int ret = sock->sendMsg(str);
  if(ret == -1){
    printf("send 'RETR' command error:%s\n",strerror(errno));
    exit(1);
  }
  str.clear();

  //下载文件
  //输出指引
  printf("输入要下载文件的文件名:>");
  fflush(stdout); // 手动刷新标准输出缓冲区

  char buf[1024];
  memset(buf,0,sizeof buf);
  //获取输入文件名
  ret = read(STDIN_FILENO,buf,sizeof buf);
  if(ret <= 0){
    printf("输入错误:%s\n",strerror(errno));
    exit(1);
  }
  buf[ret - 1] = '\0';
  printf("输入成功:%s\n",buf);
  //获取数据传输套接字
  Socket* datasock = dataclient.getSocket();
  str = buf;
  ret = datasock->sendMsg(str);
  if(ret == -1){
    printf("send file name error:%s\n",strerror(errno));
    exit(1);
  }
  printf("[Client] 发送文件名 : %s\n", str.c_str());
  str.clear();

  //sleep(5);
  //获取文件
  ret = datasock->recvMsg(str);
  printf("读取服务器传输成功：%s\n",str.c_str());
  if(ret == -1){
    printf("recv file_send count error:%s\n",strerror(errno));
    exit(1);
  }  
  if(strcmp(str.c_str(), "450 Requested file action not taken.") == 0 || strcmp(str.c_str(), "no search file.") == 0){
    printf("未在服务器列表找到该文件，已为您退出连接\n");
    tip = 3;
    return false;
  }
  printf("正在下载文件...\n");
  
  int tmp;
  long endrt;
  sscanf(str.c_str(),"%dand%ld",&tmp, &endrt);
  printf("tmp == %d, endrt == %ld\n", tmp, endrt);
  FILE * file = fopen(buf, "wb");
  //rewind(file);
  for(int j = 0; j <= tmp; j++){
    if(j == tmp && endrt == 0) j++;
    str.clear();
    datasock->recvMsg(str);
    if(j < tmp)
      fwrite(str.data(), 1, 4096, file);
    else
      fwrite(str.data(), 1, endrt, file);
  }
  fflush(file);
  printf("下载成功！正在为您退出当前模式\n");
  tip = 1;
  //exit(1);
  //str = "success";
  //datasock->sendMsg(str);
  return true;
}
std::string GetFileName(const char arr[]){
  int i = strlen(arr) - 1;
  std::string str;
  for(;i >= 0; i--){
    if(arr[i] == '/') break;
    else str.push_back(arr[i]);
  }
  std::reverse(str.begin(), str.end());
  return str;
}
bool FTPClient::STOR(){
  Socket* sock = this->getSocket();
  std::string str = "STOR";
  int ret = sock->sendMsg(str);
  if(ret == -1){
    printf("send 'STOR' command error:%s\n",strerror(errno));
    exit(1);
  }
  str.clear();
  //下载文件
  //输出指引
  char arr[1024];
  char path[3072];
  FILE* file;
  printf("\033[0;32m输入需要传输的文件路径:>\033[0m");
  while(1){
    fflush(stdout); // 手动刷新标准输出缓冲区

    memset(arr,0,sizeof arr);
    //获取输入文件名
    ret = read(STDIN_FILENO,arr,sizeof arr);
    printf("输入路径为：%s",arr);
    if(ret <= 0){
      printf("输入错误:%s\n",strerror(errno));
      exit(1);
    }
    if(arr[0] == '\n') {
      system("clear");
      menu();
      printf("STOR\n\033[0;32m路径有误,请重新输入:>\033[0m");
      continue;
    }
    arr[ret - 1] = '\0';
    char *pa = NULL;
    pa = getcwd(NULL, 0);
    //若不为绝对路径，拼接路径
    if(arr[0]!='/')
      sprintf(path,("%s/%s"),pa,arr);
    //若为绝对路径，直接传入路径
    else strcpy(path,arr);
    file = fopen(path, "rb");
    if (file == NULL) {
      system("clear");
      menu();
      printf("STOR\n\033[0;32m路径有误,请重新输入:>\033[0m");
      continue;
    }
    else break;
   }
  printf("输入成功:%s\n",arr);
  printf("正在上传文件...\n");
  //获取数据传输套接字
  Socket* datasock = dataclient.getSocket();
  //发送文件名
  str.clear();
  str = GetFileName(arr);
  datasock->sendMsg(str);

  //获取文件状态
  struct stat st;
  if(lstat(path,&st) == -1){//获取文件状态
      perror("stat");//处理错误返回值
      exit(1);
  }
  //判断需要分几次传输
  char buf[4096];//一次发送4k
  int tmp = st.st_size / sizeof buf;
  //if(st.st_size % 4 != 0) tmp++;
  printf("文件大小:%ld  发送文件块数tmp == %d, endrt == %ld\n",st.st_size,tmp,(st.st_size + 4096)%4096);
  //发送需要传输的次数
  str.clear();
  char tmps[30];
  sprintf(tmps,"%dand%ld",tmp,(st.st_size + 4096)%4096);
  str = tmps;
  datasock->sendMsg(str);

  size_t bytesRead;
  while ((bytesRead = fread(buf, 1, sizeof(buf), file)) > 0) {
      str.clear();
      str.assign(buf, bytesRead); 
      datasock->sendMsg(str);
      //memset(buf, 0, sizeof buf);
  }
  printf("上传成功！\n");
  tip = 2;
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
        tip = 0;
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
    else if(strcmp(buf,"RETR") == 0){
      if(this->pasv == true){
        system("clear");  
        this->LIST();
        this->RETR();
        this->EXIT();
        //this->EXIT();
      }
      continue;
    }
    else if(strcmp(buf,"STOR") == 0){
      if(this->pasv == true){
        this->STOR();
        this->EXIT();
      }
      continue;
    }
    else if(strcmp(buf,"QUIT") == 0){
      if(this->pasv == true)
        this->EXIT();
      client.~FTPClient();
      printf("\033[0;32m成功退出服务器\033[0m\n");
      exit(1);
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