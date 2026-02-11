GCC = gcc
GCCFLAGS = -Wall -Wextra

all: udp_client udp_server

clean:
	rm -f server client

udp_client: client_files/udp_client.c
	$(GCC) $(GCCFLAGS) -o client_files/client client_files/udp_client.c

udp_server: server_files/udp_server.c
	$(GCC) $(GCCFLAGS) -o server_files/server server_files/udp_server.c

