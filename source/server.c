#include "server.h"

struct proxyServer server;

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
		pollDelete(&server.poll, &ss->client);
	}

	if(ss->server.fd>0){
		close(ss->server.fd);
		pollDelete(&server.poll, &ss->server);
	}

	if(ss->ad){
		asyncDnsFree(ss->ad);
	}

	del_timer(&ss->timer);

	memFree(ss);
}

void closeServer(Session *ss)
{
	if(ss->server.fd>0){
		close(ss->server.fd);
		pollDelete(&server.poll, &ss->server);
		ss->server.fd=-1;
	}
}


int serverConnected(Session *ss)
{
	char buf[1024]={0};
	proxyMsgHead *head=(void*)buf;
	proxyMsgReply *msg=(void*)head->data;

	strncpy(head->http,httpOK,sizeof(head->http));
	head->sec=ss->sec;
	head->type=msgTypeReply;
	head->len=htons(sizeof(*msg)+sizeof(*head));
	head->magic=htonl(msgMagic);

	msg->connect=1;
	
	proxyCrypt(head->sec, msg, sizeof(*msg));
	
	if(tcpSend(ss->client.fd, head, sizeof(*head)+sizeof(*msg))==-1){
		Printf("send failed:%s\n",strerror(errno));
		return -1;
	}

	set_flag(&ss->flags, flagServerConnected);

	return 0;
}



void sendClientData(unsigned long data)
{
	int ret;
	Session *ss=(void *)data;
	
	if(!ss->server.recvBufUsed){
		pollOut(&server.poll, &ss->client, 0);
		return ;
	}
	
	ret=tcpSend(ss->client.fd, ss->server.recvBuf, ss->server.recvBufUsed);
	if(ret==-1){
		Printf("send failed:%s\n",strerror(errno));
		goto err;
	}

	//Printf("send %d bytes\n",ret);

	ss->server.recvBufUsed-=ret;
	if(ss->server.recvBufUsed){
		if(ret){
			memmove(ss->server.recvBuf,ss->server.recvBuf+ret,ss->server.recvBufUsed);
		}
		pollOut(&server.poll, &ss->client, 1);
	}else{
		if(test_flag(&ss->flags, flagDestroy)){
			goto err;
		}
		pollOut(&server.poll, &ss->client, 0);
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
			goto err;
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
			goto err;
		}
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
		pollOut(&server.poll, &ss->server, 1);
	}else{
		pollOut(&server.poll, &ss->server, 0);
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
			goto err;
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
	if(ss->sec){
		proxyCrypt(ss->sec, ss->server.recvBuf+ss->server.recvBufUsed, len);
	}
	ss->server.recvBufUsed+=len;
	
	return sendClientData((unsigned long)ss);
	
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

	if(pollAdd(&server.poll, &ss->server)){
		goto err;
	}
	if(!test_flag(&ss->flags, flagServerConnected)){
		pollOut(&server.poll, &ss->server, 1);
	}

	ss->server.data=(unsigned long)ss;
	ss->server.in=recvServerData;
	ss->server.out=sendServerData;
	ss->server.err=serverSockError;
	
	return ;
err:
	sessionDestroy(ss);
}

int recvClientMsg(Session *ss)
{
	proxyMsgHead *head=(void*)ss->client.recvBuf;
	proxyMsgConnect *msg=(void*)head->data;

	if(ss->client.recvBufUsed<sizeof(*head)){
		Printf("recvBufUsed:%d\n",ss->client.recvBufUsed);
		return 0;
	}
	
	if(ntohl(head->magic)!=msgMagic){
		Printf("magic:%08X error\n",ntohl(head->magic));
		return -1;
	}

	if(ntohs(head->len)>ss->client.recvBufUsed){
		Printf("len:%d recvBufUsed:%d\n",ntohs(head->len),ss->client.recvBufUsed);
		return 0;
	}

	if(head->type!=msgTypeConnect){
		Printf("head->type:%d\n",head->type);
		return -1;
	}

	ss->sec=head->sec;
	proxyCrypt(head->sec, msg, sizeof(*msg));

	strncpy(ss->host,msg->host,sizeof(ss->host));
	ss->port=ntohs(msg->port);
	ss->sockType=msg->sockType;
	ss->client.recvBufUsed-=ntohs(head->len);

	//Printf("host:%s\n",ss->host);
	set_flag(&ss->flags, flagDataParsed);

	return 0;
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
	if(ss->sec){
		proxyCrypt(ss->sec, ss->client.recvBuf+ss->client.recvBufUsed, len);
	}
	ss->client.recvBufUsed+=len;

	if(!test_flag(&ss->flags, flagDataParsed)){
		if(recvClientMsg(ss)){
			goto err;
		}
	}
	
	if(test_flag(&ss->flags, flagDataParsed) && !test_and_set_flag(&ss->flags, flagServerDnsParsing)){
		ss->ad=asyncDnsParse(ss->host, (unsigned long)ss, dnsParseSuccess);
		if(!ss->ad){
			Printf("asyncDnsParse failed\n");
			goto err;
		}
	}

	if(test_flag(&ss->flags, flagServerConnected)){
		return sendServerData((unsigned long)ss);
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

	if(pollAdd(&server.poll, &ss->client)){
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

void serverListener(unsigned long data)
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
			Printf("Client %s:%d\n",ipstr(addr.sin_addr.s_addr),ntohs(addr.sin_port));
			ss=sessionCreate(newSock);
			if(!ss){
				Printf("sessionCreate failed\n");
				close(newSock);
			}
		}
	}
}


int serverListenInit(void)
{
	struct sockaddr_in addr;
	
	server.listen.fd=socket(AF_INET,SOCK_STREAM,0);
	if(server.listen.fd==-1){
		perror("socket");
		return -1;
	}

	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(server.serverPort);
	addr.sin_addr.s_addr = INADDR_ANY;

	setReuseaddr(server.listen.fd);
	if(bind(server.listen.fd, (struct sockaddr *)&addr, sizeof(struct sockaddr))== -1){
		Printf("bind:%s\n",strerror(errno));
		return -1;
	}

	if(setNonblock(server.listen.fd) < 0){
		Printf("setNonblock falied\n");
		goto out;
	}

	if(listen(server.listen.fd, SOMAXCONN)==-1){
		Printf("bind:%s\n",strerror(errno));
		goto out;
	}
	
	if(pollAdd(&server.poll, &server.listen)){
		goto out;
	}
	
	server.listen.data=(unsigned long)server.listen.fd;
	server.listen.in=serverListener;
	
	return 0;
	
out:
	close(server.listen.fd);
	return -1;
}

void signalHandler(int sig)
{
	int i;
	Printf("signal=%d\n",sig);

	for(i=0;i<server.poll.used;i++){
		if(server.poll.array[i].fd!=-1){
			if(server.poll.array[i].fd!=server.listen.fd){
				close(server.poll.array[i].fd);
			}
		}
	}
	close(server.listen.fd);
	
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
	
	if(!server.debug){
		return ;
	}
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	fprintf(stdout,"[%s:%d]:%s",fun,line,msg);
	fflush(stdout);
}



int serverInit(int argc, char **argv)
{
	srand(time(NULL));
	memset(&server,0,sizeof(server));
	initSignalHandler();
	pollInit(&server.poll);
	jiffies_init();
	init_timers_cpu();
	
	if(argc<2){
		printf("Used: %s serverPort \n",argv[0]);
		return -1;
	}

	if(argc==3){
		server.debug=atoi(argv[2]);
	}

	server.serverPort=atoi(argv[1]);

	return 0;
}

int serverStart(int argc, char **argv)
{
	if(serverInit(argc,argv)){
		return -1;
	}

	if(serverListenInit()){
		return -1;
	}

	asyncDnsInit(StrToIp("8.8.8.8"),&server.poll);

	while(1){
		pollRun(&server.poll,10);
		run_timers();
	}

	return 0;
}

