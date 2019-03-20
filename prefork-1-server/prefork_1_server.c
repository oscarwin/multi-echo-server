
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

static int iChildNum;
static pid_t* pids;

pid_t child_make(int iListenFd);
void child_main(int iListenFd);
void sig_int(int signo);

void sig_int(int signo)
{
    for (int i = 0; i < iChildNum; ++i)
    {
        kill(pids[i], SIGTERM);
    }

    while (wait(NULL) > 0);
    exit(0);
}

pid_t child_make(int iListenFd)
{
    pid_t pid;

    if ((pid = fork()) > 0)
    {
        return pid;
    }

    child_main(iListenFd);
    return 0;
}

void child_main(int iListenFd)
{
    int iConnectFd;
    socklen_t clilen;
    char buf[BUFSIZE];
    int n;
    struct sockaddr_in stCliAddr;
    while(1)
    {
        clilen = sizeof(stCliAddr);
        if ((iConnectFd = accept(iListenFd, (struct sockaddr*)&stCliAddr, &clilen)) < 0)
        {
            perror("accept error");
        }

        while((n = read(iConnectFd, buf, BUFSIZE)) > 0)
        {
            printf("recv: %s\n", buf);
            if (write(iConnectFd, buf, n) < 0)
            {
                perror("write error");
            }
        }
    }
    close(iConnectFd);
}

int main(int argc, char* argv[])
{
    int iListenFd;
    struct sockaddr_in stServAddr;
    
    if (argc == 2)
    {
        iChildNum = atoi(argv[1]);
    }
    else
    {
        iChildNum = 10;
    }

    pids = (pid_t*)calloc(iChildNum, sizeof(pid_t));

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
    signal(SIGINT, sig_int);

    printf("listen on *:8888\n");

    for (int i = 0; i < iChildNum; ++i)
    {
        pids[i] = child_make(iListenFd);
    }

    pause();

    return 0;
}