#ifndef __CLIENT_H__
#define __CLIENT_H__

#include <timer.h>
#include <pollopt.h>
#include <async_dns.h>
#include <flagopt.h>
#include <proxy_msg.h>

#define hostMaxSize 64
#define sessionTime 600
#define clientStart m##a##i##n

struct proxyClient{
	struct Poll poll;
	struct Socket listen;

	__u16 clientPort;
	__u16 serverPort;
	__u32 serverIp;
	int debug;
	int fast;
};
extern struct proxyClient client;

enum{
	flagDataParsed,
	flagServerDnsParsing,
	flagServerConnecting,
	flagProxyConnecting,
	flagServerConnected,
	flagProxyConnected,
	flagServerClose,
	flagProxyClose,
	flagUsedProxy,
	flagConfirmed,
	flagDestroy,
};

typedef struct{
	struct Socket client;
	struct Socket server;
	struct Socket proxy;
	struct timer_list timer;
	struct async_dns *ad;
	unsigned long flags;
	int sockType;
	char host[hostMaxSize];
	__u16 port;
	__u8 sec;
}Session;

static inline char *ip2str(__u32 ip, char *str)
{
	unsigned char *ip_dot=(unsigned char *)&ip;

	sprintf(str,"%d.%d.%d.%d",ip_dot[0],ip_dot[1],ip_dot[2],ip_dot[3]);
	return str;
}

static inline char *ipstr(__u32 ip)
{
	static char str[20]={0};
	unsigned char *ip_dot=(unsigned char *)&ip;

	sprintf(str,"%d.%d.%d.%d",ip_dot[0],ip_dot[1],ip_dot[2],ip_dot[3]);
	return str;
}


void sendServerData(unsigned long data);

#endif

