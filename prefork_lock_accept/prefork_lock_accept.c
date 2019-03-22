/*
** 预先派生子进程，使用进程间共享的线程锁保护accept
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
#include <sys/mman.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BUFSIZE 100

static int iChildNum;
static pid_t* pids;
static pthread_mutex_t *mptr;

pid_t child_make(int iListenFd);
void child_main(int iListenFd);
void sig_int(int signo);
void my_lock_init();
int my_lock();
int my_release();

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

        int iRet = 0;
        iRet = my_lock();
        if (iRet < 0)
        {
            continue;
        }

        if ((iConnectFd = accept(iListenFd, (struct sockaddr*)&stCliAddr, &clilen)) < 0)
        {
            perror("accept error");
            exit(1);
        }

        my_release();

        while((n = read(iConnectFd, buf, BUFSIZE)) > 0)
        {
            printf("recv: %s\n", buf);
            if (write(iConnectFd, buf, n) < 0)
            {
                perror("write error");
                exit(1);
            }
        }
    }
    close(iConnectFd);
}

void my_lock_init()
{
    int fd;
    pthread_mutexattr_t mattr;

    if ((fd = open("/dev/zero", O_RDWR)) <0)
    {
        perror("open error");
        exit(1);
    }

    mptr = mmap(0, sizeof(pthread_mutex_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mptr == MAP_FAILED)
    {
        perror("mmap error");
        exit(1);
    }
    close(fd);

    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(mptr, &mattr);
}

int my_lock()
{
    if (pthread_mutex_lock(mptr) < 0)
    {
        perror("pthread_mutex_lock error");
        return -1;
    }
    return 0;
}

int my_release()
{
    if (pthread_mutex_unlock(mptr) < 0)
    {
        perror("pthread_mutex_unlock error");
        return -1;
    }
    return 0;
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
    my_lock_init();

    printf("listen on *:8888\n");

    for (int i = 0; i < iChildNum; ++i)
    {
        pids[i] = child_make(iListenFd);
    }

    pause();

    return 0;
}