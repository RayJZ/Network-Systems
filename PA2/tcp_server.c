//
// Created by Oliver Doig on 3/10/23.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define BUFSIZE 8192
#define MAXCHAR 65535

void err(int socket_descriptor, char* error)
{
    char error_msg[BUFSIZE];
    memset(error_msg,0,BUFSIZE);
    strcpy(error_msg,error);
    write(socket_descriptor,error_msg,BUFSIZE);
}

int parse_content_type(char* content_type, char* extension)
{
    if(strcmp(extension,".html")==0)
        strcpy(content_type,"text/html");
    else if(strcmp(extension,".txt")==0)
        strcpy(content_type,"text/plain");
    else if(strcmp(extension,".png")==0)
        strcpy(content_type,"image/png");
    else if(strcmp(extension,".gif")==0)
        strcpy(content_type,"image/gif");
    else if(strcmp(extension,".jpg")==0)
        strcpy(content_type,"image/jpg");
    else if(strcmp(extension,".ico")==0)
        strcpy(content_type,"image/x-icon");
    else if(strcmp(extension,".css")==0)
        strcpy(content_type,"text/css");
    else if(strcmp(extension,".js")==0)
        strcpy(content_type,"application/javascript");
    else
        return -1;
    return 0;
}

void handle_request(int socket_descriptor, char* URL, char* HTTP_Version)
{
    char content_type[32];
    char *extension;
    char filename[BUFSIZE];
    memset(content_type,0,32);
    memset(filename,0,BUFSIZE);
    FILE* file;

    //assemble filename
    strcat(filename,"www");
    //handle home page
    if(strcmp(URL,"/")==0)
        strcat(filename,"/index.html");
    else
        strcat(filename,URL);

    fprintf(stderr,"filename: %s\n",filename);
    //check if file doesn't exist/404
    if(access(filename, F_OK) != 0)
    {
        fprintf(stderr,"The requested file cannot be found in the document tree\n");
        err(socket_descriptor,"HTTP/1.0 404 Not Found\r\nContent-Type:text/plain\r\nContent-length:0\r\n\r\n");
        close(socket_descriptor);
        return;
    }

    //test for file permission issue/403
    if(access(filename, R_OK) != 0)
    {
        fprintf(stderr,"The requested file cannot be accessed due to a file permission issue\n");
        err(socket_descriptor,"HTTP/1.0 403 Forbidden\r\nContent-Type:text/plain\r\nContent-length:0\r\n\r\n");
        close(socket_descriptor);
        return;
    }

    //get extension
    extension = strrchr(filename, '.');
    if(parse_content_type(content_type,extension)==-1)
    {
        fprintf(stderr,"Invalid filetype\n");
        close(socket_descriptor);
        return;
    }

    //file should exist if we make it this far
    file = fopen(filename, "rb");

    //get filesize
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    //fprintf(stderr,"1\n");
    char header[BUFSIZE];
    memset(header,0,BUFSIZE);

    sprintf(header,"%s 200 OK\r\nContent-Type:%s\r\nContent-Length:%ld\r\n\r\n",HTTP_Version,content_type,file_size);
    //fprintf(stderr,"\nheader:\n%s",header);

    //send header
    write(socket_descriptor,header,strlen(header));

    //fprintf(stderr,"header len:%lu\nfile_size:%lu\n",strlen(header),file_size);
    unsigned long remaining_bytes = file_size;
    //dispatch chunks
    while(remaining_bytes>0)
    {
        fprintf(stderr,"remaining bytes:%lu\n",remaining_bytes);
        int chunk_size;
        if(remaining_bytes<65535)
            chunk_size=(int)remaining_bytes;
        else
            chunk_size = MAXCHAR%remaining_bytes;
        fprintf(stderr,"chunk size:%d\n",chunk_size);

        char response[chunk_size];
        memset(response,0,chunk_size);

        //fprintf(stderr,"memset success\n");
        fread(response,1,chunk_size,file);
        //fprintf(stderr,"fread success\n");
        write(socket_descriptor,response,chunk_size);
        //fprintf(stderr,"write success\n");
        remaining_bytes-=chunk_size;
    }
    fprintf(stderr,"Finished Sending\n");
}

//the thread's process. handles the client then returns (pthread_close was leaking memory. opted for 'return'.)
void *thread(void *arg)
{
    //fprintf(stderr,"thread() entered with argument '%d'\n", *(int*) arg);
    int socket_descriptor = *((int *) arg);
    char buf[BUFSIZE];
    char error[BUFSIZE];
    memset(buf,0,BUFSIZE);
    memset(error,0,BUFSIZE);

    //read from socket
    ssize_t read_success = read(socket_descriptor,buf,BUFSIZE);
    if(read_success<0)
    {
        fprintf(stderr,"Socket read failed.\n");
        return NULL;
    }

    //fprintf(stderr,"request: %s\n",buf);
    //parse HTTP header up to carriage return
    char* request_method = strtok(buf," ");
    char* request_URL = strtok(NULL, " ");
    char* request_version = strtok(NULL, "\r");

    //test for malformed/invalid/400
    if(!request_method || !request_URL || !request_version)
    {
        fprintf(stderr,"The request could not be parsed or is malformed\n");
        err(socket_descriptor,"HTTP/1.0 400 Bad Request\r\nContent-Type:text/plain\r\nContent-length:0\r\n\r\n");
        close(socket_descriptor);
        return NULL;
    }

    //test if method is allowed/405
    if(strcmp(request_method,"GET")!=0)
    {
        fprintf(stderr,"A method other than GET was requested\n");
        err(socket_descriptor,"HTTP/1.0 405 Method Not Allowed\r\nContent-Type:text/plain\r\nContent-length:0\r\n\r\n");
        close(socket_descriptor);
        return NULL;
    }

    //test for valid http version/505
    if(strcmp(request_version,"HTTP/1.0")!=0 && strcmp(request_version,"HTTP/1.1")!=0)
    {
        fprintf(stderr,"An HTTP version other than 1.0 or 1.1 was requested\n");
        err(socket_descriptor,"HTTP/1.0 505 HTTP Version Not Supported\r\nContent-Type:text/plain\r\nContent-length:0\r\n\r\n");
        close(socket_descriptor);
        return NULL;
    }

    //request is good!/200
    handle_request(socket_descriptor,request_URL,request_version);
    close(socket_descriptor);
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
    struct sockaddr_in clientaddr; /* client addr */
    int address_len=sizeof(clientaddr);
    pthread_t thid;

    //check command line args
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);
    fprintf(stderr, "Server running on port %d\n",portno);

    //open socket
    int socket = open_socket(portno);
    int *socket_descriptor;
    //start accepting connections
    while(1)
    {
        //accept connection
        socket_descriptor = malloc(sizeof(int));
        *socket_descriptor = accept(socket, (struct sockaddr *) &clientaddr, (socklen_t *) &address_len);
        //spawn thread
        if(socket_descriptor>=0)
            pthread_create(&thid, NULL, thread, socket_descriptor);
        else
            fprintf(stderr,"Failed to accept client.\n");
    }
}