# joecoder-s-webserver
继数据结构与算法、操作系统OS、计算机网络、多进程、多线程、网络编程等之后，基于Linux使用C++编写的简单高并发webserver。
采用TCP流式socket通讯、仿Proactor模式、利用epoll、水平触发且设置EPOLLONESHOT事件、定时非活跃检测(未集成)等方法和技术实现基本的webserver，
借助webbench进行服务器压力测试，可接受约9000线程并发访问。
