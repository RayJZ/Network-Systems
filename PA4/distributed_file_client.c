/*
 * udpclient.c - A simple UDP client
 * usage: udpclient <host> <port>
 */
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <time.h>

#define BUFSIZE 8192
#define MAXCHAR 65535

unsigned long hash(char *str)
{
    unsigned long hash = 5381;
    int c;
    while((c= *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

int put_helper(struct sockaddr_in server_socket_address, long chunk_size, char* path, int chunk_number, int server_number)
{
    char * filename = basename(path);
    char buf[BUFSIZE];

    //connect to server
    int server_socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_descriptor == -1)
    {
        fprintf(stderr,"Socket creation failed\n");
        exit(0);
    }
    else
        fprintf(stderr,"Socket successfully created\n");

    if(connect(server_socket_descriptor, &server_socket_address, sizeof(server_socket_address)) != 0)
    {
        fprintf(stderr,"connection with server #%d failed with errno: %s\n", server_number, strerror(errno));
        return -1;
    }
    else
    {
        fprintf(stderr,"connected to server #%d on socket no. %d\n", server_number, server_socket_descriptor);

        memset(buf,0,BUFSIZE);
        sprintf(buf,"put chunk-%d-%s %ld ",chunk_number,filename,chunk_size);
        fprintf(stderr,"writing to server #%d, buf: \"%s\", strlen(buf) = %lu\n",server_number,buf,strlen(buf));
        write(server_socket_descriptor,buf,strlen(buf));
    }

    usleep(100);

    FILE* file = fopen(path, "rb");
    int file_seekpoint = (chunk_number-1)*(int)chunk_size;
    //dispatch chunks
    fseek(file,file_seekpoint,SEEK_SET);
    long remaining_bytes=chunk_size;
    while(remaining_bytes>0)
    {
        //fprintf(stderr,"remaining bytes:%lu\n",remaining_bytes);
        int packet_size;
        if(remaining_bytes<65535)
            packet_size=(int)remaining_bytes;
        else
            packet_size = MAXCHAR%remaining_bytes;
        //fprintf(stderr,"packet size:%d\n",packet_size);

        char response[packet_size];
        memset(response,0,packet_size);

        size_t amt = fread(response,1,packet_size,file);
        fprintf(stderr,"sending response of size %zu to server #%d.\n\n",amt,server_number);
        ssize_t written_bytes = write(server_socket_descriptor,response,amt);
        if(written_bytes<0)
            fprintf(stderr, "error writing: %s", strerror(errno));
        remaining_bytes-=written_bytes;
        fprintf(stderr,"written_bytes: %zd\n", written_bytes);
        if(chunk_number==4 && written_bytes<packet_size)
            break;
    }
    fclose(file);
    close(server_socket_descriptor);
    return 0;
}

int put(struct sockaddr_in *server_socket_addresses, int num_servers, char* path)
{
    unsigned long hash_value = hash(basename(path));
    int offset = (int)((hash_value)%num_servers);

    //fprintf(stderr,"hash value: %lu\n",hash_value);
    const int hash_length = snprintf(NULL, 0, "%lu",hash_value);
    char hashed_path[hash_length+1];
    snprintf(hashed_path,hash_length+1,"%lu",hash_value);

    FILE* file = fopen(path, "rb");
    if(!file)
    {
        fprintf(stderr, "failed to open file %s\n", path);
        return -1;
    }

    //get filesize
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    fprintf(stderr,"path: %s, file_size: %ld\n", path,file_size);

    long chunk_size = (file_size/num_servers) + 1;

    fclose(file);

    for(int i=0;i<num_servers;i++)
    {
        int server_number=i+1;
        fprintf(stderr,"\nDispatching chunks to server %d.\n",server_number);

        //determine chunk numbers
        int desired_chunk1 = server_number-offset;
        if(desired_chunk1<=0)
            desired_chunk1+=num_servers;
        int desired_chunk2 = desired_chunk1+1;
        if(desired_chunk2>num_servers)
            desired_chunk2=1;

        int result = put_helper(server_socket_addresses[i],chunk_size,path,desired_chunk1,server_number);
        if(result<0)
        {
            fprintf(stderr, "%s put failed\n",path);
            return -1;
        }
        result = put_helper(server_socket_addresses[i],chunk_size,path,desired_chunk2,server_number);
        if(result<0)
        {
            fprintf(stderr, "%s put failed\n",path);
            return -1;
        }
    }
    return 0;
};

int list_helper(struct sockaddr_in server_socket_address, char* filename, int server_number)
{
    char buf[BUFSIZE];
    if(filename==NULL)
        filename="";

    //connect to server
    int server_socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_descriptor == -1)
    {
        fprintf(stderr,"Socket creation failed\n");
        return -1;
    }
    else
    {
        //fprintf(stderr,"Socket successfully created\n");
    }

    if(connect(server_socket_descriptor, &server_socket_address, sizeof(server_socket_address)) != 0)
    {
        fprintf(stderr,"connection with server #%d failed with errno: %s\n", server_number, strerror(errno));
        return -1;
    }
    else
    {
        printf("+------------------+ Server %d +-----+ list %s +------------------+\n",server_number,filename);
        //fprintf(stderr,"connected to server #%d on socket no. %d\n", server_number, server_socket_descriptor);
        if(strcmp(filename,"")==0)
            filename="NULL";
        memset(buf,0,BUFSIZE);
        sprintf(buf,"list %s ",filename);
        //fprintf(stderr,"writing to server #%d, buf: \"%s\", strlen(buf) = %lu\n",server_number,buf,strlen(buf));
        write(server_socket_descriptor,buf,strlen(buf));

        memset(buf,0,BUFSIZE);
        ssize_t read_success = read(server_socket_descriptor,buf,BUFSIZE);
        if(read_success<0)
        {
            fprintf(stderr, "read error\n");
            return -1;
        }
        printf(buf,strlen(buf));
    }
    close(server_socket_descriptor);
    return 0;
}

int list(struct sockaddr_in *server_socket_addresses, int num_servers, char* filename)
{
    int ret_val = 0;
    for(int i=0;i<num_servers;i++)
    {
        int server_number=i+1;
        int result = list_helper(server_socket_addresses[i],filename,server_number);
        if(result<0)
            ret_val = -1;
    }
    return ret_val;
}

int get_helper(struct sockaddr_in server_socket_address, char* filename, int chunk_number, int server_number, char* chunk_buf, int *chunk_size)
{
    char buf[MAXCHAR];
    memset(buf,0,MAXCHAR);
    //connect to server
    int server_socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_descriptor == -1)
    {
        fprintf(stderr,"Socket creation failed\n");
        return -1;
    }
    else
        fprintf(stderr,"Socket successfully created\n");

    if(connect(server_socket_descriptor, &server_socket_address, sizeof(server_socket_address)) != 0)
    {
        fprintf(stderr,"connection with server #%d failed with errno: %s\n", server_number, strerror(errno));
        return -1;
    }
    else
    {
        fprintf(stderr,"connected to server #%d on socket no. %d\n", server_number, server_socket_descriptor);
        memset(buf,0,BUFSIZE);
        sprintf(buf,"get chunk-%d-%s",chunk_number,filename);
        fprintf(stderr,"writing to server #%d, buf: \"%s\", strlen(buf) = %lu\n",server_number,buf,strlen(buf));
        write(server_socket_descriptor,buf,strlen(buf));
    }

    memset(buf,0,BUFSIZE);
    FILE* file = fopen(filename, "wb");
    *chunk_size = read(server_socket_descriptor, buf, MAXCHAR);

    usleep(100);
    fprintf(stderr, "bytes read from server: %d\n",*chunk_size);
    memcpy(chunk_buf,buf,*chunk_size);

    fclose(file);
    close(server_socket_descriptor);
    return 0;
}

int get(struct sockaddr_in *server_socket_addresses, int num_servers, char* filename, char chunks[num_servers][MAXCHAR], int chunk_sizes[num_servers])
{
    unsigned long hash_value = hash(filename);
    int offset = (int)((hash_value)%num_servers);

    //fprintf(stderr,"hash value: %lu\n",hash_value);
    const int hash_length = snprintf(NULL, 0, "%lu",hash_value);
    char hashed_path[hash_length+1];
    snprintf(hashed_path,hash_length+1,"%lu",hash_value);

    FILE* file = fopen(filename, "wb");
    if(!file)
    {
        fprintf(stderr, "failed to open file %s\n", filename);
        return -1;
    }
    //get filesize

    fclose(file);
    int dead_servers = 0;
    for(int i=0;i<num_servers;i++)
    {
        int server_number=i+1;
        fprintf(stderr,"\nReceiving chunks from server %d.\n",server_number);

        //determine chunk numbers
        int desired_chunk1 = server_number-offset;
        if(desired_chunk1<=0)
            desired_chunk1+=num_servers;
        int desired_chunk2 = desired_chunk1+1;
        if(desired_chunk2>num_servers)
            desired_chunk2=1;

        int result = get_helper(server_socket_addresses[i],filename,desired_chunk1,server_number, chunks[desired_chunk1-1], &chunk_sizes[desired_chunk1-1]);
        if(result<0)
        {
            if(++dead_servers>=2)
            {
                fprintf(stderr, "insufficient servers online to get.\n");
                return -1;
            }
        }
        else
            get_helper(server_socket_addresses[i],filename,desired_chunk2,server_number,chunks[desired_chunk2-1], &chunk_sizes[desired_chunk2-1]);
    }
    return 0;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    char buf[BUFSIZE];
    memset(buf,0,BUFSIZE);
    int file_count = argc-2;

    /*
     * check args
     */
    if (argc < 2) {
        fprintf(stderr,"usage: %s <command> [filename] ... [filename]\n", argv[0]);
        return 1;
    }
    if(strcmp(argv[1],"get")==0)
    {
        if(argc<=2)
        {
            fprintf(stderr,"usage: %s get [filename]\n", argv[0]);
            return 1;
        }
    }
    else if(strcmp(argv[1],"put")==0)
    {
        if(argc<=2)
        {
            fprintf(stderr,"usage: %s put [filename] ... [filename]\n", argv[0]);
            return 1;
        }
    }
    else if(strcmp(argv[1],"list")!=0)
    {
        fprintf(stderr, "Invalid command. Try 'get', 'put', or 'list'.\n");
        return 1;
    }

    /*
     * check dfc.conf
     */

    if(access(strcat(getenv("HOME"), "/dfc.conf"),F_OK)!=0)
    {
        fprintf(stderr, "~/dfc.conf does not exist\n");
        return 1;
    }
    if(access(strcat(getenv("HOME"), "/dfc.conf"),R_OK)!=0)
    {
        fprintf(stderr, "~/dfc.conf is inaccessible\n");
        return 1;
    }

    //get number of servers
    int num_servers = 0;
    FILE* dfc_conf_file = fopen(strcat(getenv("HOME"), "/dfc.conf"), "r");
    if(!dfc_conf_file)
    {
        fprintf(stderr, "Error opening ~/dfc.conf\n");
        return 1;
    }

    char filenames[file_count][256];
    for(int i=0;i<file_count;i++)
        strcpy(filenames[i],argv[i+2]);

    char buf_temp[4096]; //max size dfc.conf
    memset(buf_temp,0,4096);
    while(fgets(buf_temp,sizeof(buf_temp),dfc_conf_file) != NULL)
        num_servers++;
    //fprintf(stderr, "num_servers: %d\n",num_servers);
    fclose(dfc_conf_file);

    //store server addresses
    dfc_conf_file = fopen(strcat(getenv("HOME"), "/dfc.conf"), "r");
    size_t len = 0;
    ssize_t line_read;
    char* line = NULL;
    char *server_address;
    int server_index = 0;
    struct sockaddr_in server_socket_addresses[num_servers];
    while ((line_read = getline(&line, &len, dfc_conf_file)) != -1) {
        strtok(line, " ");
        strtok(NULL, " ");
        server_address = strtok(NULL, "\n");
        char* server_address_no_port = strtok(server_address, ":");
        int server_port = atoi(strtok(NULL, ""));
        server_socket_addresses[server_index].sin_addr.s_addr = inet_addr(server_address_no_port);
        server_socket_addresses[server_index].sin_port = htons(server_port);
        server_socket_addresses[server_index].sin_family = AF_INET;
        server_index++;
        //fprintf(stderr, "Server: %s Port: %d\n", server_address_no_port, server_port);
    }
    fclose(dfc_conf_file);

    char chunks[num_servers][MAXCHAR];
    for(int i=0;i<num_servers;i++)
        memset(chunks[i],0,MAXCHAR);
    if(strncmp(argv[1],"list",4)==0 && file_count<=0)
    {
        int success = list(server_socket_addresses,num_servers,NULL);
        if(success<0)
            return 1;
    }
    for(int i=0;i<file_count;i++)
    {
        if(strncmp(argv[1],"list",4)==0)
        {
            int success = list(server_socket_addresses,num_servers,filenames[i]);
            if(success<0)
                return 1;
        }
        else if(strncmp(argv[1],"get",3)==0)
        {
            int chunk_sizes[num_servers];
            int success = get(server_socket_addresses,num_servers,filenames[i], chunks, chunk_sizes);
            if(success==0)
            {
                FILE* file = fopen(filenames[i],"wb");
                for(int j=0;j<num_servers;j++)
                {
                    //fprintf(stderr,"chunk_%d: %s\n", j+1, chunks[j]);
                    fwrite(chunks[j],chunk_sizes[j],1,file);
                }
                fclose(file);
            }
            else
            {
                fprintf(stderr,"%s is incomplete.\n",filenames[i]);
                return 1;
            }
        }
        else if(strncmp(argv[1],"put",3)==0)
        {
            int success = put(server_socket_addresses,num_servers,filenames[i]);
            if(success<0)
            {
                return 1;
            }
        }
    }
}
