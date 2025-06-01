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

  //根据当前模式输出菜单
  void menu(void);

  //客户端运行应有功能
  void ctlthread(void);

  void trans(std::string &, std::string &, short&);
  

  /**
   * @brief 利用通信套接字发送PASV命令给服务器，服务器回复一个端口地址，利用端口地址定义数据传输套接字
   * @return 成功返回true,失败返回false
   */
  bool PASV(void);

  /**
   * @brief 若处于被动模式，利用通信套接字发送LIST命令给服务器，之后从数据套接字读取内容，输出列表
   * @return 成功返回true,失败返回false
   */
  bool LIST(void);

  /**
   * @brief 若处于被动模式，利用通信套接字发送STOR命令给服务器，再从数据套接字发送文件(发送文件在socket类实现)
   * @return 成功返回true,失败返回false
   */
  bool STOR(void);
  
  /**
   * @brief 若处于被动模式，利用通信套接字发送RETR命令给服务器，再从数据套接字接收文件(接收文件在socket类实现)
   * @return 成功返回true,失败返回false
   */
  bool RETR(void);

  /**
   * @brief 用户传来退出命令，重置套接字及类对象，pasv置为false
   * @return 成功返回true,失败返回false
   */
  bool EXIT(void);


  private:
  // 通信套接字
  int fd_;
  // 通信类对象
  std::unique_ptr<Socket> socket_;

  //数据传输套接字
  int datafd;
  //数据传输类对象
  std::unique_ptr<Socket> datasocket;
  //是否进入被动模式-----------true进入，false未进入
  bool pasv;


};