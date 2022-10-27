#ifndef __ASYNC_DNS_H__
#define __ASYNC_DNS_H__

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
#include <asm/byteorder.h>

#include "timer.h"

#define DNS_RCODE_OK 0

typedef void(*async_dns_fun)(unsigned long data, unsigned int ip);

struct _dnshdr{
	unsigned short 	id;			//标识[Identification]
	union {
		struct {
			#if defined(__BIG_ENDIAN_BITFIELD)
			unsigned short 	qr:1,		//查询/响应[Query/Response]
					opcode:4,	//操作码[Operator Code]
					aa:1,		//权威回答[Authoritative Answer]
					tc:1,		//截断[Truncation]
					rd:1,		//期望递归[Recursion Desired]
					
					ra:1,		//可用递归[Recursion Available]
					zero:3,		//保留字段
					rcode:4;	//响应码[Response Code]
			#elif defined (__LITTLE_ENDIAN_BITFIELD)
			unsigned short 	rd:1,		//期望递归[Recursion Desired]
					tc:1,		//截断[Truncation]
					aa:1,		//权威回答[Authoritative Answer]
					opcode:4,	//操作码[Operator Code]
					qr:1,		//查询/响应[Query/Response]
					
					rcode:4,	//响应码[Response Code]
					zero:3, 	//保留字段
					ra:1;		//可用递归[Recursion Available]
			#else
			#error	"Please fix <asm/byteorder.h>"
			#endif
		};
		unsigned short flags;
	};
	unsigned short	qdcount;	//问题数[Questions]
	unsigned short	ancount;	//答案数[Answers]
	unsigned short	nscount;	//权威答案数[Authority]
	unsigned short	arcount;	//附加答案数[Additional]
};


struct async_dns{
	struct Socket sock;
	struct timer_list timer;
	async_dns_fun fun;
	int count;
	char dns[64];
	unsigned int ip;
	unsigned long data;
};

void asyncDnsInit(unsigned int server,struct Poll *poll);
struct async_dns *asyncDnsParse(char *dns, unsigned long data, void(*fun)(unsigned long , unsigned int ));
void asyncDnsFree(struct async_dns *ad);
unsigned int StrToIp(char *stringin);


#endif


