/*
** 为每个连接fork一个子进程进行处理
** gcc命令： gcc -Wall -o pcc-server pcc.c
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

#define BUFSIZE 100

void sig_child(int signo)
{
    pid_t pid;
    int stat;
    while ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
    {
        printf("child %d exit\n", pid);
    }
    return;
}

int main(int argc, char* argv[])
{
    int iListenFd, iConnectFd;
    struct sockaddr_in stServAddr, stCliAddr;
    socklen_t clilen;
    char buf[BUFSIZE];
    int n;
    pid_t childPid;
    
    // 创建套接字
    if ((iListenFd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket error");
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
        exit(EXIT_FAILURE);
    }

    // 监听套接字
    if (listen(iListenFd, 5) < 0)
    {
        perror("listen error");
        exit(EXIT_FAILURE);
    }

    // 注册信号处理函数，接受子进程退出信号，避免进程僵尸
    signal(SIGCHLD, sig_child);

    printf("listen on *:8888\n");

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

    return 0;
}