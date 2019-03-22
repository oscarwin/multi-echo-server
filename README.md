# multi-echo-server
all kind of echo server

**base-server** 

基础的回射服务器

**concurrent-server**

并发服务器

**prefork-1-server**

预先派生子进程，accept无上锁保护

gcc -Wall -std=c99 -D_POSIX_C_SOURCE -o prefork-1-server prefork-1-server.c

**prefork-3-server** 

预先派生子进程，accept使用线程上锁保护

gcc -Wall -std=c99 -D_POSIX_C_SOURCE -o prefork-3-server prefork-3-server.c -lpthread

