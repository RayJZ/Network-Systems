GCC = gcc
GCCFLAGS = -Wall -Wextra

all: tcp_server

clean:
	rm -f server

tcp_server: tcp_server.c
	$(GCC) $(GCCFLAGS) -o server tcp_server.c