#pragma once

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "Socket.h"
#include "InetAddress.h"
#include "Buffer.h"

class HTTP
{
public:
    // 读取文件的名称m_real_file大小
    static const int FILENAME_LEN = 200;
    // 读缓冲区大小
    static const int READ_BUFFER_SIZE = 2048;
    // 写缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024;

    // 报文的请求方式
    enum METHOD
    {
        GET = 0,
        POST
    };

    // 主状态机状态
    enum CHECK_STATS
    {
        CHECN_STATE_REQUESTLINE = 0,
        CHECN_STATE_HEADER,
        CHECNK_STATE_CONTENT
    };

    // 报文解析的结果
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

    HTTP();
    ~HTTP();
public:
    // 初始化套接字地址，函数内部调用私有化方法init
    void init(int sockfd, const sockaddr_in& addr);
    // 关闭http连接
    void close_conn(bool real_close=true);
    void process();
    // 读取浏览器发送来的全部数据
    bool read_once();
    // 响应报文写入函数
    bool write();
    sockaddr_in* get_address() { return &addr_; }
    void initmysql_result();
    void initreesultFile(conncetion_pool* connPool);

private:
    void init();
    HTTP_CODE proces_read(); // 从readBuffer读取数据，并处理请求报文
    bool process_write(HTTP_CODE ret); // 向
     


public:
    static int m_epollfd_;
    static int m_user_count_;
    // MYSQL* mysql;

private:
    
    int m_sockfd;
    sockaddr_in addr_;

    char read_buf_[READ_BUFFER_SIZE];
    int m_read_idx; // readBuffer中数据的最后一个字节的下一个位置
    int m_checked_idx; // readBuffer读取的位置m_checked_idx;
    int m_start_line; //readbuf中已经解析的字符个数

    char write_buf_[WRITE_BUFFER_SIZE];
    // Buffer write_buf_; // 存储发出的响应报文数据
    int m_write_idx; // writeBuffer中的查昂都

    CHECK_STATS check_state_; // 主状态机状态
    METHOD method_; // 请求方法

    // 解析请求报文中对应的6个变量
    char real_file_[FILENAME_LEN];
    char* url_;
    char* version_;
    char* host_;
    int content_length_;
    bool linger_;
    

    char* file_address; // 服务器上的文件地址
    struct stat m_file_state_; 
    struct iovec iv_[2]; // io向量机制iovec
    int iv_count_;
    int cgi_; // 是否启用POST
    char* string_; // 存储请求头数据
    int bytes_to_send; // 剩余发送字节数
    int bytes_have_send; // 已经发送字节数
};