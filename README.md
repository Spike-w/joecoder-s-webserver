# joecoder-s-webserver
继数据结构与算法、操作系统OS、计算机网络、多进程、多线程、网络编程等之后，基于Linux使用C++编写的简单高并发webserver。
![9df58b25ccbf9a817e492b2c911e18c](https://github.com/user-attachments/assets/7d8fadfa-5904-43fb-b3b6-3d70b2e86516)

采用TCP流式socket通讯、仿Proactor模式、利用epoll、水平触发且设置EPOLLONESHOT事件、定时非活跃检测( 未集成到main )等方法和技术实现基本的webserver，
借助webbench进行服务器压力测试，可接受约近万个( 9000+ )客户端的并发访问。

![1cca757c5ae850b907101403ac787d9a](https://github.com/user-attachments/assets/4a20073c-3ce9-448b-be81-7059d7ce949e)



**********************************************************
4.22 鹅厂问到有没有进行压测和实时观测，分析可能发生原因，遂补充

运行情况：
![51ef53e9424c6a616f7065378fab9659](https://github.com/user-attachments/assets/1332c4e4-8d0e-4cdc-ba0b-2bf9eb51c732)

采用top指令进行实时观测：
1、可以看到长连接下，时间过长+大量链接，导致cpu占用率过高，此原因1（但此时1000并发连接是完全没问题的）
![image](https://github.com/user-attachments/assets/2c791a24-a1e1-4670-a85d-9dbde291c175)

2、改到10000并发，erro
![image](https://github.com/user-attachments/assets/81609ec4-af44-40b3-8b30-b95db76100ff)

可能原因：
OS方面：文件描述符限制、内存不足、进程数限制
根据ulimit -n查看可以打开的fd数，其实还ok，但是比较接近了，虽然是多线程共享资源，如果多个线程的连接只使用 1 个文件描述符，操作系统就无法区分各个线程的请求和状态，导致网络通信和文件访问管理混乱。需要注意的是epoll进行端口复用，虽然可以注册到同一个epoll实例上进行管理，但是每个连接在底层建立时依然会占用一个文件描述符，epoll 只是提供了一种高效的方式来监控这些文件描述符上的事件，而不是改变每个连接对应一个文件描述符的本质。
根据ulimit -u查看可以创建的pid数，其实也还ok
有一部分硬件限制原因（上一个硬件可能限制更大，由于其他项目的需要，我对电脑进行了升级，提高了性能）
![image](https://github.com/user-attachments/assets/37d4de96-3945-49c1-9d55-6d630e7b55ba)

3、具体原因就很明显了，打开代码可以看到，代码中对epoll监听的fd数量进行了限制&&对list任务队列的max_request进行了限制



