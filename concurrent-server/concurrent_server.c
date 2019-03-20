
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

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
        return 1;
    }

    // 绑定套接字
    bzero(&stServAddr, sizeof(stServAddr));
    stServAddr.sin_family = AF_INET;
    stServAddr.sin_port = htons(8888);
    stServAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(iListenFd, (struct sockaddr*)&stServAddr, sizeof(stServAddr)) < 0)
    {
        perror("bind error");
        return 1;
    }

    // 监听套接字
    if (listen(iListenFd, 5) < 0)
    {
        perror("listen error");
        return 1;
    }

    // 注册信号处理函数
    signal(SIGCHLD, sig_child);

    printf("listen on *:8888\n");

    while(1)
    {
        clilen = sizeof(stCliAddr);
        if ((iConnectFd = accept(iListenFd, (struct sockaddr*)&stCliAddr, &clilen)) < 0)
        {
            perror("accept error");
            return 1;
        }

        if ((childPid = fork()) == 0)
        {
            close(iListenFd);

            while((n = read(iConnectFd, buf, BUFSIZE)) > 0)
            {
                printf("recv: %s\n", buf);
                if (write(iConnectFd, buf, n) < 0)
                {
                    perror("write error");
                    return 1;
                }
            }
        }
        close(iConnectFd);
    }

    return 0;
}