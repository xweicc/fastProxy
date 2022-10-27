
#include "pollopt.h"

void pollInit(struct Poll *This)
{
	int i=0;

	memset(This, 0, sizeof(*This));
	
	for(i=0;i<pollMaxNum;i++){
		This->array[i].fd=-1;
	}
	This->used=0;

	for(i=0;i<=pollMask;i++){
		INIT_HLIST_HEAD(&This->hhead[i]);
	}
}

void pollOut(struct Poll *This, struct Socket *sock, int opt)
{
	if(opt){
		This->array[sock->pollId].events |= POLLOUT;
	}else{
		This->array[sock->pollId].events &= ~POLLOUT;
	}
}


int pollAdd(struct Poll *This, struct Socket *sock)
{
	int i;
	
	for(i=0;i<pollMaxNum;i++){
		if(This->array[i].fd==-1){
			This->array[i].fd=sock->fd;
			break;
		}
	}
	if(i==pollMaxNum){
		Printf("Add poll falied. fd:%d\n",sock->fd);
		return -1;
	}
	This->array[i].events = POLLIN;
	if(i>=This->used){
		This->used=i+1;
	}

	sock->pollId=i;
	hlist_add_head(&sock->hlist, &This->hhead[sock->fd&pollMask]);

	return 0;
}

void pollDelete(struct Poll *This, struct Socket *sock)
{
	int i=0;
	int thisUsed=0;
	
	for(i=0;i<This->used&&i<pollMaxNum;i++){
		if(This->array[i].fd==sock->fd){
			This->array[i].fd=-1;
		}
		if(This->array[i].fd!=-1){
			thisUsed=i+1;
		}
	}
	This->used=thisUsed;

	hlist_del(&sock->hlist);
}

void pollRun(struct Poll *This, int timeout)
{
	int i,nready;
	
	nready=poll(This->array,This->used, timeout);
	if(nready < 1){
		return ;
	}

	for(i=0;i<This->used;i++){
		if(This->array[i].fd<0){
			continue;
		}
		if(This->array[i].revents & (POLLIN | POLLOUT | POLLERR)){
			struct Socket *sock=SocketFind(This, This->array[i].fd);
			if(sock){
				if(This->array[i].revents & POLLIN){
					if(sock->in){
						sock->in(sock->data);
					}
				}else if(This->array[i].revents & POLLOUT){
					if(sock->out){
						sock->out(sock->data);
					}
				}else{
					if(sock->err){
						sock->err(sock->data);
					}
				}
			}else{
				Printf("SocketFind failed. fd:%d\n",This->array[i].fd);
				exit(1);
			}
			if(--nready<=0){
				break;
			}
		}
	}
}

struct Socket *SocketFind(struct Poll *This, int fd)
{
	struct Socket *sock;
	struct hlist_node *pos;
	hlist_for_each_entry(sock, pos, &This->hhead[fd&pollMask], hlist){
		if(sock->fd==fd){
			return sock;
		}
	}
	return NULL;
}

int setNonblock(int sock)
{
	return fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0)|O_NONBLOCK);
}

int setReuseaddr(int sock)
{
	int optval = 1;
	return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(optval));
}

