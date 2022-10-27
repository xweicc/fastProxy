vpath %.c source

OBJC  = client.o pollopt.o timer.o async_dns.o
OBJC := $(foreach n,$(OBJC), buildc/$(n))

OBJS  = server.o pollopt.o timer.o async_dns.o
OBJS := $(foreach n,$(OBJS), builds/$(n))

CFLAGS += -I./include -Wall

ifeq ($(CC),)
CC=gcc
endif

all: clean client server

client: $(OBJC)
	@echo "[client] LD -o $@"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o proxyClient $^

server: $(OBJS)
	@echo "[server] LD -o $@"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o proxyServer $^

clean:
	rm -rf proxyClient proxyServer buildc builds

buildc/%.o:%.c buildc
	@echo "[client] CC -o $@"
	@$(CC) -o $@ $(CFLAGS) -c $<
buildc:
	mkdir buildc

builds/%.o:%.c builds
	@echo "[server] CC -o $@"
	@$(CC) -o $@ $(CFLAGS) -c $<
builds:
	mkdir builds
