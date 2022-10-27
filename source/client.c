#include "client.h"

struct proxyClient client;

/*
	-1：出错
	=0：未收到数据
	>0：收到数据长度
*/
int tcpRecv(int sock, void *buf, int size)
{
	int len;
	errno=0;
	len=recv(sock,buf,size,0);
	if(len==0){
		return -1;
	}else if(len<0){
		if(errno==EINTR || errno==EAGAIN || errno==EINPROGRESS || errno==ERESTART || errno==0){
			return 0;
		}else{
			return -1;
		}
	}else{
		return len;
	}
}

/*
	-1：出错
	=0：未发送数据
	>0：发送数据长度
*/
int tcpSend(int sock, void *buf, int size)
{
	int len;
	errno=0;
	len=send(sock,buf,size,0);
	if(len==0){
		return -1;
	}else if(len<0){
		if(errno==EINTR || errno==EAGAIN || errno==EINPROGRESS || errno==ERESTART || errno==0){
			return 0;
		}else{
			return -1;
		}
	}else{
		return len;
	}
}

void sessionDestroy(Session *ss)
{
	if(ss->client.fd>0){
		close(ss->client.fd);
		pollDelete(&client.poll, &ss->client);
	}

	if(ss->server.fd>0){
		close(ss->server.fd);
		pollDelete(&client.poll, &ss->server);
	}

	if(ss->proxy.fd>0){
		close(ss->proxy.fd);
		pollDelete(&client.poll, &ss->proxy);
	}

	if(ss->ad){
		asyncDnsFree(ss->ad);
	}

	del_timer(&ss->timer);

	memFree(ss);
}

void closeProxy(Session *ss)
{
	if(ss->proxy.fd>0){
		close(ss->proxy.fd);
		pollDelete(&client.poll, &ss->proxy);
		ss->proxy.fd=-1;
	}
	set_flag(&ss->flags, flagProxyClose);
}

void closeServer(Session *ss)
{
	if(ss->ad){
		asyncDnsFree(ss->ad);
		ss->ad=NULL;
	}
	if(ss->server.fd>0){
		close(ss->server.fd);
		pollDelete(&client.poll, &ss->server);
		ss->server.fd=-1;
	}
	set_flag(&ss->flags, flagServerClose);
}


int serverConnected(Session *ss)
{
	if(ss->sockType==sockTypeHttp){
		if(ss->client.recvBufUsed){
			pollOut(&client.poll, &ss->server, 1);
		}
	}else if(ss->sockType==sockTypeTcp){
		char *str="HTTP/1.1 200 Connection Established\r\n\r\n";
		if(tcpSend(ss->client.fd, str, strlen(str))!=strlen(str)){
			Printf("send failed:%s\n",strerror(errno));
			return -1;
		}
	}else if(ss->sockType==sockTypeSocks5){
		char data[10]={0x05,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00};
		if(tcpSend(ss->client.fd, data, sizeof(data))!=sizeof(data)){
			Printf("send failed:%s\n",strerror(errno));
			return -1;
		}
	}else{
		Printf("sockType:%d error\n",ss->sockType);
		return -1;
	}

	closeProxy(ss);
	
	set_flag(&ss->flags, flagServerConnected);
	set_flag(&ss->flags, flagConfirmed);

	Printf("Host:%s Not proxy\n",ss->host);

	return 0;
}


void sendClientData(unsigned long data)
{
	int ret;
	Session *ss=(void *)data;
	struct Socket *sock;


	if(test_flag(&ss->flags, flagUsedProxy)){
		sock=&ss->proxy;
	}else{
		sock=&ss->server;
	}
	
	if(!sock->recvBufUsed){
		pollOut(&client.poll, &ss->client, 0);
		return ;
	}
	
	ret=tcpSend(ss->client.fd, sock->recvBuf, sock->recvBufUsed);
	if(ret==-1){
		Printf("send failed:%s\n",strerror(errno));
		goto err;
	}

	//Printf("send %d bytes\n",ret);

	sock->recvBufUsed-=ret;
	if(sock->recvBufUsed){
		if(ret){
			memmove(sock->recvBuf,sock->recvBuf+ret,sock->recvBufUsed);
		}
		pollOut(&client.poll, &ss->client, 1);
	}else{
		pollOut(&client.poll, &ss->client, 0);
	}
	
	return ;
err:
	sessionDestroy(ss);
}


int checkConnect(int sock)
{
	int error;
	socklen_t len=sizeof(error);
	if(getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len)){
		Printf("Error\n");
		return 0;
	}
	if(error){
		Printf("Error:%d\n",error);
		return 0;
	}
	return 1;
}

void serverSockError(unsigned long data)
{
	Session *ss=(void *)data;
	
	if(!test_flag(&ss->flags, flagServerConnected)){
		if(checkConnect(ss->server.fd)){
			if(serverConnected(ss)){
				goto err;
			}
			return ;
		}else{
			Printf("checkConnect failed\n");
			if(test_flag(&ss->flags, flagProxyClose)){
				goto err;
			}else{
				closeServer(ss);
				return ;
			}
		}
	}
err:
	sessionDestroy(ss);
}

void sessionTimeout(unsigned long data)
{
	Session *ss=(void *)data;
	sessionDestroy(ss);
}

//GET http://www.baidu.com/ HTTP/1.1
int httpProxyParsing(Session *ss)
{
	char *buf=ss->client.recvBuf;
	char *p=buf;
	int i=0;

	if(!strncmp(buf,"GET http://",11)){
		p+=11;
		buf+=4;
	}else if(!strncmp(buf,"POST http://",12)){
		p+=12;
		buf+=5;
	}else{
		Printf("Not find http\n");
		return -1;
	}
	
	while(*p&&*p!='/'&&*p!=':'&&i<hostMaxSize){
		ss->host[i++]=*p++;
	}
	if(i==hostMaxSize){
		Printf("Host too long\n");
		return -1;
	}
	if(*p==':'){
		ss->port=atoi(++p);
	}else{
		ss->port=80;
	}
	p=strchr(p,'/');
	if(!p){
		Printf("Not find '/'\n");
		return -1;
	}

	memmove(buf,p,ss->client.recvBufUsed-(p-ss->client.recvBuf));
	ss->client.recvBufUsed-=(p-buf);

	return 0;
}

//CONNECT www.baidu.com:443 HTTP/1.1
int tcpProxyParsing(Session *ss)
{
	char *p=ss->client.recvBuf+8;
	int i=0;
	
	while(*p&&*p!=':'&&i<hostMaxSize){
		ss->host[i++]=*p++;
	}
	if(i==hostMaxSize){
		Printf("Host too long\n");
		return -1;
	}
	if(*p!=':'){
		Printf("Not find ':'\n");
		return -1;
	}
	ss->port=atoi(++p);

	ss->client.recvBufUsed=0;

	return 0;
}

int socks5FirstProxyParsing(Session *ss)
{
	char *p=ss->client.recvBuf;
	int NMETHODS=p[1];
	int i,unauth=0;
	char reply[2]={0x05,0x00};

	p+=2;
	for(i=0;i<NMETHODS&&i<ss->client.recvBufUsed;i++){
		if(p[i]==0x0){
			unauth=1;
		}
	}
	if(!unauth){
		return -1;
	}
	ss->client.recvBufUsed=0;

	if(tcpSend(ss->client.fd, reply, sizeof(reply))!=sizeof(reply)){
		Printf("send failed:%s\n",strerror(errno));
		return -1;
	}

	return 0;	
}

void __printHex(const char *fun, int line, void *data, int len)
{
	unsigned char *p=data;
	int i=0;
	
	printf("[%s:%d]:Hex:%d\n",fun,line,len);
	for(i=0;i<len;i++){
		printf("%02X ",p[i]);
	}
	printf("\n");
}

#define printHex(data,len) do{\
		if(client.debug){__printHex((char *)__FUNCTION__,__LINE__,data,len);}\
	}while(0)

		
int socks5ReplyNotSupportIpv6(Session *ss)
{
	char reply[4]={0x05,0x08,0x00,0x04};

	if(tcpSend(ss->client.fd, reply, sizeof(reply))!=sizeof(reply)){
		Printf("send failed:%s\n",strerror(errno));
		return -1;
	}

	Printf("Reply.\n");

	return 0;	
}


int socks5ProxyParsing(Session *ss)
{
	char *p=ss->client.recvBuf;
	int ATYP=p[3],len;

	if(p[0]!=0x05 || p[1]!=0x01){
		Printf("p:%02X %02X\n",p[0],p[1]);
		return -1;
	}

	if(ATYP==0x01){
		ip2str((__u32)p[4],ss->host);
	}else if(ATYP==0x03){
		len=p[4];
		if(len+4>ss->client.recvBufUsed||len>hostMaxSize){
			return -1;
		}
		memcpy(ss->host,&p[5],len);
		p+=(5+len);
		ss->port=ntohs(*(unsigned short *)p);
	}else if(ATYP==0x04){
		return socks5ReplyNotSupportIpv6(ss);
	}else{
		Printf("ATYP:%02X\n",ATYP);
		printHex(ss->client.recvBuf,ss->client.recvBufUsed);
		return -1;
	}
	ss->client.recvBufUsed=0;

	return 0;
}


//解析数据，获取域名和端口信息
int proxyDataParsing(Session *ss)
{
	char *p=ss->client.recvBuf;

	if(ss->sockType==sockTypeSocks5First){
		ss->sockType=sockTypeSocks5;
		if(socks5ProxyParsing(ss)){
			Printf("socks5ProxyParsing failed\n");
			goto out;
		}
		set_flag(&ss->flags, flagDataParsed);
		return 0;
	}
	
	if(!strncmp(p,"GET",3) || !strncmp(p,"POST",4)){
		ss->sockType=sockTypeHttp;
		if(httpProxyParsing(ss)){
			Printf("httpProxyParsing failed\n");
			goto out;
		}
		set_flag(&ss->flags, flagDataParsed);
	}else if(!strncmp(p,"CONNECT ",8)){
		ss->sockType=sockTypeTcp;
		if(tcpProxyParsing(ss)){
			Printf("tcpProxyParsing failed\n");
			goto out;
		}
		set_flag(&ss->flags, flagDataParsed);
	}else if(p[0]==0x05){
		ss->sockType=sockTypeSocks5First;
		if(socks5FirstProxyParsing(ss)){
			Printf("socks5FirstProxyParsing failed\n");
			goto out;
		}
	}else{
		Printf("data error\n");
		goto out;
	}
	return 0;
out:
	return -1;
}


int httpSubtractUrlHost(Session *ss)
{
	char *p=ss->client.recvBuf;
	int delLen=0;

	//有的HTTP每个请求都有GET http://
	if(!strncmp(ss->client.recvBuf,"GET http://",11)){
		p+=11;
		p=strchr(p,'/');
		if(!p){
			return -1;
		}
		delLen=p-ss->client.recvBuf-4;
		memmove(ss->client.recvBuf+4,ss->client.recvBuf+delLen+4,ss->client.recvBufUsed-delLen);
		ss->client.recvBufUsed-=delLen;
	}else if(!strncmp(ss->client.recvBuf,"POST http://",12)){
		p+=12;
		p=strchr(p,'/');
		if(!p){
			return -1;
		}
		delLen=p-ss->client.recvBuf-5;
		memmove(ss->client.recvBuf+5,ss->client.recvBuf+delLen+5,ss->client.recvBufUsed-delLen);
		ss->client.recvBufUsed-=delLen; 
	}
	return 0;
}

void sendServerData(unsigned long data)
{
	int ret;
	Session *ss=(void *)data;

	if(!test_flag(&ss->flags, flagServerConnected)){
		if(checkConnect(ss->server.fd)){
			if(serverConnected(ss)){
				goto err;
			}
			return ;
		}else{
			Printf("checkConnect failed\n");
			if(test_flag(&ss->flags, flagProxyClose)){
				goto err;
			}else{
				closeServer(ss);
				return ;
			}
		}
	}

	if(!test_flag(&ss->flags, flagConfirmed)){
		return ;
	}

	if(!ss->client.recvBufUsed){
		return ;
	}

	if(ss->sockType==sockTypeHttp){
		if(httpSubtractUrlHost(ss)){
			return ;
		}
	}

	ret=tcpSend(ss->server.fd, ss->client.recvBuf, ss->client.recvBufUsed);
	if(ret==-1){
		Printf("send failed:%s\n",strerror(errno));
		goto err;
	}

	//Printf("send %d bytes\n",ret);
	ss->client.recvBufUsed-=ret;
	if(ss->client.recvBufUsed){
		if(ret){
			memmove(ss->client.recvBuf,ss->client.recvBuf+ret,ss->client.recvBufUsed);
		}
		pollOut(&client.poll, &ss->server, 1);
	}else{
		pollOut(&client.poll, &ss->server, 0);
	}
	
	return ;
err:
	sessionDestroy(ss);
}

void recvServerData(unsigned long data)
{
	int len;
	Session *ss=(void *)data;

	if(!test_flag(&ss->flags, flagServerConnected)){
		if(checkConnect(ss->server.fd)){
			if(serverConnected(ss)){
				goto err;
			}
			return ;
		}else{
			Printf("checkConnect failed\n");
			if(test_flag(&ss->flags, flagProxyClose)){
				goto err;
			}else{
				closeServer(ss);
				return ;
			}
		}
	}
	
	if(bufMaxSize==ss->server.recvBufUsed){
		return ;
	}
	len=tcpRecv(ss->server.fd,ss->server.recvBuf+ss->server.recvBufUsed,bufMaxSize-ss->server.recvBufUsed);
	if(len==0){
		return ;
	}else if(len==-1){
		if(errno){
			Printf("recv failed:%s\n",strerror(errno));
		}
		if(ss->server.recvBufUsed){
			set_flag(&ss->flags, flagDestroy);
			closeServer(ss);
			len=0;
		}else{
			goto err;
		}
	}
	//Printf("recv %d bytes\n",len);
	
	ss->server.recvBufUsed+=len;
	
	if(test_flag(&ss->flags, flagConfirmed)){
		return sendClientData((unsigned long)ss);
	}

	return ;
	
err:
	sessionDestroy(ss);
}


int proxyConnected(Session *ss)
{
	char buf[1024]={0};
	proxyMsgHead *head=(void*)buf;
	proxyMsgConnect *msg=(void*)head->data;

	strncpy(head->http,httpGet,sizeof(head->http));
	head->sec=rand();
	if(!head->sec){
		head->sec=1;
	}
	head->type=msgTypeConnect;
	head->len=htons(sizeof(*msg)+sizeof(*head));
	head->magic=htonl(msgMagic);

	strncpy(msg->host,ss->host,sizeof(msg->host));
	msg->port=htons(ss->port);
	msg->sockType=ss->sockType;
	
	proxyCrypt(head->sec, msg, sizeof(*msg));
	
	if(tcpSend(ss->proxy.fd, head, sizeof(*head)+sizeof(*msg))==-1){
		Printf("send failed:%s\n",strerror(errno));
		return -1;
	}

	set_flag(&ss->flags, flagProxyConnected);

	return 0;
}

void proxySockError(unsigned long data)
{
	Session *ss=(void *)data;
	
	if(!test_flag(&ss->flags, flagProxyConnected)){
		if(checkConnect(ss->proxy.fd)){
			if(proxyConnected(ss)){
				goto err;
			}
			return ;
		}else{
			Printf("checkConnect failed\n");
			if(test_flag(&ss->flags, flagServerClose)){
				goto err;
			}else{
				closeProxy(ss);
				return ;
			}
		}
	}
err:
	sessionDestroy(ss);
}

void dnsParseSuccess(unsigned long data, unsigned int ip)
{
	Session *ss=(void *)data;
	struct sockaddr_in addr={0};

	ss->ad=NULL;
	if(!ip || (ntohl(ip)&0xFF000000)==0x7F000000){
		Printf("ip failed. host:%s\n",ss->host);
		goto err;
	}

	addr.sin_family=AF_INET;
	addr.sin_addr.s_addr=ip;
	addr.sin_port=htons(ss->port);
	
	ss->server.fd=socket(AF_INET, SOCK_STREAM, 0);
	if(ss->server.fd<=0){
		Printf("socket:%s\n",strerror(errno));
		goto err;
	}

	setNonblock(ss->server.fd);
	
	set_flag(&ss->flags, flagServerConnecting);
	if(-1==connect(ss->server.fd,(struct sockaddr*)&addr,sizeof(addr))){
		if(errno!=EINPROGRESS){
			Printf("connect failed:%s\n",strerror(errno));
			goto err;
		}
	}else{
		if(serverConnected(ss)){
			goto err;
		}
	}

	if(pollAdd(&client.poll, &ss->server)){
		goto err;
	}
	if(!test_flag(&ss->flags, flagServerConnected)){
		pollOut(&client.poll, &ss->server, 1);
	}

	ss->server.data=(unsigned long)ss;
	ss->server.in=recvServerData;
	ss->server.out=sendServerData;
	ss->server.err=serverSockError;
	
	return ;
err:
	if(test_flag(&ss->flags, flagProxyClose)){
		sessionDestroy(ss);
	}else{
		closeServer(ss);
	}
}



int recvProxyMsg(Session *ss)
{
	proxyMsgHead *head=(void*)ss->proxy.recvBuf;
	proxyMsgReply *msg=(void*)head->data;

	if(ss->proxy.recvBufUsed<sizeof(*head)){
		Printf("recvBufUsed:%d\n",ss->proxy.recvBufUsed);
		return 0;
	}
	
	if(ntohl(head->magic)!=msgMagic){
		Printf("magic:%08X error\n",ntohl(head->magic));
		return -1;
	}

	if(ntohs(head->len)>ss->proxy.recvBufUsed){
		Printf("len:%d recvBufUsed:%d\n",ntohs(head->len),ss->proxy.recvBufUsed);
		return 0;
	}

	if(head->type!=msgTypeReply){
		Printf("head->type:%d\n",head->type);
		return -1;
	}

	if(!msg->connect){
		return -1;
	}
	ss->sec=head->sec;
	
	ss->proxy.recvBufUsed-=ntohs(head->len);

	closeServer(ss);

	if(ss->sockType==sockTypeHttp){
		if(ss->client.recvBufUsed){
			pollOut(&client.poll, &ss->proxy, 1);
		}
	}else if(ss->sockType==sockTypeTcp){
		char *str="HTTP/1.1 200 Connection Established\r\n\r\n";
		if(tcpSend(ss->client.fd, str, strlen(str))!=strlen(str)){
			Printf("send failed:%s\n",strerror(errno));
			return -1;
		}
	}else if(ss->sockType==sockTypeSocks5){
		char data[10]={0x05,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00};
		if(tcpSend(ss->client.fd, data, sizeof(data))!=sizeof(data)){
			Printf("send failed:%s\n",strerror(errno));
			return -1;
		}
	}else{
		Printf("sockType:%d error\n",ss->sockType);
		return -1;
	}

	set_flag(&ss->flags, flagUsedProxy);
	set_flag(&ss->flags, flagConfirmed);

	if(ss->proxy.recvBufUsed){
		if(ss->sec){
			proxyCrypt(ss->sec, ss->proxy.recvBuf, ss->proxy.recvBufUsed);
		}
	}

	if(ss->client.recvBufUsed){
		if(ss->sec){
			proxyCrypt(ss->sec, ss->client.recvBuf, ss->client.recvBufUsed);
		}
	}

	Printf("Host:%s Used proxy\n",ss->host);

	return 0;
}

void recvProxyData(unsigned long data)
{
	int len;
	Session *ss=(void *)data;

	if(!test_flag(&ss->flags, flagProxyConnected)){
		if(checkConnect(ss->proxy.fd)){
			if(proxyConnected(ss)){
				goto err;
			}
			return ;
		}else{
			Printf("checkConnect failed\n");
			if(test_flag(&ss->flags, flagServerClose)){
				goto err;
			}else{
				closeProxy(ss);
				return ;
			}
		}
	}
	
	if(bufMaxSize==ss->proxy.recvBufUsed){
		return ;
	}
	len=tcpRecv(ss->proxy.fd,ss->proxy.recvBuf+ss->proxy.recvBufUsed,bufMaxSize-ss->proxy.recvBufUsed);
	if(len==0){
		return ;
	}else if(len==-1){
		if(errno){
			Printf("recv failed:%s\n",strerror(errno));
		}
		if(ss->proxy.recvBufUsed){
			set_flag(&ss->flags, flagDestroy);
			closeProxy(ss);
			len=0;
		}else{
			goto err;
		}
	}
	//Printf("recv %d bytes\n",len);
	if(test_flag(&ss->flags, flagConfirmed)){
		if(ss->sec){
			proxyCrypt(ss->sec, ss->proxy.recvBuf+ss->proxy.recvBufUsed, len);
		}
	}
	ss->proxy.recvBufUsed+=len;

	if(test_flag(&ss->flags, flagConfirmed)){
		return sendClientData((unsigned long)ss);
	}else{
		if(recvProxyMsg(ss)){
			goto err;
		}
	}

	return ;
	
err:
	sessionDestroy(ss);
}

void sendProxyData(unsigned long data)
{
	int ret;
	Session *ss=(void *)data;

	if(!test_flag(&ss->flags, flagProxyConnected)){
		if(checkConnect(ss->proxy.fd)){
			if(proxyConnected(ss)){
				goto err;
			}
			return ;
		}else{
			Printf("checkConnect failed\n");
			if(test_flag(&ss->flags, flagServerClose)){
				goto err;
			}else{
				closeProxy(ss);
				return ;
			}
		}
	}

	if(!test_flag(&ss->flags, flagConfirmed)){
		return ;
	}

	if(!ss->client.recvBufUsed){
		return ;
	}

	ret=tcpSend(ss->proxy.fd, ss->client.recvBuf, ss->client.recvBufUsed);
	if(ret==-1){
		Printf("send failed:%s\n",strerror(errno));
		goto err;
	}

	//Printf("send %d bytes\n",ret);
	ss->client.recvBufUsed-=ret;
	if(ss->client.recvBufUsed){
		if(ret){
			memmove(ss->client.recvBuf,ss->client.recvBuf+ret,ss->client.recvBufUsed);
		}
		pollOut(&client.poll, &ss->proxy, 1);
	}else{
		if(test_flag(&ss->flags, flagDestroy)){
			goto err;
		}
		pollOut(&client.poll, &ss->proxy, 0);
	}
	
	return ;
err:
	sessionDestroy(ss);
}

int connectProxyServer(Session *ss)
{
	struct sockaddr_in addr={0};

	addr.sin_family=AF_INET;
	addr.sin_addr.s_addr=client.serverIp;
	addr.sin_port=htons(client.serverPort);
	
	ss->proxy.fd=socket(AF_INET, SOCK_STREAM, 0);
	if(ss->proxy.fd<=0){
		Printf("socket:%s\n",strerror(errno));
		goto err;
	}

	setNonblock(ss->proxy.fd);
	
	if(-1==connect(ss->proxy.fd,(struct sockaddr*)&addr,sizeof(addr))){
		if(errno!=EINPROGRESS){
			Printf("connect failed:%s\n",strerror(errno));
			goto err;
		}
	}else{
		if(proxyConnected(ss)){
			goto err;
		}
	}

	if(pollAdd(&client.poll, &ss->proxy)){
		goto err;
	}
	if(!test_flag(&ss->flags, flagProxyConnected)){
		pollOut(&client.poll, &ss->proxy, 1);
	}

	ss->proxy.data=(unsigned long)ss;
	ss->proxy.in=recvProxyData;
	ss->proxy.out=sendProxyData;
	ss->proxy.err=proxySockError;
	
	return 0;
err:
	return -1;
}


void recvClientData(unsigned long data)
{
	int len;
	Session *ss=(void *)data;

	if(bufMaxSize==ss->client.recvBufUsed){
		return ;
	}
	len=tcpRecv(ss->client.fd,ss->client.recvBuf+ss->client.recvBufUsed,bufMaxSize-ss->client.recvBufUsed);
	if(len==0){
		return ;
	}else if(len==-1){
		if(errno){
			Printf("recv failed:%s\n",strerror(errno));
		}
		goto err;
	}
	//Printf("recv %d bytes\n",len);
	if(test_flag(&ss->flags, flagUsedProxy)){
		if(ss->sec){
			proxyCrypt(ss->sec, ss->client.recvBuf+ss->client.recvBufUsed, len);
		}
	}
	ss->client.recvBufUsed+=len;

	if(!test_flag(&ss->flags, flagDataParsed)){
		if(proxyDataParsing(ss)){
			goto err;
		}
	}
	
	if(test_flag(&ss->flags, flagDataParsed)){
		if(!test_and_set_flag(&ss->flags, flagServerDnsParsing)){
			if(client.fast){
				ss->ad=asyncDnsParse(ss->host, (unsigned long)ss, dnsParseSuccess);
				if(!ss->ad){
					Printf("asyncDnsParse failed\n");
					goto err;
				}
			}else{
				closeServer(ss);
			}
			
		}
		if(!test_and_set_flag(&ss->flags, flagProxyConnecting)){
			if(connectProxyServer(ss)){
				Printf("connectProxyServer failed\n");
				goto err;
			}
		}
	}

	if(test_flag(&ss->flags, flagConfirmed)){
		if(test_flag(&ss->flags, flagUsedProxy)){
			return sendProxyData((unsigned long)ss);
		}else{
			return sendServerData((unsigned long)ss);
		}
	}

	return ;
	
err:
	sessionDestroy(ss);
}




Session* sessionCreate(int sock)
{
	Session *ss=memMalloc(sizeof(*ss));
	if(!ss){
		Printf("memMalloc failed\n");
		return NULL;
	}

	setNonblock(sock);
	ss->client.fd=sock;

	if(pollAdd(&client.poll, &ss->client)){
		memFree(ss);
		return NULL;
	}

	ss->client.data=(unsigned long)ss;
	ss->client.in=recvClientData;
	ss->client.out=sendClientData;

	setup_timer(&ss->timer, sessionTimeout, (unsigned long)ss);
	mod_timer(&ss->timer, jiffies+sessionTime*HZ);
	
	return ss;
}

void clientListener(unsigned long data)
{
	int newSock;
	socklen_t len=0;
	struct sockaddr_in addr;
	int sock=(int)data;

	while(1){
		len = sizeof(struct sockaddr_in);
		newSock = accept(sock, (struct sockaddr *) &addr, &len);
		if(newSock < 0){
			break;
		}else{
			Session *ss;
			//Printf("Client %s:%d\n",ipstr(addr.sin_addr.s_addr),ntohs(addr.sin_port));
			ss=sessionCreate(newSock);
			if(!ss){
				Printf("sessionCreate failed\n");
				close(newSock);
			}
		}
	}
}


int clientListenInit(void)
{
	struct sockaddr_in addr;
	
	client.listen.fd=socket(AF_INET,SOCK_STREAM,0);
	if(client.listen.fd==-1){
		perror("socket");
		return -1;
	}

	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(client.clientPort);
	addr.sin_addr.s_addr = INADDR_ANY;

	setReuseaddr(client.listen.fd);
	if(bind(client.listen.fd, (struct sockaddr *)&addr, sizeof(struct sockaddr))== -1){
		Printf("bind:%s\n",strerror(errno));
		return -1;
	}

	if(setNonblock(client.listen.fd) < 0){
		Printf("setNonblock falied\n");
		goto out;
	}

	if(listen(client.listen.fd, SOMAXCONN)==-1){
		Printf("bind:%s\n",strerror(errno));
		goto out;
	}
	
	if(pollAdd(&client.poll, &client.listen)){
		goto out;
	}
	
	client.listen.data=(unsigned long)client.listen.fd;
	client.listen.in=clientListener;
	
	return 0;
	
out:
	close(client.listen.fd);
	return -1;
}

void signalHandler(int sig)
{
	int i;
	Printf("signal=%d\n",sig);

	for(i=0;i<client.poll.used;i++){
		if(client.poll.array[i].fd!=-1){
			if(client.poll.array[i].fd!=client.listen.fd){
				close(client.poll.array[i].fd);
			}
		}
	}
	close(client.listen.fd);
	
	exit(0);
}

void initSignalHandler(void)
{
	struct sigaction sa;

	memset(&sa,0,sizeof(sa));
	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, 0); 	//忽略管道破裂信号
	
	memset(&sa,0,sizeof(sa));
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, 0); 	//忽略子进程退出信号
	
	signal(SIGHUP, SIG_IGN);		//忽略终端退出信号
	
	signal(SIGINT, signalHandler);	//Ctrl+C
	signal(SIGTERM, signalHandler); //kill
	signal(SIGSEGV, signalHandler); //段错误
	signal(SIGBUS, signalHandler);	//总线错误
	signal(SIGABRT, signalHandler); //Abort

}


void Fprintf(const char *fun, int line, const char *fmt, ...)
{
	va_list ap;
	char msg[1024]={0};
	
	if(!client.debug){
		return ;
	}
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	fprintf(stdout,"[%s:%d]:%s",fun,line,msg);
	fflush(stdout);
}


int clientInit(int argc, char **argv)
{
	srand(time(NULL));
	memset(&client,0,sizeof(client));
	initSignalHandler();
	pollInit(&client.poll);
	jiffies_init();
	init_timers_cpu();
	
	if(argc<5){
		printf("Used: %s clientPort serverIp serverPort fast\n",argv[0]);
		return -1;
	}

	if(argc==6){
		client.debug=atoi(argv[5]);
	}

	client.clientPort=atoi(argv[1]);
	client.serverIp=StrToIp(argv[2]);
	client.serverPort=atoi(argv[3]);
	client.fast=atoi(argv[4]);

	return 0;
}

int clientStart(int argc, char **argv)
{
	if(clientInit(argc,argv)){
		return -1;
	}

	if(clientListenInit()){
		return -1;
	}

	asyncDnsInit(StrToIp("114.114.114.114"),&client.poll);

	while(1){
		pollRun(&client.poll,10);
		run_timers();
	}

	return 0;
}

