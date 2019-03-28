本文作为自己学习网络编程的总结笔记。打算分析一下主流服务器模式的优缺点，及适用场景，每种模型实现一个回射服务器。客户端用同一个版本，服务端针对每种模型编写对应的回射服务器。

本文所有代码放在：[https://github.com/oscarwin/multi-echo-server](https://user-gold-cdn.xitu.io/2019/3/22/169a5f43ccce37a7)

## 单进程迭代服务器

单进程迭代服务器是我接触网络编程编写的第一个服务器模型，虽然代码只有几行，但是每一个套接字编程的函数都涉及到大量的知识，这里我并不打算介绍每个套接字函数的功能，只给出一个套接字编程的基础流程图。


![](https://user-gold-cdn.xitu.io/2019/3/22/169a5b46d0e97ca7?w=952&h=794&f=png&s=52858)

有几点需要解释的是：

- 服务器调用listen函数以后，客户端与服务端的3次握手是由内核自己完成的，不需要应用程序的干预。内核为所有的连接维护两个个队列，队列的大小之和由listen函数的backlog参数决定。服务端收到客户算的SYN请求后，会回复一个SYN+ACK给客户端，并往未完成队列中插入一项。所以未完成队列中的连接都是SYN_RCVD状态的。当服务器收到客户端的ACK应答后，就将该连接从未完成队列转移到已完成队列。

- 当未完成队列和已完成队列满了后，服务器就会直接拒绝连接。常见的SYN洪水攻击，就是通过大量的SYN请求，占满了该队列，导致服务器拒绝其他正常请求达到攻击的目的。

- accept函数会一直阻塞，直到已完成队列不为空，然后从已完成队列中取出一个完成连接的套接字。

## 多进程并发服务器

单进程服务器只能同时处理一个连接。新建立的连接会一直呆在已完成队列里，得不到处理。因此，自然想到通过多进程来实现同时处理多个连接。为每一个连接产生一个进程去处理，称为PPC模式，即process per connection。其流程图如下(图片来自网络，侵删)：


![](https://user-gold-cdn.xitu.io/2019/3/22/169a5c3abf4f217c?w=505&h=475&f=png&s=72365)

这种模式下有几点需要注意：

- 统一由父进程来accept连接，然后fork子进程处理读写
- 父进程fork以后，立即关闭了连接套接字，而子进程则立即关闭了监听套接字。因为父进程只处理连接，子进程只处理读写。linux在fork了以后，子进程会继承父进程的文件描述符，父进程关闭连接套接字后，文件描述符的计数会减一，在子进程里并没有关闭，当子进程退出关闭连接套接字后，该文件描述符才被关闭

这种模式存在的问题：

- fork开销大。进程fork的开销太大，在fork时需要为子进程开辟新的进程空间，子进程还要从父进程那里继承许多的资源。尽管linux采用了写时复制技术，总的来看，开销还是很大
- 只能支持较少的连接。进程是操作系统重要的资源，每个进程都要分配独立的地址空间。在普遍的服务器上，该模式只能支持几百的连接。
- 进程间通信复杂。虽然linux有丰富的进程间通信方法，但是这些方法使用起来都有些复杂。

核心代码段如下，完整代码在ppc_server目录。
```c
    while(1)
    {
        clilen = sizeof(stCliAddr);
        if ((iConnectFd = accept(iListenFd, (struct sockaddr*)&stCliAddr, &clilen)) < 0)
        {
            perror("accept error");
            exit(EXIT_FAILURE);
        }

        // 子进程
        if ((childPid = fork()) == 0)
        {
            close(iListenFd);

            // 客户端主动关闭，发送FIN后，read返回0，结束循环
            while((n = read(iConnectFd, buf, BUFSIZE)) > 0)
            {
                printf("pid: %d recv: %s\n", getpid(), buf);
                fflush(stdout);
                if (write(iConnectFd, buf, n) < 0)
                {
                    perror("write error");
                    exit(EXIT_FAILURE);
                }
            }

            printf("child exit, pid: %d\n", getpid());
            fflush(stdout);
            exit(EXIT_SUCCESS);
        }
        // 父进程
        else
        {
            close(iConnectFd);
        }
    }
```
    

## 预先派生子进程服务器

既然fork进程时的开销比较大，因此很自然的一种优化方式是，在服务器启动的时候就预先派生子进程，即prefork。每个子进程自己进行accept，大概的流程图如下(图片来自网络，侵删)：

![](https://user-gold-cdn.xitu.io/2019/3/22/169a5fcf5c9b4717?w=1411&h=1396&f=png&s=187430)

相比于pcc模式，prefork在建立连接时的开销小了很多，但是另外两个问题——连接数有限和进程间通信复杂的问题还是存在。除此之外，prefork模式还引入了新的问题，当有一个新的连接到来时，虽然只有一个进程能够accept成功，但是所有的进程都被唤醒了，这个现象被称为惊群。惊群导致不必要的上下文切换和资源的调度，应该尽量避免。好在linux2.6版本以后，已经解决了惊群的问题。对于惊群的问题，也可以在应用程序中解决，在accept之前加锁，accept以后释放锁，这样就可以保证同一时间只有一个进程阻塞accept，从而避免惊群问题。进程间加锁的方式有很多，比如文件锁，信号量，互斥量等。

无锁版本的代码在prefork_server目录。加锁版本的代码在prefork_lock_server目录，使用的是进程间共享的线程锁。

## 多线程并发服务器

线程是一种轻量级的进程(linux实现上派生进程和线程都是调用do_fork函数来实现)，线程共享同一个进程的地址空间，因此创建线程时不需要像fork那样，拷贝父进程的资源，维护独立的地址空间，因此相比进程而言，多线程模型开销要小很多。多线程并发服务器模型与多进程并发服务器模型类似。

![](https://user-gold-cdn.xitu.io/2019/3/24/169afc488d048efe?w=497&h=395&f=png&s=59486)

多线程并发服务器模型，与多进程并发服务器模型相比，开销小了很多。但是同样存在连接数很有限这个限制。除此之外，多线程程序还引入了新的问题

- 多线程程序不如多进程程序稳定，一个线程崩溃可能导致整个进程崩溃，最终导致服务完全不可用。而多进程程序则不存在这样的问题
- 多进程程序共享了地址空间，省去了多进程程序之间复杂的通信方法。但是却需要对共享资源同时访问时进行加锁保护
- 创建线程的开销虽然比创建进程的开销小，但是整体来说还是有一些开销的。

## 预先派生线程服务器

和预先派生子进程相似，可以通过预先派生线程来消除创建线程的开销。

![](https://user-gold-cdn.xitu.io/2019/3/25/169b49230e159f85?w=1411&h=1295&f=png&s=191476)

预先派生线程的代码在pthread_server目录。

## reactor模式

前面提及的几种模式都没能解决的一个问题是——连接数有限。而IO多路复用就是用来解决海量连接数问题的，也就是所谓的C10K问题。

IO多路复用有三种实现方案，分别是select，poll和epoll，关于三者之间的区别就不在赘述，网络上已经有很多文章讲这个的了，比如这篇文章 [Linux IO模式及 select、poll、epoll详解](https://segmentfault.com/a/1190000003063859#articleHeader15)。

epoll因为其可以打开的文件描述符不像select那样受系统的限制，也不像poll那样需要在内核态和用户态之间拷贝event，因此性能最高，被广泛使用。

epoll有两种工作模式，一种是LT(level triggered)模式，一种是ET(edge triggered)模式。LT模式下通知可读，加入来了4k的数据，但是只读了2k，那么再次阻塞在epoll上时，还会再次通知。而ET模式下，如果只读了2k，再次阻塞在epoll上时，就不会通知。因此，ET模式下一次读就要把数据全部读完。因此，只能采用非阻塞IO，在while循环中读取这个IO，read或write返回EAGAIN。如果采用了非阻塞IO，read或write会一直阻塞，导致没有阻塞在epoll_wait上，IO多路复用就失效了。**非阻塞IO配合IO多路复用就是reactor模式**。reactor是核反应堆的意思，光是听这名字我就觉得牛不不要不要的了。

epoll编码的核心代码，我直接从man命令里的说明里拷贝过来了，我们的实现在目录reactor_server里。

```
#define MAX_EVENTS 10
struct epoll_event ev, events[MAX_EVENTS];
int listen_sock, conn_sock, nfds, epollfd;

/* Set up listening socket, 'listen_sock' (socket(),bind(), listen()) */

// 创建epoll句柄
epollfd = epoll_create(10);
if (epollfd == -1) {
   perror("epoll_create");
   exit(EXIT_FAILURE);
}

// 将监听套接字注册到epoll上
ev.events = EPOLLIN;
ev.data.fd = listen_sock;
if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev) == -1) {
   perror("epoll_ctl: listen_sock");
   exit(EXIT_FAILURE);
}

for (;;) {
    // 阻塞在epoll_wait
   nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
   if (nfds == -1) {
       perror("epoll_pwait");
       exit(EXIT_FAILURE);
   }

   for (n = 0; n < nfds; ++n) {
       if (events[n].data.fd == listen_sock) {
           conn_sock = accept(listen_sock, (struct sockaddr *) &local, &addrlen);
           if (conn_sock == -1) {
               perror("accept");
               exit(EXIT_FAILURE);
           }
           
           // 将连接套接字设定为非阻塞、边缘触发，然后注册到epoll上
           setnonblocking(conn_sock);
           ev.events = EPOLLIN | EPOLLET;
           ev.data.fd = conn_sock;
           if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock,
                       &ev) == -1) {
               perror("epoll_ctl: conn_sock");
               exit(EXIT_FAILURE);
           }
       } else {
           do_use_fd(events[n].data.fd);
       }
   }
}
```

然后我们再分析一下epoll的原理。

epoll_create创建了一个文件描述符，这个文件描述符实际是指向的一个红黑树。当用epoll_ctl函数去注册文件描述符时，就是往红黑树中插入一个节点，该节点中存储了该文件描述符的信息。当某个文件描述符准备好了，回去调用一个回调函数ep_poll_callback将这个文件描述符准备好的信息放到rdlist里，epoll_wait则阻塞于rdlist直到其中有数据。

![](https://user-gold-cdn.xitu.io/2019/3/27/169bf8420a85039f?w=1014&h=375&f=png&s=36778)

## proactor模式

proactor模式就是采用异步IO加上IO多路复用的方式。使用异步IO，将读写的任务也交给了内核来做，当数据已经准备好了，用户线程直接就可以用，然后处理业务逻辑就OK了。

## 多种模式的服务器该如何选择

常量连接常量请求，如：管理后台，政府网站，可以使用ppc和tpc模式

常量连接海量请求，如：中间件，可以使用ppc和tpc模式

海量连接常量请求，如：门户网站，ppc和tpc不能满足需求，可以使用reactor模式

海量连接海量请求，如：电商网站，秒杀业务等，ppc和tpc不能满足需求，可以使用reactor模式