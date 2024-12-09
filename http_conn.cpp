#include "http_conn.h"
#include <strings.h>

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You don't have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The request file is not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the request file.\n";

// 网站的根目录
const char* doc_root = "/home/joecoder/webserver/resources";

int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);    // 获取flag
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return new_option;  // 原代码是return old_option
}

// 向epoll中添加需要监听的文件描述符, 也是在main中引用的
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;    // | EPOLLET;
    if( one_shot ) {
        // oneshot，EPOLLONESHOT让事件只能被触发一次，想要再次触发必须在触发之后调用epoll_ctl函数重新写入EPOLLONESHOT
        event.events |= EPOLLONESHOT;   // 防止同一个通信被不同的线程处理
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // 设置文件描述符非阻塞
    setnonblocking(fd);
}

// 从epoll中移除监听的文件描述符
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改文件描述符，重置socket上的EPOLLONESHOT事件，确保下一次可读时EPOLLIN事件能被触发
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;    /*错误1*/
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

/*前面的都是要不然main中药extern的，要不然就是下面要调用的，不是在http_conn中定义过的*/
// 初始化参数，后面init
// 所有的客户数
int http_conn::m_user_count = 0;
// 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
int http_conn::m_epollfd = -1;

// 关闭连接
void http_conn::close_conn() {  // 所以只是关闭一个连接，把fd从epoll中移除
    if(m_sockfd != -1) {
        removefd( m_epollfd, m_sockfd );
        m_sockfd = -1;
        m_user_count--;
    }
}

/*所以说，每次一个新的任务产生都init，但是都放在一个epoll中*/
// 初始化连接，外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr) {
    m_sockfd = sockfd;
    m_address = addr;

    // 端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    addfd(m_epollfd, sockfd, true);
    m_user_count++; /*所以说，每次一个新的任务产生都init，但是都放在一个epoll中*/
    init();
}

void http_conn::init() {
    m_check_state = CHECK_STATE_REQUESTLINE;    // 状态设为检查请求行
    m_linger = false;   // 默认不保持连接，Connection : keep_alive是设置保持连接

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    bzero(m_read_buf, READ_BUFFER_SIZE);    // 参数1是要置零的，参数2是大小
    bzero(m_write_buf, WRITE_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}

// 循环读取客户数据，直到无数据可读或者对方关闭连接
bool http_conn::read() {
    if( m_read_idx >= READ_BUFFER_SIZE ) {
        return false;
    }
    int bytes_read = 0;
    while(true) {
        // 从m_read_buf + m_read_idx索引出开始保存数据，大小是read_buffer_size - m_read_idx
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, 
        READ_BUFFER_SIZE - m_read_idx, 0);  /*剩余需要读取的数据长度len = read_buffer_size - m_read_idx*/

        // printf("读取到的数据长度bytes_read = %d, 缓冲区初始位置m_read_buf = %s,目前索引/已读取 m_read_idx = %d\n", bytes_read, m_read_buf, m_read_idx);

        if(bytes_read == -1) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                // 没有数据
                break;
            }
            return false;
        } else if(bytes_read == 0) {
            // 对方关闭连接
            return false;
        }
        m_read_idx += bytes_read;
    }

    return true;
}


// 解析一行，判断依据\r\n
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for( ; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];
        if( temp == '\r' ) {
            if( (m_checked_idx + 1) == m_read_idx ) {
                return LINE_OPEN;   // 数据行尚不完整
            } else if ( m_read_buf[m_checked_idx + 1] == '\n' ) {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if( temp == '\n' ) {
            if( (m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r') ) {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';     /*注意是当前位置置零，然后再++*/
                return LINE_OK;
            }
            return LINE_BAD;
        }

    }
    return LINE_OPEN;
}
/*将\r\n替换为\0的操作通常是为了方便对字符串进行处理，特别是在C/C++中处理以null结尾的字符串（null-terminated strings）时。这种操作通常用于处理HTTP报文的解析，其中\r\n表示行结束，而\0表示字符串的结束。*/

// 解析HTTP请求行，获得请求方法、目标URL、以及HTTP版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t");    // 判断\t在字符串txt中出现的位置，返回给m_url，失败返回NULL
    if( !m_url ) {
        return BAD_REQUEST; // 客户请求错误
    }

    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';    /*1.将m_url位置内容置为\0（也就是\t），断开字符串，让m_method取用;2.m_url++*/ 

    char* method = text;    // 现在的text已经被断开，text指向的内容就是获取方法
    if( strcasecmp(method, "GET") == 0 ) {  //忽略大小写比较,string.h中的函数
        m_method = GET;
    } else {
        return BAD_REQUEST;
    }

    // /index.html HTTP/1.1  =  m_url
    m_version = strpbrk(m_url, " \t");
    if( !m_version ) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';    /*代代更替，现在用m_version作为下一个索引了，\r\n蛟不敢忘\r\n*/
    if( strcasecmp(m_version, "HTTP/1.1") != 0 ) {
        return BAD_REQUEST;
    }

    /*    
     * http://192.168.121.130.:10000/index.html     
    */
    if(strncasecmp(m_url, "http://", 7) == 0 ) {    // 函数略有差别n
        m_url += 7;
        m_url = strchr(m_url, '/'); // 在采纳数str所指向的字符串中搜索第一次出现字符c(一个无符号字符)的位置。
    }
    /*后面m_url在do_request中还要用到，所以这里停在/index.com的前一个位置*/
    if( !m_url || m_url[0] != '/' ) {
        return BAD_REQUEST;
    }
    m_check_state = CHECK_STATE_HEADER;   // 在process_read中先switch CHECK_STATE_REQUESTLINE,执行完当前内容后，指向后面
    return NO_REQUEST;  // requestline已完成
}

// 解析HTTP请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char* text) {
    // 遇到空行，表示头部文字解析完毕
    if( text[0] == '\0' ) {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体
        // 状态机转到CHECK_STATE_CONTENT状态
        if( m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明已经得到一个完整的HTTP请求,但这种情况下，完整的消息体只有request_line
        return GET_REQUEST;
    } 
    /*对比是头部的那一部分内容，遇到相应参数保存，同时考虑到Connection:后面的\t部分*/
    else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn( text, " \t" );  /*返回有几个 \t，并加到text上，也就是跳过这些空格，到达实际内容keep-alive*/
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            m_linger = true;
        }
    } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol(text);  // 也就是Content-Length:    后面的参数
    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        // 处理Host头部字段
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    } else {
        printf( "oop! unknow header %s\n", text );
    }
    return NO_REQUEST;
}

// 解析HTTP请求内容，实际并没有真正解析HTTP请求的消息体，只是判断它是否被完整的读入了
http_conn::HTTP_CODE http_conn::parse_content(char* text) {
    if( m_read_idx >= (m_content_length + m_checked_idx) ) {
        text[ m_content_length ] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 主状态机，解析请求，会调用上面的分析代码
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    while(( (m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK) ) 
    || ((line_status = parse_line()) == LINE_OK ) ) {
        // 获取一行数据
        text = get_line();
        m_start_line = m_checked_idx;
        printf("got 1 http line: %s\n", text);

        switch( m_check_state ) {       /*默认是CHECK_STATE_REQUESTLINE所以从头开始执行，然后再各函数中，将状态设为指向下一部分*/
            case CHECK_STATE_REQUESTLINE : {
                ret = parse_request_line( text );
                if ( ret == BAD_REQUEST ) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER : {
                ret = parse_headers( text );
                if ( ret == BAD_REQUEST ) {
                    return BAD_REQUEST;
                } else if ( ret == GET_REQUEST ) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT : {
                ret = parse_content( text );
                if( ret == GET_REQUEST ) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default : {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

/*
    当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
    如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
    映射到内存地址m_file_address处，并告诉调用者获取文件成功
*/
http_conn::HTTP_CODE http_conn::do_request() {
    // "/home/joecoder/webserver/resources"
    strcpy( m_real_file, doc_root );
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);   // 将url(=index.html)拷贝到路径后面，不确定长度就用预先规定的最大长度减去
    
    // 获取m_real_file文件的相关的状态信息给m_file_stat(包括大小、权限、拥有者、最后访问/修改时间等)，-1失败，0成功
    if( stat(m_real_file, &m_file_stat) < 0 ) {     
        return NO_RESOURCE;
    }

    // 判断访问权限，是否允许
    if( !(m_file_stat.st_mode & S_IROTH) ) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录，而不是文件
    if( S_ISDIR( m_file_stat.st_mode ) ) {
        return BAD_REQUEST;
    }

    // 如果是文件，且访问权限足够，以只读方式打开文件
    int fd = open( m_real_file, O_RDONLY );
    // 创建内存映射    使用了 mmap 函数来将一个文件映射到内存中，允许程序直接访问文件内容而不需要显式地进行读取操作。
    m_file_address = (char*) mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close( fd );
    return FILE_REQUEST;
}

/*munmap 函数用于释放通过 mmap 函数映射的内存区域。当不再需要某块内存区域时，可以使用 munmap 函数将该内存区域从进程的地址空间中解除映射，使得该内存区域可以被系统回收。*/
// 对内存映射区执行munmap操作，在write中调用
void http_conn::unmap() {
     if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = 0;
    }
}

// 写HTTP响应
bool http_conn::write() {
    int temp = 0;
    int bytes_have_send = 0;    // 已经发送的字节
    int bytes_to_send = m_write_idx;    // 将要发送的字节m_write_idx写缓冲区中待发送的字节

    if( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束
        modfd( m_epollfd, m_sockfd, EPOLLIN );  /*修改文件描述符，重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发*/
        init();
        return true;
    }
    
    while(1) {
        /*writev 函数可以将多个缓冲区的数据一次性写入文件描述符，避免了多次调用 write 函数的开销，并且能够更高效地处理数据传输。*/
        temp = writev(m_sockfd, m_iv, m_iv_count);  // 返回值为实际写入的字节数
        if(temp <= -1) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if(errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if( bytes_to_send <= bytes_have_send ) {
            // 已发送的位置超过需要发送的位置，说明全部发送，响应HTTP成功
            // 根据HTTP请求中的Connection字段决定是否立即关闭连接
            unmap();
            if(m_linger) {
                // 保持连接，等待下次联系
                init();
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return true;
            } else {
                // 说明要关闭连接
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return false;
            }
        }
    }
}

// 往写缓冲区中写入待发送的数据，也就是吧format的参数传给m_write_buf
bool http_conn::add_response( const char *format, ... ) {
    if( m_write_idx >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    /*下面这段代码主要是用来格式化字符串，并将其写入到管城区，同时处理了可变数量的参数*/
    va_list arg_list;   // 声明一个va_list 类型的变量arglist，用于存储可变数量的参数列表
    va_start( arg_list, format );   // 初始化arglist，format用于确定可变参数列表的起始位置。
    
    /*使用 vsnprintf 函数将格式化后的字符串写入到 m_write_buf + m_write_idx 的位置，并返回写入的字符数*/
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1- m_write_idx, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ) {
        return false;
    }
    m_write_idx += len; // 更新写入索引，将写入的字符数加到写入索引上
    va_end( arg_list ); // 结束对可变参数列表的访问，和va_start一同使用
    return true;
}

bool http_conn::add_status_line( int status, const char* title ) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_linger() {
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line() {
    return add_response( "%s", "\r\n" );
}

bool http_conn::add_headers(int content_len) {
    add_content_length( content_len );
    add_content_type();
    add_linger();
    add_blank_line();
    return true;
}
// 对content的内容还是简单操作的
bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

// 根据服务器处理HTTP请求的结果也就是process_read的结果ret，决定返回给客户端的内容，可以看process中调用process_write的过程
bool http_conn::process_write( HTTP_CODE ret ) {
    switch (ret) {
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers( strlen( error_403_form ) );
            if( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);   // do_request()中调用stat返回的实际file的状态
            m_iv[ 0 ].iov_base = m_write_buf;   // 要用writev写入的第一部分的缓冲区指针
            m_iv[ 0 ].iov_len = m_write_idx;    // 写入的长度
            m_iv[ 1 ].iov_base = m_file_address;    
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            return true;
        default:
            return false;
    }

// 奇怪，为什么要进行这个的初始化呢？，，其他状态都是default
// 因为break，没走出if( ! add_content( error_404_form ) ) { return false;}
    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}


// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void http_conn::process() {
    // 解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if ( read_ret == NO_REQUEST ) {
        modfd( m_epollfd, m_sockfd, EPOLLIN );
        return;
    }

    // 生成响应也就是read返回值要求服务器有回应
    bool write_ret = process_write( read_ret );
    if ( !write_ret ) {
        close_conn();   // 有问题关闭连接
    }
    modfd( m_epollfd, m_sockfd, EPOLLOUT ); // 重新设置，保证下次fd能够被触发
}


