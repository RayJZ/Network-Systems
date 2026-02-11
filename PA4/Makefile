GCC = gcc
GCCFLAGS = -Wall -Wextra

all: dfs_server dfs_client

clean:
	rm -f dfs dfc

dfs_server: distributed_file_server.c
	$(GCC) $(GCCFLAGS) -o dfs distributed_file_server.c

dfs_client: distributed_file_client.c
	$(GCC) $(GCCFLAGS) -o dfc distributed_file_client.c