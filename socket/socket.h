#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string>
#include <iostream>
#include <cstring>
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

class Socket {
  public:
  Socket(int sockfd);
  ~Socket();

  /**
   * @brief 发送消息
   * @param msg 要发送的消息
   * @return 成功返回0，失败返回-1
   */
  int sendMsg(std::string msg);

  /**
   * @brief 接收消息
   * @param msg 接收到的消息
   * @return 成功返回0，失败返回-1
   */
  int recvMsg(std::string& msg);

  private:
  /**
   * @brief 忙轮询保证发送数据
   * @param sockfd 目标套接字
   * @param buf 数据内容
   * @param len 数据长度
   * @return 成功返回true，失败返回false
   */
  bool send_all(int sockfd,const void * buf,size_t len);

  /**
   * @brief 忙轮询保证读取数据
   * @param sockfd 目标套接字
   * @param buf 数据存放位置
   * @param len 数据存放位置长度
   * @return 成功返回true，失败返回false
   */
  bool recv_all(int sockfd,void * buf,size_t len);
  typedef struct bag{
    uint32_t len;
    std::string str;
  }bag;

  int sockfd_;
};
