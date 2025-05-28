#include "socket.h"

Socket::Socket(int sockfd) : sockfd_(sockfd) {}

Socket::~Socket() {
  close(sockfd_);
}

bool Socket::send_all(int sockfd,const void * buf,size_t len){
  const char*p = static_cast<const char*>(buf);
  while(len > 0){
    int n = send(sockfd,p,len,0);
    if(n <= 0) return false;
    p += n;
    len -= n;
  }
  return true;
}

bool Socket::recv_all(int sockfd,void * buf,size_t len){
  char* p = static_cast<char*>(buf);
  while(len > 0){
    int n = recv(sockfd,p,len,0);
    if(n < 0) return false;
    if(n == 0){
      printf("对端已断开连接\n");
      exit(1);
    }
    p += n;
    len -= n;
  }
  return true;
}

int Socket::sendMsg(std::string msg) {
  uint32_t len = htonl(msg.size());
  if(!send_all(sockfd_,&len,sizeof len)) return -1;
  if(!send_all(sockfd_,msg.data(),msg.size())) return -1;
  return 0;
}

int Socket::recvMsg(std::string& msg) {
  uint32_t len, slen;
  if(!recv_all(sockfd_,&len,sizeof len)) return -1;
  slen = ntohl(len);
  msg.clear();
  msg.resize(slen);
  if(!recv_all(sockfd_,msg.data(),slen)) return -1;
  return 0;
}
