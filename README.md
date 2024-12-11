# joecoder-s-webserver
继数据结构与算法、操作系统OS、计算机网络、多进程、多线程、网络编程等之后，基于Linux使用C++编写的简单高并发webserver。
![9df58b25ccbf9a817e492b2c911e18c](https://github.com/user-attachments/assets/7d8fadfa-5904-43fb-b3b6-3d70b2e86516)

采用TCP流式socket通讯、仿Proactor模式、利用epoll、水平触发且设置EPOLLONESHOT事件、定时非活跃检测( 未集成到main )等方法和技术实现基本的webserver，
借助webbench进行服务器压力测试，可接受约近万个( 9000+ )客户端的并发访问。

![1cca757c5ae850b907101403ac787d9a](https://github.com/user-attachments/assets/4a20073c-3ce9-448b-be81-7059d7ce949e)

