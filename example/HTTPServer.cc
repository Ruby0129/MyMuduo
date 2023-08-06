#include "HTTPServer.h"

#include<string.h>
#include <stdarg.h>

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

HTTPServer::HTTPServer(EventLoop* loop, 
        const InetAddress& addr, 
        const std::string& name)
        : loop_(loop)
        , server_(loop, addr, name)
        , start_line_(0)
        , check_state_(CHECK_STATE_REQUESTLINE)
        , bytes_to_send_(0)
        , bytes_have_send_(0)
        , linger_(false)
        , method_(GET)
        , cgi_(0)
        , read_idx_(0)
        , checked_idx_(0)
        , content_length_(0)
        , url_(nullptr)
        , version_(nullptr)
        , host_(nullptr)
        , isParsing_(false)
        , message_(nullptr)
        , file_address_(nullptr)
        , write_idx_(0)
    // 注册回调函数
{
    server_.setConnectionCallback(
        std::bind(&HTTPServer::onConnection, this, std::placeholders::_1)
    );
    server_.setMessageCallback(
        std::bind(&HTTPServer::onMessage, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
    );

    memset(real_file_, 0, FILENAME_LEN);
    memset(readBuf_, '\0', READ_BUFFER_SIZE);
    memset(writeBuf_, '\0', WRITE_BUFFER_SIZE);
    
    // 设置合适的loop线程数量
    server_.setThreadNum(3);
}

// 连接建立或者断开的回调
void HTTPServer::onConnection(const TcpConnectionPtr &conn)
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

std::string pre_str;
// 可读写事件回调
void HTTPServer::onMessage(const TcpConnectionPtr &conn, 
                   Buffer* buffer,
                   Timestamp time)
{
    // 从Buffer中读取HTTP请求报文
    // 不处理重复的HTTP报文
    std::string newReadstr = buffer->retrieveAllAsString();
    if (pre_str != newReadstr)
    {
        pre_str = newReadstr;
        const char* data = newReadstr.c_str();
        // strcat(readBuf_,data);
        LOG_INFO("readBuf_ had benn changed...\n");
        strcpy(readBuf_, data);
        read_idx_ = newReadstr.size();
        start_line_ = 0;

        std::cout << readBuf_ << std::endl;
        // readBuf_ += buffer->retrieveAllAsString();
        
        // 解析报文
        HTTP_CODE read_ret = process_read();

        // NO_REQUEST, 请求不完整，需要继续接收请求数据
        if (read_ret == NO_REQUEST)
        {
            conn->send("<html><head><title>inComplete Request Please Retry!</title><head></html> \n");
            pre_str = "";
            read_idx_ = 0;
            start_line_ = 0;
            checked_idx_ = 0;
            check_state_ = CHECK_STATE_REQUESTLINE;
            conn->shutdown(); // 关闭写端 EPOLLHUP => closeCallback_
            return;
        }

        // 调用process_write完成报文响应
        bool write_ret = process_write(read_ret);
        if(!write_ret)
        {
            conn->send("<html><head><title>Hello Conn Sorry is Failed!</title><head></html> \n");
            pre_str = "";
            read_idx_ = 0;
            start_line_ = 0;
            checked_idx_ = 0;
            check_state_ = CHECK_STATE_REQUESTLINE;
            conn->shutdown(); // 关闭写端 EPOLLHUP => closeCallback_
            // LOG_INFO("URL = %s, HOST = %s, VERSION = %s, CONTENT-LENGTH = %d\n",
            // url_, host_, version_, conteng_length_);
        }
        else
        {
            LOG_INFO("send Message to Client:writeBuf_: %s, file_address_: %s \n", writeBuf_, file_address_);
            conn->send(writeBuf_);
            conn->send(file_address_);
            write_idx_ = 0;
            memset(writeBuf_, 0, WRITE_BUFFER_SIZE);
            pre_str = "";
            read_idx_ = 0;
            start_line_ = 0;
            checked_idx_ = 0;
            check_state_ = CHECK_STATE_REQUESTLINE;
        }
    }
}

// 处理请求报文
    /**
     *  process_read通过while循环，将主从状态机进行封装，对报文的每一行进行循环处理
     *  主状态机转移到CHECK_STATE_CONTENT, 该条件涉及解析消息体
     *  从状态机转移到LINE_OK，该条件涉及解析请求行和请求头部
     *  两者为或关系，，当条件为真继续循环，否则退出
     * 
     * 从状态机读取数据
     * 调用get_line()函数，通过start_line_将从状态机读取数据间接赋值给text
     * 主状态机解析text
    */
char* HTTPServer::Get_line()
{
    // start_line是行在read_buffer
    // LOG_INFO("Enter GET LINE readBUf: %s \n", readBuf_);
    LOG_INFO("start_line_ = %d", start_line_);
    return readBuf_ + start_line_;
}

HTTPServer::HTTP_CODE HTTPServer::process_read()
{
    // 初始化从状态机转台，HTTP请求解析结果
    LOG_INFO("Enter process_read()");
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    // parse_line为从状态机的具体实现
    while((check_state_ == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status=parse_line()) == LINE_OK))
    {
        LOG_INFO("Enter HTTP Parse \n");
        text = Get_line(); // start_line 是从每一个数据行在read_buf中起始位置
        std::cout << std::string(text) <<std::endl;
        start_line_ = checked_idx_; // checked_idx时从状态机在read_buf中读取的位置

        // 主状态机的三种状态转移逻辑
        switch(check_state_)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                // 解析请求行
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST)
                    return BAD_REQUEST;
                break;
            }
            case CHECK_STATE_HEADER:
            {
                // 解析请求头
                ret = parse_headers(text);
                if (ret == BAD_REQUEST)
                    return BAD_REQUEST;
                else if (ret == GET_REQUEST)
                {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                // 解析消息体
                ret = parse_content(text);

                // 完整解析POST请求后，跳转到报文响应函数
                if (ret == GET_REQUEST)
                    return do_request();
                
                // 解析完消息体即完成报文解析，避免再次进入循环，更新line_status
                line_status = LINE_OPEN;
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

// 从状态机读取一行，分析是请求的哪一部分
// 通过查找\r\n将报文拆解成单独的行进行解析
/**
 *  从状态机，将每行数据末尾的\r\n值为\0\0, 并更新从状态机在buffer中读取的位置checked_idx来驱动主状态机解析
 *  从状态机从read_Buf逐字节读取，判断当前字节是否为\r
 *  1.如果接下来的字符是\n, 将\r\n修改为\0\0, 将checked_idx指向下一行的开头，发挥LINE_OK
 *  2.接下来到达了Buffer末尾，表示buffer需要继续接收，返回LINE_OPEN
 *  3.否则语法错误，返回LINE_BAD
 *  当前字节不是\r,判断是否是\n
 *  如果前一个字符是\r, 将\r\n修改成\0\0，将checked_idx指向下一行开头，返回LINE_OK
 *  当前字节既不是\r也不是\n
 *  接收不完整，需要继续接收，返回LINE_OPEN
*/

// read_idx_指向readBuffer末尾
// checked_idx_指向从状态机当前正在分析的字节
HTTPServer::LINE_STATUS HTTPServer::parse_line()
{
    LOG_INFO("Enter parse line \n");
    char temp;
    for(; checked_idx_ < read_idx_; ++checked_idx_)
    {
        temp = readBuf_[checked_idx_];
        // temp为将要分析的字节
        if (temp == '\r')
        {
            LOG_INFO("meeting a line \n");
            // 下一个字符达到了buffer末尾，接收不完整，需要继续接收
            if ((checked_idx_ + 1) == read_idx_)
            {
                return LINE_OPEN;
            }
            // 下一个字符是\n, 将\r\n改为\0\0
            else if (readBuf_[checked_idx_ + 1] == '\n')
            {
                readBuf_[checked_idx_++] = '\0';
                readBuf_[checked_idx_++] = '\0';
                LOG_INFO("readBuf in Parse_line() : %s \n", readBuf_+checked_idx_);
                return LINE_OK;
            }
            // 都不符合，返回语法错误
            return LINE_BAD;
        }

        // 如果当前字符是\n, 也有可能读取到完整行
        // 一般是上次读取到\r就到buffer末尾了，没有接收完整，再次接收时会出现这种情况
        else if(temp == '\n')
        {
            LOG_INFO("a incomplete line \n");
            if (checked_idx_ > 1 && readBuf_[checked_idx_-1] == '\r')
            {
                readBuf_[checked_idx_ - 1] = '\0';
                readBuf_[checked_idx_++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }

    LOG_INFO("a incomplete line end parse line \n");
    // 没有找到\r\n, 需要继续接收
    return LINE_OPEN;
}

// 主状态机解析报文中的请求行数据
/**
 *  主状态机初始状态是CHECK_STATE_REQUESTLINE
 *  主状态机解析前，从状态机已经将每一行末尾\r\n改为\0\0，便于主状态机直接取出对应的字符串进行处理
 * 
 * CHECK_STATE_REQUESTLINE
 * 主状态机的初始状态，调用parse_request_line解析请求行
 * 解析函数从readBuf中解析HTTP请求行，获得请求方法、目标URL和HTTP版本号
 * 解析完成主状态机状态变为CHECK_STATE_HEADER
*/
HTTPServer::HTTP_CODE HTTPServer::parse_request_line(char* text)
{   
    LOG_INFO("Enter parse_request_line \n");
    // HTTP报文中，请求行用来说明请求类型、要访问的资源以及使用的HTTP版本，各个部分通过\t或空格分隔
    // 请求行中最先含有空格和\t任一字符的的位置并返回
    LOG_INFO("Enter LINE: %s \n", text);
    url_ = strpbrk(text, " \t");
    if (!url_)
    {   
        LOG_INFO("url is empty... BAD REQUEST\n");
        // 没有空格或\t， 报文格式有误
        return BAD_REQUEST;
    }

    // 该位置改为\0, 用于将前面数据取出
    *url_++ = '\0';

    char* method = text; // 取出数据，并通过与GET或POST比较，确定请求方式
    if(strcasecmp(method, "GET") == 0)
    {
        method_ = GET;
    }
    else if (strcasecmp(method, "POST") == 0)
    {
        method_ = POST;
        cgi_ = 1;
    }
    else
        return BAD_REQUEST;

    LOG_INFO("Method = %s \n", method);

    // url_此时跳过了第一个空格或\t字符，但不知道后面是否还有
    // url_指针继续向后偏移，通过查找，继续跳过空格和\t字符，指向请求资源的第一个字符
    // strspn(str1, str2) 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标
    url_ += strspn(url_, " \t");

    // 使用与判断请求方式相同的逻辑，判断HTTP版本号
    version_ = strpbrk(url_, " \t");
    if (!version_)
    {
        return BAD_REQUEST;
    }

    *version_++ = '\0';
    version_ += strspn(version_, " \t");

    LOG_INFO("VERSION = %s \n", version_);

    // 仅支持HTTP/1.1
    if (strcasecmp(version_, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    // 对请求资源的前7个字符进行判断
    // 这里主要是有些报文的请求资源会带有http://
    if (strncasecmp(url_, "http://", 7) == 0)
    {
        url_ += 7;
        // 找到后面第一个/
        url_ = strchr(url_, '/');
    }

    // https
    if (strncasecmp(url_, "https://", 8) == 0)
    {
        url_ += 8;
        // 找到后面第一个/
        url_ = strchr(url_, '/');
    }

    LOG_INFO("URL = %s \n", url_);

    // 一般的不会有上述两种符号， 直接是单独的/或后面带访问资源
    if (!url_ || url_[0] != '/')
    {
        return BAD_REQUEST;
    }

    // url为/时，显示欢迎界面
    if (strlen(url_) == 1)
    {
        strcat(url_, "judge.html");
    }

    check_state_ = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 主状态机解析报文中的请求头数据
/**
 * 解析完请求行后，主状态机继续分析请求头
 * 请求头和空行的处理使用同一个函数，这里通过判断当前的text首位是不是\0字符，如是说明是空行
 * CHECK_STATE_HEADER
 * 判断是空行还是请求头，若是空行，进而判断content-length是否为0，不是0，为POST请求，转移到CHECK_STATE_CONTENT
 * 否则是GET请求，报文解析结束
 * 若不是空行，即解析的是请求头部字段，主要分析connection，content-length字段
 * connection: 判断是keep-alive还是close，是长连接还是短链接
 * content-length: 用于读取post请求的消息体长度
*/
HTTPServer::HTTP_CODE HTTPServer::parse_headers(char* text)
{
    LOG_INFO("Enter parse_headers \n");
    LOG_INFO("Enter LINE: %s \n", text);

    if(text[0] == '\0')
    {
        if (content_length_ != 0)
        {
            // POST需要跳转到消息体处理状态
            check_state_ = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        LOG_INFO("GET REQUEST PARSE COMPLETE CONTENT_LENGTH = %d!\n", content_length_);
        return GET_REQUEST;
    }

    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        LOG_INFO("Connection: %s\n", text);
        if (strcasecmp(text, "keep-alive") == 0)
        {
            linger_ = true;
        }
    }

    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        LOG_INFO("Content-length: %s\n", text);
        content_length_ = atol(text);
    }

    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        LOG_INFO("Host: %s\n", text);
        host_ = text;
    }

    else
    {
        LOG_INFO("OOP! UNKNOW HEADER: %s\n", text);
    }

    return NO_REQUEST;
}

// 主状态机解析报文中的请求内容
// 如果是GET请求上面就已经解析完成了
/**
 * 后续的登录和注册功能，为了避免将用户名和密码直接暴露在URL中，使用POST请求将用户名和密码在报文中封装
 * POST请求报文中，消息体的末尾没有任何字符不会返回LINE_OK，因此不能使用从状态机的状态
 * 因此使用主状态机的状态作为循环入口条件
 * check_state == CHECK_STATE_CONTENT && line_status == LINE_OK 是因为解析完消息体后，整个报文都解析完成了，但此时主状态机状态还是CHECK_STATE_CONTENT
 * 如果不设置line_status = LINE_OPEN就会再次进入循环
 
 * CHECK_STATE_CONTENT:
 * 仅用于解析POST请求
 * 用于保存post请求消息体，为登录和注册功能做准备 
*/
HTTPServer::HTTP_CODE HTTPServer::parse_content(char* text)
{
    LOG_INFO("Enter parse_content \n");
    LOG_INFO("Enter LINE: %s \n", text);

    // 判断是否读取了消息体
    if (read_idx_ >= (content_length_ + checked_idx_))
    {
        text[content_length_] = '\0';
        message_ = text;

        return GET_REQUEST;
    }

    return NO_REQUEST;
}

// 生成响应报文
/**
 * process_read函数的返回值是对请求的文件分析后的结果
 * 一部分是语法错误导致的BAD_REQUEST
 * 一部分是do_request的返回结果，将网站根目录和url文件拼接，然后通过stat判断该文件属性
 * 为了提高访问速度，通过mmap进行映射，将普通文件映射到内存逻辑地址
 * url_为请求报文中解析出的请求资源，以/开头，也就是/xxx, 项目中解析的url_有8中情况
 * ./ GET请求 跳转到judge.html
 * /0 POST请求，跳转到log.html 注册
 * /1 POST请求，跳转到log.html 登录功能
 * /2 CGISQL.cgi POST请求进行登录校验 成功跳转welcome.html 失败跳转logError.html
 * /3 CGISQL.cgi POST请求进行注册校验，成功跳转log.html登陆页面，失败跳转registerError.html
 * /5 POST请求跳转到picture.html 图片请求
 * /6 POST请求跳转到video.html 视频请求
 * /7 POST请求跳转到fans.html 关注页面
*/
const char* doc_root = "/home/zy/Project/MyMuduo/root";
HTTPServer::HTTP_CODE HTTPServer::do_request()
{
    LOG_INFO("Enter do_request URL: %s\n", url_);
    // 将初始化的real_file_ 
    strcpy(real_file_, doc_root);
    int len = strlen(doc_root);

    // 找到url_中的/的位置
    const char* p = strrchr(url_, '/');
    // 实现登录和注册校验
    if (cgi_ == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        // 根据标志判断是登录检测还是注册检测

        // 同步线程登录校验

        // CGI多线程登录校验
    }

    // 请求资源为/0, 表示跳转注册界面
    if(*(p + 1) == '0')
    {
        char* url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(url_real, "/register.html");

        // 将网站目录和/register.html进行拼接， 更新到real_file_
        strncpy(real_file_+len, url_real, strlen(url_real));
        free(url_real);
    }

    // 请求资源为/1，表示跳转登陆界面
    else if (*(p + 1) == '1')
    {
        char* url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(url_real, "/log.html");
        strncpy(real_file_+len, url_real, strlen(url_real));
        free(url_real);       
    }
    else
    {
        // 既不是登录和注册，直接将url与网站目录拼接
        strncpy(real_file_+len, url_, FILENAME_LEN-len-1);
        LOG_INFO("REQUEST URL: %s \n", real_file_);
    }
    LOG_INFO("real_file: %s\n", real_file_);
    // 通过stat获取请求资源文件信息，成功则将信息更新到file_stat结构体
    // 失败返回NO_RESOURCE状态，表示资源不存在
    if(stat(real_file_, &file_stat_) < 0)
    {   
        LOG_INFO("ENTER stat(real_file_, &file_stat_) < 0, errno = %d\n",errno);
        return NO_RESOURCE;
    }

    // 判断文件的权限，是否可读，不可读返回FORBIDDEN_REQUEST状态
    if (!(file_stat_.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;
    
    if (S_ISDIR(file_stat_.st_mode))
        return BAD_REQUEST;

    // 以只读方式获取文件描述符，通过mmap将该文件映射到内存中
    int fd = open(real_file_, O_RDONLY);
    file_address_ = (char*)mmap(0, file_stat_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    LOG_INFO("file address: %s\n", file_address_);

    close(fd);

    return FILE_REQUEST;
}

// 写入响应报文
/**
 * process_write
 * 根据do_request的返回程度，向write_buf中写入响应报文
 * add_status_line: 添加状态行: HTTP:/1.1 状态码 状态信息
 * add_headers: 添加消息报头，内部调用add_content_length 和 add_linger函数
 * content-length：响应报文长度，用于浏览器判断服务器是否发送完数据
 * connection记录连接状态，告诉浏览器端保持长连接
 * add_blank_line添加空行
*/
bool HTTPServer::process_write(HTTP_CODE ret)
{
    LOG_INFO("Enter PROCESS WRITE \n");
    LOG_INFO("file_address_: %s \n", file_address_);
    switch(ret)
    {
        // 500
        case INTERNAL_ERROR: 
        {
            LOG_INFO("process_write INTERNAL_ERROR\n");
            // 状态行
            add_status_line(500, error_500_title);
            // 消息报头
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form))
            {
                return false;
            }
            break;
        }
        // 404
        case BAD_REQUEST:
         {
            LOG_INFO("process_write BAD_REQUEST\n");
            // 状态行
            add_status_line(404, error_404_title);
            // 消息报头
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form))
            {
                return false;
            }
            break;
        }
        // 403 
        case FORBIDDEN_REQUEST:
        {
            LOG_INFO("process_write FORBIDDEN_REQUEST\n");

            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form))
            {
                return false;
            }
            break;
        }
        // 200
        case FILE_REQUEST:
        {
            LOG_INFO("process_write FILE_REQUEST\n");

            add_status_line(200, ok_200_title);
            if (file_stat_.st_size != 0)
            {
                add_headers(file_stat_.st_size);
                // 发送的全部数据为响应报文头部信息和文件大小
                bytes_to_send_ = write_idx_ + file_stat_.st_size;
            }
            else
            {
                // 请求的资源大小为0，返回空白html文件
                const char* ok_string = "<html><body></body><html>";
                add_headers(strlen(ok_string));
                if  (!add_content(ok_string))
                {
                    return false;
                }
            }
            break;
        }
        default:
        {
            LOG_INFO("FILE_REQUEST: %d, FORBIDDEN_REQUEST: %d, BAD_REQUEST: %d, INTERNAL_ERROR: %d \n",
                        FILE_REQUEST, FORBIDDEN_REQUEST, BAD_REQUEST, INTERNAL_ERROR);
            LOG_INFO("real state is %d \n", ret);                        
            LOG_INFO("process_write FALSE!\n");      
            return false;      
        }
    }

    return true;
}


// 根据响应报文格式，生成对应8个部分
bool HTTPServer::add_response(const char* format, ...)
{
    // 如果写入内容超出write_buf容量
    if(write_idx_ >= WRITE_BUFFER_SIZE)
    {
        return false;
    }

    // 可变参数列表
    va_list arg_list;
    // 将变量arg_list初始化为传入参数
    va_start(arg_list, format);

    // 将数据从可变参数列表写入write_buf
    int len = vsnprintf(writeBuf_+write_idx_, WRITE_BUFFER_SIZE-1-write_idx_, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE - 1 - write_idx_))
    {
        va_end(arg_list);
        return false;
    }

    write_idx_ += len;
    va_end(arg_list); // 清空可变参数列表

    return true;
}

bool HTTPServer::add_content(const char* content)
{
    return add_response("%s", content);
}

// 添加状态行
bool HTTPServer::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

void HTTPServer::add_headers(int content_length)
{
    add_content_length(content_length);
    add_linger();
    add_blank_line();
}

bool HTTPServer::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool HTTPServer::add_content_length(int content_length)
{
    return add_response("Content-Length:%d\r\n", content_length);
}

bool HTTPServer::add_linger()
{
    return add_response("Connection:%s\r\n", (linger_==true)?"keep-alive" : "close");
}

bool HTTPServer::add_blank_line()
{
    return add_response("%s", "\r\n");
}

HTTPServer::~HTTPServer()
{
    LOG_INFO("~HTTPServer..");
}
