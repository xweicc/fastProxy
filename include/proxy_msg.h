#ifndef __PROXY_MSG_H__
#define __PROXY_MSG_H__

#define msgMagic 0xC6D8A2B4
#define httpSize 36

#define httpGet "GET / HTTP/1.1\r\nHost: baidu.com\r\n\r\n"
#define httpOK "HTTP/1.1 200 OK\r\n\r\n"

enum{
	msgTypeNone,
	msgTypeConnect,
	msgTypeReply,
};

enum sockType{
	sockTypeNone,
	sockTypeHttp,
	sockTypeTcp,
	sockTypeSocks5First,
	sockTypeSocks5,
};

typedef struct{
	char http[httpSize];
	__u8 sec;
	__u8 type;
	__u16 len;
	__u32 magic;
	__u8 data[0];
} proxyMsgHead;

typedef struct{
	char host[64];
	__u16 port;
	__u8 sockType;
	__u8 reserved;
} proxyMsgConnect;

typedef struct{
	__u8 connect;
	__u8 reserved[3];
} proxyMsgReply;

static inline void proxyCrypt(unsigned char sec,void *data,int len)
{
	unsigned char *p=data;
	while(len){
		len--;
		p[len]=p[len]^sec;
	}
}

#endif

