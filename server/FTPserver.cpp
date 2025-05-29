#include "../socket/socket.h"
#include "../EpollReactor.hpp"

server::server()
    : listenfd_(socket(AF_INET, SOCK_STREAM, 0)), socket_(nullptr) {
    }

server::~server() {
  if (listenfd_ != -1) {
    close(listenfd_);
  }
}

bool server::setListen(unsigned short port) {
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr_len = sizeof addr;

  int ret = bind(listenfd_,(struct sockaddr*)&addr, sizeof addr);
  if(ret == -1) return false;
  
  ret = listen(listenfd_, 128);
  if(ret == -1) return false;
  return true;
}

bool server::acceptConn() {
  int cfd = accept(listenfd_,NULL, NULL);
  if(cfd == -1)
    return false;
  socket_.reset(new Socket (cfd));
  return true;
}

int main(){
  
  readctor rct(2100);
  while(1){
    
  }
  return 0;
}

/*server sv;
  sv.setListen(2100);
  sv.acceptConn();
  Socket sk(*sv.getSocket());
  std::string str;
  while(1){
    str.clear();
    sk.recvMsg(str);
    for(int i = 0; i < str.size() ; i++) printf("%c",str[i]);
    puts("");
  }*/