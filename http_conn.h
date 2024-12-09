#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <stdio.h>
#include <cstdio>
#include <stdlib.h>
#include <sys/epoll.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>   // memory mapping内存映射的函数库，如memset，memcpy
#include <stdarg.h>     // 于支持可变参数函数（variadic functions）。可变参数函数是指可以接受不定数量参数的函数，例如printf和scanf等函数。
#include <errno.h>
#include "locker.h"
#include <sys/uio.h>    // 通常用于进行多个非连续内存区域的输入输出操作。如readv、writev


class http_conn {
public:
    static const int FILENAME_LEN = 200;    // 文件名的最大长度
    static const int READ_BUFFER_SIZE = 2048;   // 读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024;  // 写缓冲区大小

public:
    // 暂且可以将enum理解为定义一个变量，该变量可以有多种状态
    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};

    /* 解析客户端的请求时，主状态机的状态：
        - CHECK_STATE_REQUESTLINE: 当前正在分析请求行
        - CHECK_STSTE_HEADER: 当前正在分析头部字段
        - CHECK_STATE_CONTENT: 当前正在解析请求体
    */
   enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};

    /* 服务器处理HTTP请求可能得结果，报文解析的结果：
        - NO_REQUEST            : 请求不完整，需要继续读取客户数据
        - GET_REQUEST           : 表示获得了一个完成的客户请求
        - BAD_REQUEST           : 表示客户请求语法错误
        - NO_RESOURCE           : 表示服务器没有资源
        - FORBIDDEN_REQUEST     : 表示客户对资源没有足够的访问权限
        - FILE_REQUEST          : 文件请求，获取文件成功
        - INTERNAL_ERROR        : 表示服务器内部错误
        - CLOSED_CONNECTION     : 表示客户端已经关闭连接了
    */
   enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};

   // 从状态机的三种可能得状态，即 行的读取状态，分别表示
   // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
   enum LINE_STATUS {LINE_OK = 0, LINE_BAD, LINE_OPEN};

public:
    http_conn(){}
    ~http_conn(){}

    void init(int sockfd, const sockaddr_in & addr);    // 初始化新接受的连接，外部调用初始化套接字地址
    void close_conn();  // 关闭连接
    void process(); // 实际处理客户端的请求，也是在线程池work->run->process中的处理函数
    bool read(); // 非阻塞读
    bool write(); //非阻塞写

private:
    void init();    // 内部初始化链接
    HTTP_CODE process_read();   // 解析http请求
    bool process_write( HTTP_CODE ret );    // 填充HTTP应答

    // 下面这一组函数被process_read调用以分析HTTP请求
    HTTP_CODE parse_request_line( char* text );
    HTTP_CODE parse_headers( char* text );
    HTTP_CODE parse_content( char* text );
    HTTP_CODE do_request();
    char* get_line() { return m_read_buf + m_start_line; }
    LINE_STATUS parse_line();

    // 下面这一组函数被process_write调用以分析HTTP请求
    void unmap();
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_content_type();
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();


public:
    static int m_epollfd;       // 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
    static int m_user_count;    // 统计用户的数量

private: 
    int m_sockfd;
    sockaddr_in m_address;

    char m_read_buf[ READ_BUFFER_SIZE ];    // 读缓冲区
    int m_read_idx;                         // 标识读缓冲区中已经读入的客户端数据的最后一个字节的下一个位置
    /*用于进行http头部各部分报文解析请求方法、URL、http版本、回车等*/
    int m_checked_idx;                      // 当前正在分析的字符在读缓冲区中的位置
    int m_start_line;                       // 当前正在解析的行的起始位置

    CHECK_STATE m_check_state;              // 主状态机当前所处的状态
    METHOD m_method;                        // 请求方法

    char m_real_file[ FILENAME_LEN ];       // 客户请求的目标文件的完整路径，其内容等于doc_root + m_url, doc_root是网站根目录
    char *m_url;                            // 客户请求的目标文件的文件名
    char *m_version;                        // HTTP协议版本号，我们仅支持HTTP1.1
    char *m_host;                           // 主机名
    int m_content_length;                   // HTTP请求的消息总长度
    bool m_linger;                          // HTTP请求是否要求保持链接

    char m_write_buf[ WRITE_BUFFER_SIZE ];  // 写缓冲区
    int m_write_idx;                        // 写缓冲区中待发送的字节数
    char* m_file_address;                   // 客户请求的目标文件被mmap到内存中的起始位置，就是要请求的文件的位置
    struct stat m_file_stat;                // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    struct iovec m_iv[2];                   // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量
    int m_iv_count;
};



#endif