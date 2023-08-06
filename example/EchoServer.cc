#include <mymuduo/TcpServer.h>
#include <mymuduo/Logger.h>

#include <string>
#include <functional>
#include <iostream>

class EchoServer
{
public:
    EchoServer(EventLoop* loop, 
        const InetAddress& addr, 
        const std::string& name)
        : server_(loop, addr, name)
        , loop_(loop)
    {
        // 注册回调函数
        server_.setConnectionCallback(
            std::bind(&EchoServer::onConnection, this, std::placeholders::_1)
        );

        server_.setMessageCallback(std::bind(&EchoServer::onMessage, this, 
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
        );

        // 设置合适的loop线程数量
        server_.setThreadNum(3);
    }

    void start()
    {
        server_.start();
    }
private:
    // 连接建立或者断开的回调
    void onConnection(const TcpConnectionPtr &conn)
    {
        if (conn->connected())
        {
            LOG_INFO("Connection UP : %s", conn->peerAddress().toIpPort().c_str());
        }
        else
        {
            LOG_INFO("Connection DOWN : %s", conn->peerAddress().toIpPort().c_str());           
        }
    }

    // 可读写事件回调
    void onMessage(const TcpConnectionPtr &conn, 
                   Buffer* buffer,
                   Timestamp time)
    {
        std::string msg = buffer->retrieveAllAsString();
        std::cout << "send_start" << std::endl;
        conn->send(msg);
        std::cout << "send_end" << std::endl;
        conn->shutdown(); // 关闭写端 EPOLLHUP => closeCallback_
    }

    EventLoop* loop_;
    TcpServer server_;
};
