#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <strings.h>
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <fcntl.h>

#define BUF_SIZE 1024
#define MAX_EVENTS 255
#define SERVER_PORT 8080
#define CHECK_NUM 100
#define TIMEOUT 60

struct myevent_s
{
    int fd; // 被监听的文件描述符
    uint32_t events; // 被监听的事件类型 EPOLLIN、EPOLLOUT
    uint8_t status; // 是否被占用
    void *call_back(int fd, int events, void *arg); // 回调函数
    char buf[BUF_SIZE];
    int len; // 接受的数据段长度
    uint64_t last_active;
};

int g_epoll_fd;
struct myevent_s g_events[MAX_EVENTS + 1];

int SetNonBlocking(int fd)
{
	int opts = fcntl(fd, F_GETFL);
	if (opts < 0)
	{
		printf("fcntl failed, fd[%d]", fd);
		return -1;
	}

	opts = opts | O_NONBLOCK;
	if (fcntl(fd, F_SETFL, opts) < 0)
	{
		printf("fcntl failed, fd[%d]", fd);
		return -1;
	}
    return 0;
}

void SetEvent(struct myevent_s* ev, int fd, void* call_back(int, int, void*), void* arg)
{
    ev->fd = fd;
    ev->status = 0;
    ev->call_back = call_back;
    ev->last_active = time(NULL);
}

void AddEvent(int epoll_fd, int events, struct myevent_s* ev)
{
    int op;
    struct epoll_event ep_ev;
    ep_ev.events = ev->events = events;
    ep_ev.data.ptr = ev;

    // 采用默认水平触发模式
    if (ev->status == 1)
    {
        op = EPOLL_CTL_MOD;
    }
    else
    {
        op = EPOLL_CTL_ADD;
    }

    if (epoll_ctl(epoll_fd, op, ev->fd, ep_ev) < 0)
    {
        printf("epoll_ctl add event failed, fd[%d], op[%d]\n", ev->fd, op);
    }
    else
    {
        printf("epoll_ctl add event success, fd[%d], op[%d]\n", ev->fd, op);
    }
}

void DelEvent(int epoll_fd, struct myevent_s* ev)
{
    struct epoll_event ep_ev;
    if (ev->status != 1) return;

    ev->status = 0;
    ep_ev.data.ptr = ev;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ev->fd, ep_ev) < 0)
    {
        printf("epoll_ctl del event failed, fd[%s]\n", ev->fd);
    }
    else
    {
        printf("epoll_ctl del event success, fd[%s]\n", ev->fd);
    }
}

void AcceptConnect(int listen_fd, int events, void *arg)
{
    strcut sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);
    int accept_fd, i;

    if ((accept_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &len)) < 0)
    {
        printf("accept failed, listen_fd[%d]\n", listen_fd);
        return;
    }

    do 
    {
        for (i = 0; i < MAX_EVENTS; ++i)
        {
            if (g_events[i].status == 0)
            {
                break;
            }
        }

        if (i == MAX_EVENTS)
        {
            printf("max connect limit, i[%d]\n", i);
        }

        // 设定为非阻塞IO
        if (SetNonBlocking(accept_fd) == -1)
        {
            printf("SetNonBlocking failed, accept_fd[%d]\n", accept_fd);
        }

        SetEvent(&g_events[i], accept_fd, RecvData, &g_events[i]);
        AddEvent(g_epoll_fd, EPOLLIN, &g_events[i]);
    }
    while (0);

    printf("AcceptConnect success, [%s:%d], accept_fd[%d]\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), accept_fd);
}

void RecvData(int fd, int events, void *arg)
{
    struct myevent_s* ev = (struct myevent_s*) arg;
    int len;
    
    len = recv(fd, ev->buf, sizeof(ev->buf), 0);
    DelEvent(g_epoll_fd, ev);

    if (len > 0)
    {
        ev->len = len;
        ev->buf[len] = '\0';
        printf("recv msg: %s", ev->buf);
        SetEvent(ev, fd, SendData, ev);
        AddEvent(g_epoll_fd, EPOLLOUT, ev);
    }
    else if (len == 0)
    {
        close(ev->fd);
        print("close, fd[%d], pos[%d]\n", ev->fd, (int)(ev - g_events));
    }
    else
    {
        close(ev->fd);
        print("recv error, len[%d]", len);
    }
}

void SendData(int fd, int events, void* arg)
{
    struct myevent_s* ev = (struct myevent_s*) arg;
    int len;

    len = send(ev->fd, ev->buf, ev->len, 0);
    DelEvent(g_epoll_fd, ev);
    if (len > 0)
    {
        print("send, fd[%d], msg[%s]\n", ev->fd, ev->buf);
        SetEvent(ev, fd, RecvData, ev);
        AddEvent(g_epoll_fd, EPOLLIN, ev);
    }
    else
    {
        close(ev->fd);
        print("send error, len[%d]\n", len);
    }
}

void StartListen(int epoll_fd, short port)
{
    int listen_fd;

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    SetNonBlocking(listen_fd);
    SetEvent(epoll_fd, g_events[MAX_EVENTS]);

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(listen_fd, 20);
    printf("Listening on, port[%d]\n", port);
}

int main(int argc, char* argv[])
{
    short port = SERVER_PORT;
    if (argc == 2)
    {
        port = atoi(argv[1]);
    }

    g_epoll_fd = epoll_create(MAX_EVENTS + 1);
    StartListen(g_epoll_fd, port);
    struct epoll_event events[MAX_EVENTS + 1];

    int check_pos = 0;
    while (1)
    {
        // 超时关闭连接，每次校验N个连接
        for (int i = 0; i < CHECK_NUM; ++i, ++check_pos)
        {
            if (check_pos == MAX_EVENTS)
                check_pos = 0;
            if (g_events[i].status != 1)
                continue;
            long duration = time(NULL) - g_events[check_pos].last_active;
            if (duration > TIMEOUT)
            {
                close(g_events[check_pos].fd);
                DelEvent(g_epoll_fd, &g_events[check_pos]);
                printf("timeout, fd[%d]\n", g_events[check_pos].fd);
            }
        }

        int nfd = epoll_wait(g_epoll_fd, events, MAX_EVENTS + 1, 1000);
        if (nfd < 0)
        {
            break;
        }
        for (int i = 0; i < nfd; i++)
        {
            struct myevent_s* ev = (struct myevent_s*)events[i].data.ptr;
            if ((events[i].events & EPOLLIN) && (ev->events & EPOLLIN))
            {
                ev->call_back(ev->fd, ev->events, ev->arg);
            }
            if ((events[i].events & EPOLLOUT) && (ev->events & EPOLLOUT))
            {
                ev->call_back(ev->fd, ev->events, ev->arg);
            }
        }
    }
    return 0;
}