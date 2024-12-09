#include <sys/socket.h>
#include <netinet/in.h>     // 这个是干嘛的
#include <arpa/inet.h>
// #include <cstdio>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/epoll.h>
// #include <signal.h>     // sigaction
// #include <assert.h>     // assert
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"

#define MAX_FD 65536    // 最大的文件描述符的个数
#define MAX_EVENT_NUMBER 10000      // 监听的最大的事件数量

extern void addfd(int epollfd, int fd, bool one_shot );
extern void removefd(int epollfd, int fd);

/*米奇妙妙屋void (handler)(int)，表示传入参数为一个第二个参数void( handler )(int)表示一个函数指针。具体来说，void( handler )(int)这个函数指针的意思是指向一个没有返回值（void）且接受一个int类型参数的函数。*/
/*想理解？因为后面要把他给sa.sa_handler = handler，不断点开发现sa_handelr就是个
Type of a signal handler. 
typedef void (*__sighandler_t) (int);*/
void addsig(int sig, void( handler )(int)){
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );  /*作用是将结构体sa的内存空间用\0填充，即用零值初始化。即sa的每个字节都设为0*/
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);    /*将所有位置1，表示该信号集阻塞所有信号，因为我不知道参数sig要传个什么进来*/
    assert( sigaction(sig, &sa, NULL) != -1 ); // assert如果内部为1，正常，内部为0，assert宏将产生一个诊断信息，并终止程序执行if throw exception的集成版
}


int main(int argc, char *argv[]) {  // argc获取参数(或者说argv数组中)的个数，argv[]真正的参数
    if( argc <= 1 ) {
        printf("usage: %s port_number\n", basename(argv[0]));   /*basename函数获取程序的路径*/
        return 1;
    }

    int port = atoi( argv[1] ); /*atoi将字符串转化为整数，ASCII to int*/
    addsig( SIGPIPE, SIG_IGN );

    threadpool< http_conn >* pool = NULL;
    try {
        pool = new threadpool<http_conn>;
    } catch( ... ) {
        return 1;
    }   /*异常捕获*/

http_conn * users = new http_conn[ MAX_FD ]; // 开辟存放任务http_conn号的空间

    int listenfd = socket(PF_INET, SOCK_STREAM, 0); // 见第4章P10套接字函数

    int ret = 0;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons( port );


    // 在绑定之前要设置端口复用
    int reuse = 1;
    setsockopt( listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    ret = listen(listenfd, 5);

    // 创建epoll对象，和事件数组，添加
    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 ); // >0就行
    // 添加到epoll对象中
    addfd( epollfd, listenfd, false );
    http_conn::m_epollfd = epollfd;

    while(true) {
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 ); //-1阻塞，epoll_create, epoll_ctl, epoll_wait三部曲  // ET模式下试试非阻塞

        if( ( number < 0 ) && ( errno != EINTR ) ) {
            printf("epoll failure\n");
            break;
        }

        for(int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;

            if ( sockfd == listenfd ) {
                
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                // printf("connfd = %d\n", connfd);                    
                if(connfd < 0) {
                    printf("errno is: %d\n", errno );
                    continue;
                }

                if( http_conn::m_user_count >= MAX_FD) {
                    close(connfd);  //量超了，当前这个不能要了
                    continue;
                }
                users[connfd].init(connfd, client_address); /*这里直接用connfd作为索引，而不是i，因为i只是第几个事件，并不能指向到对应任务*/

            } else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
               
                users[sockfd].close_conn();

            } else if(events[i].events & EPOLLIN) {

                if(users[sockfd].read()) {
                    pool->append(users + sockfd);   /*users是http_conn任务，pool是线程池，这一步是将该任务添加到pool的任务队列中*/
                } else {    // 这里把read返回值判断做了极简化
                    users[sockfd].close_conn();
                }

            } else if(events[i].events & EPOLLOUT) {
                if( !users[sockfd].write() ) {
                    users[sockfd].close_conn();
                }
            }
        }

    }

    /*这里，几个关键点就可以看的很清晰了*/
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;


    return 0;
}