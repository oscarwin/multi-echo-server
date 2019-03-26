/*
** reactor模式 = io多路复用 + 非阻塞io
** reactor有多种形式
** 1 单进程/线程, 单reactor
** 2 多线程, 单reactor
** 3 多线程/进程, 多reactor
** 本例为单进程，单reactor
** gcc命令：gcc -Wall -o reactor_server reactor_server.c -std=c99
*/

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <fcntl.h>

#define MAX_EVENTS 20
#define BUF_SIZES  200

void setnonblocking(int fd)
{
	int opts = fcntl(fd, F_GETFL);
	if (opts < 0)
	{
		perror("fcntl error");
		exit(EXIT_FAILURE);
	}

	opts = opts | O_NONBLOCK;
	if (fcntl(fd, F_SETFL, opts) < 0)
	{
		perror("fcntl error");
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char* argv[])
{
    int iListenFd, iConnectFd;
    int iEpollFd;
    struct sockaddr_in stServAddr, stCliAddr;
    socklen_t clilen;
    // pthread_t tid;
    int iPthreadNum = 0;
    int ifds;
    int n;
    struct epoll_event stEv, stEvents[MAX_EVENTS];
    char buf[BUF_SIZES];

    if (argc == 2)
    {
        iPthreadNum = atoi(argv[1]);
    }
    else
    {
        iPthreadNum = 10;
    }

    
    // 创建套接字
    if ((iListenFd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket error");
        exit(EXIT_FAILURE);
    }

    // 创建epoll句柄
    iEpollFd = epoll_create(256);
    if (iEpollFd == -1) 
    {
       perror("epoll_create error");
       exit(EXIT_FAILURE);
    }

    // 设定监听套接字为非阻塞io
    setnonblocking(iListenFd);

    // 将监听套接字注册到epoll中
    stEv.events = EPOLLIN | EPOLLET; // 设置监听可读，采用ET模式
    stEv.data.fd = iListenFd;        // 设置要监听的文件描述符
    if (epoll_ctl(iEpollFd, EPOLL_CTL_ADD, iListenFd, &stEv) < 0)
    {
    	perror("epoll_ctl error");
    	exit(EXIT_FAILURE);
    }

    // 绑定套接字
    bzero(&stServAddr, sizeof(stServAddr));
    stServAddr.sin_family = AF_INET;
    stServAddr.sin_port = htons(8888);
    stServAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(iListenFd, (struct sockaddr*)&stServAddr, sizeof(stServAddr)) < 0)
    {
        perror("bind error");
        exit(1);
    }

    // 监听套接字
    if (listen(iListenFd, 5) < 0)
    {
        perror("listen error");
        exit(1);
    }

    for ( ; ; )
    {
    	ifds = epoll_wait(iEpollFd, stEvents, MAX_EVENTS, -1);
    	if (ifds == -1)
    	{
    		perror("epoll_wait error");
    		exit(EXIT_FAILURE);
    	}

    	for (int i = 0; i < ifds; ++i)
    	{
    		// 判断为监听套接字
    		if (stEvents[i].data.fd == iListenFd)
    		{
    			clilen = sizeof(stCliAddr);
		        if ((iConnectFd = accept(iListenFd, (struct sockaddr*)&stCliAddr, &clilen)) < 0)
		        {
		            perror("accept error");
		            exit(1);
		        }

		        // 设定为非阻塞IO
		        setnonblocking(iConnectFd);

		        // 连接套接字注册到epoll
		        stEv.events = EPOLLIN | EPOLLOUT | EPOLLET;
		        stEv.data.fd = iConnectFd;
		        if (epoll_ctl(iEpollFd, EPOLL_CTL_ADD, iConnectFd, &stEv) < 0)
			    {
			    	perror("epoll_ctl error");
			    	exit(EXIT_FAILURE);
			    }
    		}
    		// 有数据可以读
    		else if (stEvents[i].events & EPOLLIN)
    		{
    			if ((n = read(stEvents[i].data.fd, buf, BUF_SIZES)) <= 0)
    			{
    				// 客户端关闭连接后，关闭文件描述符，从epoll删除注册信息
    				close(stEvents[i].data.fd);
    				epoll_ctl(iEpollFd, EPOLL_CTL_DEL, stEvents[i].data.fd, &stEv);
    			}
    			else
    			{
    				printf("recv: %s\n", buf);
		            fflush(stdout);
		            if (write(stEvents[i].data.fd, buf, n) < 0)
		            {
		                perror("write error");
		                exit(1);
		            }
    			}
    		}
    	} // end for (int i = 0; i < ifds; ++i)
    }// end for ( ; ; )

    return 0;
}