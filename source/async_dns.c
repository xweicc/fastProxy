/*
	Parse DNS asynchronously

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License 
	as published by the Free Software Foundation.

	Auther: QiaoWei <qwiaoei@163.com>
	Date:   Tue Jun 8 2021
*/
#include <client.h>

static struct {
	struct {
		unsigned int server;
		unsigned int defServer;
		int rtt;
		int rtc;
	}config;
	struct Poll *poll;
}async;

//获取系统配置的DNS服务器
static unsigned int getSysDnsServer(void)
{
	unsigned int ip=0;
	char buf[1024]={0},*p,*end;
	FILE *fp=NULL;
	
	fp=fopen("/etc/resolv.conf","r");
	if(!fp){
		return 0;
	}
	while(1){
		memset(buf, 0, sizeof(buf));
		if(fgets(buf,sizeof(buf)-1,fp)==NULL){
			break;
		}
		if(buf[0]=='#'){
			continue;
		}
		if(strncmp(buf, "nameserver", 10)==0){
			p=buf+10;
			while(*p==' ' || *p=='\t') p++;
			end=strchr(p,'\r');
			if(end){
				*end=0;
			}
			end=strchr(p,'\n');
			if(end){
				*end=0;
			}
			ip=StrToIp(p);
			break;
		}
	}
	
	if(fp){
		fclose(fp);
	}
	return ip;
}

/*
	dns字符串转网络序
	BaiDu.com->5baidu3com0
*/
static int dnsStrToNet(char *in, char *out, int out_len)
{
	int i = 1;
	unsigned char num = 0;
	int dot_pos = 0;
	
	while(*in){
		if(*in == '.'){
			out[dot_pos] = num;
			dot_pos = i;
			num = 0;
		}else{
			if(*in >= 'A' && *in <='Z'){
				out[i]=(*in) + ('a' -'A');
			}else{
				out[i]=*in;
			}
			num++;
		}		
		in++;i++;
		
		if(i >= out_len-1){
			return -1;
		}
	}
	out[dot_pos] = num;
	out[i]=0;

	return i;
}

static unsigned short dnsQueryIdGet(void)
{
	static unsigned short id=10000;

	if(id++ > 20000){
		id=10000;
	}

	return htons(id);
}

static int buildDnsQueryPacket(char *dns, char *buf, int size)
{
	struct _dnshdr *hdr=(void *)buf;
	char *ptr;

	hdr->id=dnsQueryIdGet();
	hdr->rd=1;
	hdr->qdcount=htons(1);

	ptr=(char *)(hdr+1);
	strncpy(ptr,dns,size-sizeof(*hdr));
	ptr+=strlen(dns)+1;

	ptr[0]=0;
	ptr[1]=1;
	ptr+=2;

	ptr[0]=0;
	ptr[1]=1;
	ptr+=2;

	return ptr-buf;
}

//解析DNS响应，返回IP地址
static unsigned int parseDnsResponse(void *data, int size)
{
	unsigned int ip;
	int i;
	struct _dnshdr *hdr=data;
	unsigned short ancount=htons(hdr->ancount);
	unsigned short qdcount=htons(hdr->qdcount);
	unsigned char *ptr, tlen;
	unsigned short Type, Class, ipLen;

	if(size<=sizeof(*hdr)){
		return 0;
	}
	if(hdr->qr!=1 || hdr->rcode!=0 || ancount<1){
		return 0;
	}

	ptr=(unsigned char *)(hdr+1);
	size-=sizeof(*hdr);

	//跳过问题
	for(i=0;i<qdcount;i++){
		while(1){
			if(!size){
				break;
			}
			tlen=*ptr++;size--;
			if(!tlen){
				ptr+=4;
				break;
			}
			if(tlen>=size){
				break;
			}
			ptr+=tlen;size-=tlen;
		}
	}
	if(!size){
		return 0;
	}

	for(i=0;i<ancount;i++){
		tlen=*ptr++;size--;
		if((tlen&0xC0)==0xC0){
			ptr++;size--;
		}else{
			Printf("tlen:0x%02X error\n",tlen);
			return 0;
		}
		
		Type=*ptr++;
		Type=(Type<<8)+*ptr++;
		size-=2;
		
		Class=*ptr++;
		Class=(Class<<8)+*ptr++;
		size-=2;

		ptr+=4;size-=4;	//ttl
		
		ipLen=*ptr++;
		ipLen=(ipLen<<8)+*ptr++;
		size-=2;

		if(Type==1 && Class==1){
			if(ipLen!=4){
				return 0;
			}
			memcpy(&ip,ptr,4);
			break;
		}else{
			ptr+=ipLen;
			size-=ipLen;
		}
	}

	return ip;
}

static int buildDnsQuerySend(struct async_dns *ad)
{
	int ret=-1,len;
	struct sockaddr_in to;
	socklen_t sockLen=sizeof(to);
	char buf[1024]={0};
	
	len=buildDnsQueryPacket(ad->dns, buf, sizeof(buf));

	bzero(&to, sizeof(to));
	to.sin_addr.s_addr=ad->count?async.config.defServer:async.config.server;
	to.sin_port=htons(53);
	to.sin_family=AF_INET;

	if(sendto(ad->sock.fd, buf, len, 0, (struct sockaddr *)&to, sockLen)==-1){
		Printf("sendto failed:%s\n",strerror(errno));
		goto out;
	}
	ret=0;
	//Printf("Send %d bytes\n",len);

out:
	return ret;
}

static void dnsCallFun(struct async_dns *ad)
{
	ad->fun(ad->data, ad->ip);
	del_timer(&ad->timer);
	if(ad->sock.fd>0){
		close(ad->sock.fd);
		pollDelete(async.poll, &ad->sock);
	}
	memFree(ad);
}

static void recvDnsResponse(unsigned long data)
{
	int ret;
	char buf[1024]={0};
	struct sockaddr_in from;
	socklen_t sockLen=sizeof(from);
	struct async_dns *ad=(void *)data;

	ret=recvfrom(ad->sock.fd, buf, sizeof(buf),0, (struct sockaddr*)&from, &sockLen);
	if(ret<=0){
		Printf("recvfrom error:%s\n",strerror(errno));
		return ;
	}
	if(from.sin_addr.s_addr!=async.config.server && from.sin_addr.s_addr!=async.config.defServer){
		return ;
	}
	
	ad->ip=parseDnsResponse(buf,ret);
	//Printf("dns:%s ip:%s\n",ad->dns,ipstr(ad->ip));
	dnsCallFun(ad);
}

void dnsRecvTimeout(unsigned long data)
{
	struct async_dns *ad=(void *)data;
	
	if(ad->count++>async.config.rtc){
		dnsCallFun(ad);
		return ;
	}
	
	buildDnsQuerySend(ad);

	mod_timer(&ad->timer, jiffies+async.config.rtt);
}

static int sendDnsQuery(struct async_dns *ad)
{
	ad->sock.fd=socket(AF_INET,SOCK_DGRAM,0);
	if(ad->sock.fd==-1){
		Printf("socket failed:%s\n",strerror(errno));
		return -1;
	}

	if(setNonblock(ad->sock.fd)==-1){
		Printf("setNonblock falied\n");
		goto out;
	}

	ad->sock.data=(unsigned long)ad;
	ad->sock.in=recvDnsResponse;
	if(pollAdd(async.poll, &ad->sock)){
		goto out;
	}

	buildDnsQuerySend(ad);
	mod_timer(&ad->timer, jiffies+async.config.rtt);
	
	return 0;
	
out:
	close(ad->sock.fd);
	return -1;
}



/*
	DNS异步解析
	参数：
		dns：域名
		data：给回调函数的参数
		fun：解析完成的回调函数
	返回：
		解析的结构体
*/
struct async_dns *asyncDnsParse(char *dns, unsigned long data, void(*fun)(unsigned long , unsigned int ))
{
	struct async_dns *ad;

	if(!dns || !dns[0] || !fun){
		return NULL;
	}

	ad=memMalloc(sizeof(*ad));
	if(!ad){
		return NULL;
	}
	ad->fun=fun;
	ad->data=data;
	setup_timer(&ad->timer, dnsRecvTimeout, (unsigned long)ad);

	ad->ip=StrToIp(dns);
	if(ad->ip){
		ad->count=async.config.rtc+1;
		mod_timer(&ad->timer, jiffies);
		return ad;
	}

	if(-1==dnsStrToNet(dns, ad->dns, sizeof(ad->dns))){
		memFree(ad);
		return NULL;
	}

	if(sendDnsQuery(ad)){
		memFree(ad);
		return NULL;
	}

	return ad;
}

/*
	DNS异步解析初始化
	参数：
		server：DNS服务器IP
*/
void asyncDnsInit(unsigned int server,struct Poll *poll)
{
	async.poll=poll;
	async.config.defServer=server;
	async.config.rtt=200;
	async.config.rtc=2;

	async.config.server=getSysDnsServer();
	if(!async.config.server){
		async.config.server=server;
	}
	Printf("Dns Server ip: %s\n",ipstr(async.config.server));
}

void asyncDnsFree(struct async_dns *ad)
{
	if(ad->sock.fd>0){
		close(ad->sock.fd);
		pollDelete(async.poll, &ad->sock);
	}
	del_timer(&ad->timer);
	memFree(ad);
}

unsigned int StrToIp(char *stringin)
{
	char * cp;
	int dots = 0;   /* periods imbedded in input string */
	int number;
	union{
		unsigned char c[4];
		unsigned int l;
	} retval;
	if(!stringin)
		return 0;

	cp = stringin;
	while(*cp)
	{
		if(*cp > '9' || *cp < '.' || *cp == '/')
			return 0;
		if(*cp == '.')	dots++;
		cp++;
	}

	if( dots != 3 )
		return 0;

	cp = stringin;
	if((number = atoi(cp)) > 255)   /* set net number */
		return 0;
	if(number==0)
		return 0;

	retval.c[0] = (unsigned char)number;

	while(*cp != '.')cp++;   	/* find dot (end of number) */
	cp++;            			/* point past dot */

	number = atoi(cp);
	while(*cp != '.')cp++;   /* find dot (end of number) */
	cp++;            /* point past dot */
	if(number > 255) return 0;
	retval.c[1] = (unsigned char)number;


	number = atoi(cp);
	while(*cp != '.')cp++;   /* find dot (end of number) */
	cp++;            /* point past dot */
	if(number > 255) return 0;
	retval.c[2] = (unsigned char)number;

	if((number = atoi(cp)) >255)
		return 0;
	retval.c[3] = (unsigned char)number;

	return (retval.l);         /* return OK code (no error string) */
}

