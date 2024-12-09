#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

// 线程同步机制封装类

// 互斥锁类
class locker {
public:
    locker() {
        if(pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }        
    }

    ~locker() {
        pthread_mutex_destroy(&m_mutex);
    }

    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    pthread_mutex_t *get() {    /*直接返回指针是因为可以直达变量，确保动态内存分配和生存周期管理、方便共享和修改、返回指针更具有轻量性、一致性*/
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;    // 互斥锁，设为私有变量
};

// 条件变量类
class cond {
public:
    cond() {
        if(pthread_cond_init(&m_cond, NULL) != 0) {
            throw std::exception();
        }
    }

    ~cond() {
        pthread_cond_destroy(&m_cond);
    }

    bool wait(pthread_mutex_t *m_mutex) {   // 代表到时候要传入的是指针
        int ret = 0;
        ret = pthread_cond_wait(&m_cond, m_mutex);  // 暂停线程，等待唤醒从暂停处继续执行，常用于生产消费者模型，见lession30 pthread_cond.c
        return ret == 0;
    }
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t) {
        int ret = 0;
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t); // 因为m_mutex传入参数定义就是指针
        return ret == 0;
    }
    bool signal() {
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast() {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    pthread_cond_t m_cond;
};

// 信号量类
class sem {
public:
    sem() {
        if(sem_init(&m_sem, 0, 0) != 0) {
            throw std::exception();
        }
    }
    sem(int num) {
        if(sem_init(&m_sem, 0, num) != 0) {
            throw std::exception();
        }
    }
    ~sem() {
        sem_destroy(&m_sem);
    }

    // 等待信号量，信号量-1
    bool wait() {
        return sem_wait(&m_sem) == 0;
    }

    // 增加信号量
    bool post() {
        return sem_post(&m_sem) == 0;
    }


private:
sem_t m_sem; 
};


/*这里还要理解互斥锁、条件变量、信号量的机制
    互斥锁mutex：只有持锁的线程才能运行，保证单一进程访问共享空间
    条件变量cond：辅助locker，常用于生产消费者模型，wait暂停当前线程，直到signal、broadcast通知，才继续从wait暂停的地方继续
    信号量sem：是一个数val，post令val+1，wait令val-1，当val==0，wait会阻塞
2.注意：if和return返回都是判断！=0或者==0，而不仅仅是直接返回函数，令其执行
*/


#endif