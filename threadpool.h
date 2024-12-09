#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <pthread.h>
#include <exception>
#include <cstdio>   // stdio.h的cpp版本cstdio
#include "locker.h"

// 线程池类，将它定义为模板类，可用于代码复用，参数T是任务类
template<typename T>
class threadpool {
public:
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    bool append(T* request);    /*添加到请求队列中，上锁，防止共享内存出错，*/

private:
    static void *worker(void *arg);      /*worker是线程的handler，要对线程进行怎样的处理*/
    void run();      /*实际的处理函数，在worker内部调用run，进行处理*/


private:    /*先来看看相关变量*/

    // 线程的数量
    int m_thread_number;
    // 线程池的数组，大小记录在m_thread_number
    pthread_t * m_threads;


    // 请求队列中最多允许的、等待处理的数量
    int m_max_requests;
    // 请求队列，因为线程池是正在处理的，请求队列是等待处理的
    std::list< T* > m_workqueue;    /*队列，存储了一系列T类型的等待线程处理任务*/

    // 保护-请求队列-的互斥锁
    locker m_queuelocker;

    // 信号量，是否有任务需要处理，信号量一般就用于这种“资源池管理”，控制资源量，如线程池和连接池
    sem m_queuestat;

    // 是否结束线程
    bool m_stop;
};

/*template<typename T>是声明这是一个模板类的成员函数定义，threadpool<T>::表示这是threadpool类内的函数*/
template < typename T >
threadpool< T >::threadpool(int thread_number, int max_requests) :
        m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(NULL) {
    
    if((thread_number <= 0 )|| (max_requests <= 0)) {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];
    if(!m_threads) {
        throw std::exception();
    }

    // 创建thread_number个线程，并将他们设置为脱离线程
    for(int i = 0; i < thread_number; i++) {
        printf("creat the %dth thread\n", i);
        // 创建失败
        if(pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete [] m_threads;
            printf("线程池创建失败\n");
            throw std::exception();
        }
        // 创建成功，设置线程分离
        if( pthread_detach( m_threads[i] ) ) {      // 因为pthread_detach的成功返回0，失败是返回errno而非-1，因此直接判断
            delete [] m_threads;            /*注意：这里并不是成功分离后运行，delete [] m_threads，而是失败才进入该句*/
            throw std::exception();
        }
    }

}

template <typename T>
threadpool< T >::~threadpool() {
    delete [] m_threads;
    m_stop = true;
}

// 将任务添加到任务队列中
template <typename T>
bool threadpool< T >::append( T* request )  /*操作：1上锁；2将request加入工作队列；3信号通知可以处理*/
{
    // 操作工作队列时一定要加锁，因为它被所有线程共享。
    m_queuelocker.lock();   /*所以在pool线程池中也包含了成员 locker，互斥锁*/
    if ( m_workqueue.size() > m_max_requests ) {    /*在构造函数threadpool中，m_max_requests被初始化为max_requests = 10000*/
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);     /*list中的自带的函数push_back*/
    m_queuelocker.unlock();
    m_queuestat.post();     /*信号，sem_post +1(increments  (unlocks)  the  semaphore pointed to by sem)sem > 0通知可以处理，sem_wait-1*/
    return true;
}

template <typename T>
void * threadpool< T >::worker(void * arg) {
    threadpool *pool = (threadpool *) arg;  /*因为在构造函数中pthread_create中传给worker的参数是this*/
    pool->run();
    return pool;
}

template<typename T>
void threadpool< T >::run() {
    while(!m_stop) {
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty()) {
            // 任务队列为空，全部执行完了
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();   // 将任务request从队列中取出
        m_workqueue.pop_front();    // 取出就可以删了
        m_queuelocker.unlock();
        if( !request ) {
            continue;
        }
        request->process(); /*request中，也就是T*中，也就是后面指定的http_conn中*/
    }
}

#endif