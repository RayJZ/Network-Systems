//
// Created by Oliver Doig on 4/18/23.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <time.h>
#include <pthread.h>

#define BUFSIZE 8192
#define MAXCHAR 65535
char* cache_directory;

unsigned long hash(char *str)
{
    unsigned long hash = 5381;
    int c;
    while((c= *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

void handle_request(int client_socket_descriptor, char* request)
{
    fprintf(stderr,"handling request %d!\n",client_socket_descriptor);
    fprintf(stderr,"request: %s\n",request);
    char buf[BUFSIZE];
    char *command;

    memset(buf,0,sizeof(buf));
    ssize_t read_success = read(client_socket_descriptor,buf,BUFSIZE);
    if(read_success<0)
    {
        fprintf(stderr, "Socket read failed.\n");
        return;
    }

    command = strtok(request, " ");
    char* filename = strtok(NULL, " ");
    int chunk_size = atoi(strtok(NULL, "\r"));

    char path[strlen(cache_directory)+strlen(filename)+1];
    strcpy(path,cache_directory);
    strcat(path,"/");
    strcat(path,filename);

    fprintf(stderr,"handling command: %s, filename: %s, chunk_size: %d\n", command,filename,chunk_size);
    //handle command
    if(strcmp(command,"put")==0)
    {
        fprintf(stderr, "putting\n");
        long bytes_read = 0;

        FILE * file = fopen(path,"wb+");
        while(bytes_read<chunk_size)
        {
            memset(buf,0,sizeof(buf));
            read_success = read(client_socket_descriptor,buf,BUFSIZE);
            if(read_success<0)
            {
                fprintf(stderr, "Socket read failed.\n");
                return;
            }
            bytes_read+=read_success;
            fwrite(buf,1,strlen(buf),file);
            fprintf(stderr, "bytes_read: %ld, read_success: %zd\n",bytes_read,read_success);
        }
        fprintf(stderr,"f\n");
        fclose(file);
    }
    else if(strcmp(command,"get")==0)
    {

    }
    else if(strcmp(command,"list")==0)
    {

    }
}

//the thread's process.
void *thread(void *arg)
{
    fprintf(stderr,"\n\n");
    int client_socket_descriptor = *((int *) arg);
    char buf[BUFSIZE];
    memset(buf,0,BUFSIZE);

    //read from socket
    ssize_t read_success = read(client_socket_descriptor,buf,BUFSIZE);
    if(read_success<0)
    {
        fprintf(stderr, "Socket read failed.\n");
        close(client_socket_descriptor);
        return NULL;
    }

    //fprintf(stderr,"buf: %s, strlen(buf): %lu\n",buf, strlen(buf));
    if(strlen(buf)==0)
    {
        fprintf(stderr, "Empty packet.\n");
        close(client_socket_descriptor);
        return NULL;
    }

    if(strncmp(buf,"list",4)==0 || strncmp(buf,"get",3)==0 || strncmp(buf,"put",3)==0)
    {
        handle_request(client_socket_descriptor,buf);
    }

    if(strncmp(buf,"get",3)==0 || strncmp(buf,"put",3)==0) //get second chunk
    {
        read_success = read(client_socket_descriptor,buf,BUFSIZE);
        if(read_success<0)
        {
            fprintf(stderr, "Socket read failed.\n");
            close(client_socket_descriptor);
            return NULL;
        }
        handle_request(client_socket_descriptor,buf);
    }

    close(client_socket_descriptor);
    //handle_request(client_socket_descriptor, curl, res, client_socket_descriptor,url,request_port, request_host, request_file,request_version);
    return NULL;
}

//generates a socket on port 'portno'
//cited the IBM documentation for socket.h
//they had a lot of incredibly useful examples
int open_socket(int portno)
{
    int option_value = 1;

    struct sockaddr_in socket_address;
    memset(&socket_address, 0, sizeof(socket_address));
    socket_address.sin_family = AF_INET; //for Internet
    socket_address.sin_port = htons(portno);
    socket_address.sin_addr.s_addr = INADDR_ANY; //binds to all network interfaces in the internet domain.

    int socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    int setsockopt_success = setsockopt(socket_descriptor, SOL_SOCKET, SO_REUSEADDR,(char*) &option_value,sizeof(int));
    int bind_success = bind(socket_descriptor, (struct sockaddr*) &socket_address, sizeof(socket_address));
    int listen_success = listen(socket_descriptor, BUFSIZE);

    if(socket_descriptor<0 || setsockopt_success<0 || bind_success<0 || listen_success<0)
    {
        fprintf(stderr,"Socket Creation Failed.\n");
        return -1;
    }
    return socket_descriptor;
}

int main(int argc, char **argv) {
    int portno; /* port to listen on */
    //int timeout; /* cache timeout */

    portno = atoi(argv[2]);
    cache_directory = argv[1];

    fprintf(stderr, "Server running on port [%d], directory [%s]\n",portno,cache_directory);

    struct sockaddr_in clientaddr; /* client addr */
    int address_len=sizeof(clientaddr);
    pthread_t thid;

    int result = mkdir(cache_directory, 0777);
    if(result!=-1)
        fprintf(stderr, "%s didn't exist. creating. or other error. lol.\n", cache_directory);
    //fprintf(stderr, "%d\n",result);
    //check command line args
    if (argc != 3) {
        fprintf(stderr, "usage: %s <port> <timeout>\n", argv[0]);
        exit(1);
    }

    //open socket
    int socket = open_socket(portno);
    int *client_socket_descriptor;
    //start accepting connections
    while(1)
    {
        //accept connection
        client_socket_descriptor = malloc(sizeof(int));
        *client_socket_descriptor = accept(socket, (struct sockaddr *) &clientaddr, (socklen_t *) &address_len);
        //spawn thread
        if(client_socket_descriptor>=0)
            pthread_create(&thid, NULL, thread, client_socket_descriptor);
        else
            fprintf(stderr,"Failed to accept client.\n");
    }
}