
/*
** 每个连接创建一个线程 
** gcc命令：gcc -Wall -std=c99 -D_POSIX_C_SOURCE -o tpc_server tpc_server.c -lpthread
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

#define BUFSIZE 100

void sig_int(int signo);
void* doit(void* arg);

void sig_int(int signo)
{
    exit(0);
}


int main(int argc, char* argv[])
{
    int iListenFd, iConnectFd;
    struct sockaddr_in stServAddr, stCliAddr;
    pthread_t tid;
    socklen_t clilen;

    // 创建套接字
    if ((iListenFd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket error");
        exit(1);
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

    // 注册信号处理函数
    signal(SIGINT, sig_int);

    printf("listen on *:8888\n");

    while(1)
    {
        clilen = sizeof(stCliAddr);
        if ((iConnectFd = accept(iListenFd, (struct sockaddr*)&stCliAddr, &clilen)) < 0)
        {
            perror("accept error");
            exit(1);
        }

        if (pthread_create(&tid, NULL, doit, (void*)iConnectFd) < 0)
        {
            perror("pthread_create error");
            exit(1);
        }
    }

    close(iListenFd);

    return 0;
}

void* doit(void* arg)
{
    int n = 0;
    int iConnectFd = (int)arg;
    char buf[BUFSIZE];

    // 分离线程
    pthread_detach(pthread_self());

    while((n = read(iConnectFd, buf, BUFSIZE)) > 0)
    {
        printf("recv: %s\n", buf);
        if (write(iConnectFd, buf, n) < 0)
        {
            perror("write error");
            return 1;
        }
    }
    close(iConnectFd);
    return (NULL);
}