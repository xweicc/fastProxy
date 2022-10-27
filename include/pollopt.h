#ifndef __POLLOPT_H__
#define __POLLOPT_H__

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <netdb.h> 
#include <ctype.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <list.h>

#define pollMaxNum 1024
#define bufMaxSize 10240
#define pollMask 0xFF

typedef unsigned int __u32;
typedef unsigned short __u16;
typedef unsigned char __u8;

typedef void (*pollFun)(unsigned long data);
struct Socket{
	struct hlist_node hlist;
	unsigned long data;
	
	pollFun in;
	pollFun out;
	pollFun err;
	
	int fd;
	int pollId;
	
	char recvBuf[bufMaxSize];
	int recvBufUsed;
};

struct Poll{
	struct hlist_head hhead[pollMask+1];
	struct pollfd array[pollMaxNum];
	int used;	//poll使用的最大下标
};


static inline void *memMalloc(int size)
{
	void *p=malloc(size);
	if(p){
		memset(p, 0, size);
	}
	return p;
}
static inline void memFree(void *buf)
{
	if(buf){
		free(buf);
	}
}

void pollInit(struct Poll *poll);
int pollAdd(struct Poll *poll, struct Socket *sock);
void pollDelete(struct Poll *poll, struct Socket *sock);
void pollRun(struct Poll *poll, int timeout);
void pollOut(struct Poll *This, struct Socket *sock, int opt);
struct Socket *SocketFind(struct Poll *This, int fd);
int setNonblock(int sock);
int setReuseaddr(int sock);

void Fprintf(const char *fun, int line, const char *fmt, ...);
#define Printf(fmt, args...) Fprintf(__FUNCTION__, __LINE__, fmt, ##args)

#endif


