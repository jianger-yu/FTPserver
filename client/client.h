#pragma once
#include <memory>
#include "../socket/socket.h"

class FTPClient {
  public:
  FTPClient();
  ~FTPClient();

  /**
   * @brief 获取当前连接的通信套接字
   * @return 返回TcpSocket指针
   */
  Socket* getSocket() const { return socket_.get(); }

  /**
   * @brief 连接到主机
   * @param ip 主机的IP地址
   * @param port 主机的端口号
   * @return 成功返回true，失败返回false
   */
  bool connectToHost(const char* ip, unsigned short port);

  private:
  // 通信套接字
  int fd_;
  // 通信类对象
  std::unique_ptr<Socket> socket_;
};