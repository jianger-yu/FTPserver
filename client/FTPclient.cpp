#include "client.h"
#include "../socket/socket.h"
#include "../EpollReactor.hpp"

pthread_mutex_t lock;

FTPClient::FTPClient()
    : fd_(socket(AF_INET, SOCK_STREAM, 0)),
      socket_(std::make_unique<Socket>(fd_)) {}

FTPClient::~FTPClient() {
  close(fd_);
}


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


int main(){
  pthread_mutex_init(&lock,NULL);

  FTPClient client;
  client.connectToHost("127.0.0.1", 2100);
  Socket sk(*client.getSocket());
  char buf[1024];
  std::string str;

  pthread_t tid = 0;
  //pthread_create(&tid, NULL, read_clit,(void *)client.getSocket());

  while(1){
    memset(buf, 0, sizeof buf);
    int ret = read(STDIN_FILENO,buf,1024);
    buf[ret] = '\0';
    str = buf;
    sk.sendMsg(str);

  } 
  pthread_mutex_destroy(&lock);
  return 0;
}