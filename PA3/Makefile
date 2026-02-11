GCC = gcc
GCCFLAGS = -Wall -Wextra

all: proxy_server

clean:
	rm -f proxy

proxy_server: proxy_server.c
	$(GCC) $(GCCFLAGS) -o proxy proxy_server.c -lcurl