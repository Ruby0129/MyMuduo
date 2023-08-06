#pragma once

#include <mymuduo/TcpServer.h>
#include <mymuduo/Logger.h>

#include <string>
#include <functional>
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

class HTTPServer
{
public:
    // 读取文件的名称real_file_大小
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    
    // 报文的请求方法，暂时只有GET和POST
    enum METHOD
    {
        GET = 0, POST
    };
    // 主状态机的状态
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    // 报文解析的结果
    enum HTTP_CODE
    {
        // NO_REQUEST 请求不完整，需要继续读取请求报文数据
        // GET_REQUEST 获取了完整的HTTP请求 跳转do_request完成请求资源映射
        // BAD_REQUEST HTTP请求有语法错误或请求资源为目录
        // INTERNAL_ERROR 服务器内部错误

        NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, 
        INTERNAL_ERROR, CLOSED_CONNECTION
    };
    // 从状态机的状态
    // LINE_OK: 完整读取一行
    // LINE_BAD: 报文语法有误
    // LINE_OPEN: 读取的行不完整
    enum LINE_STATUS{ LINE_OK = 0, LINE_BAD, LINE_OPEN };

    HTTPServer(EventLoop* loop, 
        const InetAddress& addr, 
        const std::string& name);

    ~HTTPServer();

    void start() { server_.start(); }
private:
    
    // 处理请求报文
    HTTP_CODE process_read();
    // 写入响应报文
    bool process_write(HTTP_CODE ret);

    // 主状态机解析报文中的请求行数据
    HTTP_CODE parse_request_line(char* text);
    // 主状态机解析报文中的请求头数据
    HTTP_CODE parse_headers(char* text);
    // 主状态机捷尔西报文中的请求内容
    HTTP_CODE parse_content(char* text);
    // 生成响应报文
    HTTP_CODE do_request();

    // 从状态机读取一行，分析是请求的哪一部分
    LINE_STATUS parse_line();

    // 根据响应报文格式，生成对应8个部分，以下函数均由process_write调用
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_status_line(int status, const char* title);
    void add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

    char* Get_line();

    // 连接建立或者断开的回调
    void onConnection(const TcpConnectionPtr &conn);
    // 可读写事件回调
    void onMessage(const TcpConnectionPtr &conn, 
                   Buffer* buffer,
                   Timestamp time);

    char readBuf_[READ_BUFFER_SIZE]; // http请求报文
    char writeBuf_[WRITE_BUFFER_SIZE]; // http响应报文

    int read_idx_; // read_buf读取的位置
    int checked_idx_; // read_buf已经解析的字符个数
    int start_line_;

    int write_idx_;

    // 主状态机的状态
    CHECK_STATE check_state_;
    // 请求方法
    METHOD method_;

    // 以下为解析请求报文中对应的6个变量
    char real_file_[FILENAME_LEN];
    char* url_;
    char* version_;
    char* host_;
    char* message_;

    int content_length_;

    bool linger_;
    bool isParsing_;
    
    struct stat file_stat_;

    char* file_address_; // 读取服务器上的文件地址
    int cgi_; // 是否启用POST

    int bytes_to_send_; // 剩余发送字节数
    int bytes_have_send_; // 已发送字节数

    EventLoop* loop_;
    TcpServer server_; 
};
