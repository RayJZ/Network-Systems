//
// Created by Oliver Doig on 3/10/23.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <signal.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <time.h>
#include <pthread.h>
#include <curl/curl.h>

#define BUFSIZE 8192
#define MAXCHAR 65535
int timeout;

unsigned long hash(char *str)
{
    unsigned long hash = 5381;
    int c;
    while((c= *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}
void err(int socket_descriptor, char* error)
{
    char error_msg[BUFSIZE];
    memset(error_msg,0,BUFSIZE);
    strcpy(error_msg,error);
    write(socket_descriptor,error_msg,BUFSIZE);
}

struct MemoryStruct {
    char *memory;
    size_t size;
};

size_t header_callback(char *buffer, size_t size, size_t nitems, void *userdata)
{
    //struct MemoryStruct *data = (struct MemoryStruct*) userdata;
    //return nitems*size;

    size_t realsize = size * nitems;
    struct MemoryStruct *mem = (struct MemoryStruct *)userdata;
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(!ptr) {
        /* out of memory! */
        fprintf(stderr, "not enough memory (realloc returned NULL)\n");
        return 0;
    }
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), buffer, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

//https://curl.se/libcurl/c/getinmemory.html
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(!ptr) {
        /* out of memory! */
        fprintf(stderr, "not enough memory (realloc returned NULL)\n");
        return 0;
    }
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

void handle_request(int socket_descriptor, CURL* curl, CURLcode res, int client_socket_descriptor, char* url)
{
    fprintf(stderr,"handling request %d!\n",client_socket_descriptor);

    unsigned long hash_value = hash(url);
    fprintf(stderr,"hash value: %lu\n",hash_value);
    const int hash_length = snprintf(NULL, 0, "%lu",hash_value);
    char hash_string[hash_length+1];
    snprintf(hash_string,hash_length+1,"%lu",hash_value);
    char path[hash_length + strlen("./cache/")];
    strcpy(path,"./cache/");
    strcat(path,hash_string);

    if(access(path,F_OK)==0)
    {
        fprintf(stderr, "file exists\n");
        //file exists
        struct stat filestat;
        stat(path, &filestat);
        time_t last_modified = filestat.st_mtime;
        time_t now = time(0);
        if(now-last_modified<timeout)
        {
            fprintf(stderr, "found file in cache, time since last accessed: %ld\n", now-last_modified);
            //hasn't expired. proceed with reading file and sending.
            //file should exist if we make it this far
            FILE *file = fopen(path, "rb");

            //get filesize
            fseek(file, 0, SEEK_END);
            long file_size = ftell(file);
            fseek(file, 0, SEEK_SET);
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
            return;
        }
        else
            fprintf(stderr, "file exists, but doesn't meet timeout.\n");
    }
        struct MemoryStruct chunk;
        chunk.memory = malloc(1);  /* will be grown as needed by the realloc above */
        chunk.size = 0;

        struct MemoryStruct header_chunk;
        header_chunk.memory = malloc(1);  /* will be grown as needed by the realloc above */
        header_chunk.size = 0;

        //fprintf(stderr,"2. hostname: %s\nfile: %s\nport: %s\n",hostname,filename,port);
        fprintf(stderr, "url: %s\n",url);
        curl = curl_easy_init();
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)&header_chunk);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        //curl_easy_setopt(curl, CURLOPT_PORT, port);

        char ERRBUF[CURL_ERROR_SIZE];
        ERRBUF[0]=0;

        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, ERRBUF);
        res = curl_easy_perform(curl);

        if(res != CURLE_OK)
        {
            size_t len = strlen(ERRBUF);
            if(len)
                fprintf(stderr, "%s%s", ERRBUF, (ERRBUF[len-1]!='\n')?"\n":"");
            else
                fprintf(stderr, "%s\n", curl_easy_strerror(res));
            fprintf(stderr,"Host unreachable\n");
            err(client_socket_descriptor,"HTTP/1.0 404 Not Found\r\nContent-Type:text/plain\r\nContent-length:0\r\n\r\n");
            close(client_socket_descriptor);
            curl_easy_cleanup(curl);
            free(chunk.memory);
            return;
        }
        else
        {
            fprintf(stderr,"%lu header bytes retrieved:\n", (unsigned long)header_chunk.size);
            //fprintf(stderr,"header:\n%s\n", header_chunk.memory);

            fprintf(stderr,"%lu bytes retrieved:\n", (unsigned long)chunk.size);
            //fprintf(stderr,"%s\n", chunk.memory);

            /*
             * caching
             */
            if(strchr(url,'?')==NULL) //there is no ?, implying not dynamic.
            {
                fprintf(stderr, "chunk size: %zu\n", chunk.size);
                fprintf(stderr, "caching hash: %s\n",hash_string);
                fprintf(stderr,"path: %s\n",path);
                FILE *new_file = fopen(path, "wb");
                if(!new_file)
                    perror("fopen");
                fwrite(header_chunk.memory,header_chunk.size,1,new_file);
                fwrite(chunk.memory, chunk.size, 1, new_file);
                fclose(new_file);
            }

            /*
             * send response
             */

            fprintf(stderr, "sending response\n");
            //send header
            write(socket_descriptor,header_chunk.memory,header_chunk.size);
            write(socket_descriptor,chunk.memory,chunk.size);
            fprintf(stderr,"Finished Sending\n");
        }

        curl_easy_cleanup(curl);
        free(chunk.memory);
}

//the thread's process.
void *thread(void *arg)
{
    CURL *curl;
    CURLcode res;
    fprintf(stderr,"\n\n");
    //fprintf(stderr,"thread() entered with argument '%d'\n", *(int*) arg);
    int client_socket_descriptor = *((int *) arg);
    char buf[BUFSIZE];
    char error[BUFSIZE];
    memset(buf,0,BUFSIZE);
    memset(error,0,BUFSIZE);

    //read from socket
    ssize_t read_success = read(client_socket_descriptor,buf,BUFSIZE);
    if(read_success<0)
    {
        fprintf(stderr,"Socket read failed.\n");
        return NULL;
    }

    //fprintf(stderr,"%s\n",buf);

    //parse HTTP header up to carriage return
    char* request_method = strtok(buf," ");
    char* request_URI = strtok(NULL, " ");
    char* request_version = strtok(NULL, "\r");

    //parse hostname and filename

    fprintf(stderr,"URI: %s\n",request_URI);

    char *request_host, *request_file, *request_port;

    //strip URI of http prefix
    //fprintf(stderr, "a\n");
    char* stripped_URI = request_URI;
    //fprintf(stderr, "stripped_URI: %s\n",stripped_URI);
    char* has_http = strstr(request_URI,"http://");
    //fprintf(stderr, "c\n");
    char* has_https = strstr(request_URI,"https://");
    //fprintf(stderr, "has_http: %s, has_https: %s\n",has_http,has_https);
    if(has_http)
        stripped_URI=has_http+7;
    else if(has_https)
        stripped_URI=has_https+8;
    //fprintf(stderr, "e");


    //fprintf(stderr, "has_http: %s\n", has_http);
    fprintf(stderr, "stripped_URI: %s\n",stripped_URI);

    //port specified and path specified
    if(strchr(stripped_URI,':') && strchr(stripped_URI,'/'))
    {
        request_host = strtok(stripped_URI, ":");
        request_port = strtok(NULL,"/");
        request_file = strtok(NULL,"\r");
    }
    //only port, no path
    else if(strchr(stripped_URI,':'))
    {
        request_host = strtok(stripped_URI, ":");
        request_port = strtok(NULL,"\r");
        request_file = "";
    }
    //only path, no port
    else if(strchr(stripped_URI,'/'))
    {
        request_host = strtok(stripped_URI, "/");
        request_file = strtok(NULL,"\r");
        request_port = "80";
    }
    //no path, no port
    else
    {
        request_host = strtok(stripped_URI,"\r");
        request_port = "80";
        request_file = "";
    }

    if(request_file==NULL)
    {
        request_file="";
    }
    fprintf(stderr,"1. host: %s\nfile: %s\nport: %s\n",request_host,request_file,request_port);

    FILE * blocklist;
    blocklist = fopen("blocklist", "r");
    if(!blocklist)
    {
        fprintf(stderr, "blocklist file does not exist. creating.\n");
        FILE* temp_file = fopen("./blocklist", "w");
        fclose(temp_file);
    }
    char line[2048];
    while(fgets(line, sizeof(line),blocklist))
    {
        //is in blocklist
        line[strcspn(line,"\n")] = 0;
        //fprintf(stderr, "line: %s, request_host: %s\n", line, request_host);
        if(strcmp(line,request_host)==0)
        {
            fprintf(stderr,"403 Blocklist\n");
            err(client_socket_descriptor,"HTTP/1.0 403 Forbidden\r\nContent-Type:text/plain\r\nContent-length:0\r\n\r\n");
            close(client_socket_descriptor);
            return NULL;
        }
    }
    fclose(blocklist);
    fprintf(stderr, "url not in blocklist\n");

    //test for malformed/invalid/400
    if(!request_method || !request_URI || !request_version)
    {
        fprintf(stderr,"The request could not be parsed or is malformed\n");
        err(client_socket_descriptor,"HTTP/1.0 400 Bad Request\r\nContent-Type:text/plain\r\nContent-length:0\r\n\r\n");
        close(client_socket_descriptor);
        return NULL;
    }

    //test if method is allowed/405
    if(strcmp(request_method,"GET")!=0)
    {
        fprintf(stderr,"A method other than GET was requested\n");
        err(client_socket_descriptor,"HTTP/1.0 405 Method Not Allowed\r\nContent-Type:text/plain\r\nContent-length:0\r\n\r\n");
        close(client_socket_descriptor);
        return NULL;
    }

    //test for host not reachable
    if(gethostbyname(request_host)==NULL)
    {
        fprintf(stderr,"Host unreachable\n");
        err(client_socket_descriptor,"HTTP/1.0 404 Not Found\r\nContent-Type:text/plain\r\nContent-length:0\r\n\r\n");
        close(client_socket_descriptor);
        return NULL;
    }

    //test for valid http version/505
    if(strcmp(request_version,"HTTP/1.0")!=0 && strcmp(request_version,"HTTP/1.1")!=0)
    {
        fprintf(stderr,"An HTTP version other than 1.0 or 1.1 was requested\n");
        err(client_socket_descriptor,"HTTP/1.0 505 HTTP Version Not Supported\r\nContent-Type:text/plain\r\nContent-length:0\r\n\r\n");
        close(client_socket_descriptor);
        return NULL;
    }

    if(request_file==NULL)
        request_file="";
    fprintf(stderr,"request_host: %s, request_file: %s\n",request_host,request_file);
    char url[strlen(request_host) + strlen(request_file)+1];
    strcpy(url, request_host);
    strcat(url, ":");
    strcat(url, request_port);
    strcat(url, "/");
    strcat(url, request_file);

    //request is good!/200
    handle_request(client_socket_descriptor, curl, res, client_socket_descriptor,url);

    close(client_socket_descriptor);
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
//    printf("%d", gethostbyname("majesticyoungfreshmelody.neverssl.com"));
    signal(SIGPIPE, SIG_IGN);
    curl_global_init(CURL_GLOBAL_ALL);
    //fprintf(stderr,"libcurl version %s\n",curl_version());
    int portno; /* port to listen on */
    //int timeout; /* cache timeout */
    struct sockaddr_in clientaddr; /* client addr */
    int address_len=sizeof(clientaddr);
    pthread_t thid;

    int result = mkdir("./cache", 0777);
    if(result!=-1)
        fprintf(stderr, "./cache didn't exist. creating.\n");
    //fprintf(stderr, "%d\n",result);
    //check command line args
    if (argc != 3) {
        fprintf(stderr, "usage: %s <port> <timeout>\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);
    timeout = atoi(argv[2]);

    fprintf(stderr, "Server running on port [%d], timeout [%d]\n",portno,timeout);

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
    curl_global_cleanup();
}