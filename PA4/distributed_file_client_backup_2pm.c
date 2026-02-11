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

int put_helper(int socket_descriptor, long chunk_size, char* path, long file_seekpoint)
{
    FILE* file = fopen(path, "rb");

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

        fread(response,1,packet_size,file);
        write(socket_descriptor,response,packet_size);
        remaining_bytes-=packet_size;
    }
    return 0;
}


//no need to check for server_status because if 1 or more are down, this func never gets invoked.
int put(int *socket_descriptors, int num_servers, char* path)
{
    char buf[BUFSIZE];
    char buf2[BUFSIZE];
    unsigned long hash_value = hash(path);
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

    char * filename = basename(path);

    long chunk_size = (file_size/num_servers) + 1;

    fclose(file);

    for(int i=0;i<num_servers;i++)
    {
        fprintf(stderr,"\n");
        //hash offset as described in project description. didn't use md5.
        int server_number=i+1;
        //fprintf(stderr, "server number: %d, offset: %d\n",server_number, offset);

        //determine chunk numbers
        int desired_chunk1 = server_number-offset;
        if(desired_chunk1<=0)
            desired_chunk1+=num_servers;
        int desired_chunk2 = desired_chunk1+1;
        if(desired_chunk2>num_servers)
            desired_chunk2=1;

        //fprintf(stderr,"(%d,%d)\n",desired_chunk1,desired_chunk2);
        //fprintf(stderr,"desired chunk 1: %d, 2: %d", desired_chunk1, desired_chunk2);

        char desired_chunk_string1[8+strlen(filename)];
        char desired_chunk_string2[8+strlen(filename)];
        sprintf(desired_chunk_string1,"chunk-%d-%s %ld\r",desired_chunk1,filename,chunk_size);
        sprintf(desired_chunk_string2,"chunk-%d-%s %ld\r",desired_chunk2,filename,chunk_size);

        //fprintf(stderr, "desired_chunk_string1: %s\ndesired_chunk_string2: %s\n",desired_chunk_string1, desired_chunk_string2);

        long chunk1=chunk_size*(desired_chunk1-1);
        long chunk2=chunk_size*(desired_chunk2-1);

        memset(buf,0,BUFSIZE);
        strcpy(buf, "put ");
        strcat(buf, desired_chunk_string1);
        fprintf(stderr,"writing to server %d, buf: %s\n",i,buf);
        write(socket_descriptors[i],buf,strlen(buf));
        put_helper(socket_descriptors[i], chunk_size, path, chunk1);

        write(socket_descriptors[i],"\r",1);

        memset(buf2,0,BUFSIZE);
        strcpy(buf2, "put ");
        strcat(buf2, desired_chunk_string2);
        fprintf(stderr,"writing to server %d, buf2: %s\n",i,buf2);
        write(socket_descriptors[i],buf2,strlen(buf2));
        put_helper(socket_descriptors[i], chunk_size, path, chunk2);

        write(socket_descriptors[i],"\r",1);
    }
    return 0;
};

int list(int *socket_descriptors, const int *server_status, int num_servers, char* filename)
{
    char buf[BUFSIZE];
    char list_buf[BUFSIZE];
    for(int i=0;i<num_servers;i++)
    {
        if(server_status[i]==1)
        {
            memset(buf,0,BUFSIZE);
            ssize_t read_success = read(socket_descriptors[i],buf,BUFSIZE);
            if(read_success<0)
            {
                fprintf(stderr, "Socket read failed.\n");
                return -1;
            }
            strcat(list_buf,buf);
        }
    }
    return 0;
}

int get(int *socket_descriptors, int *server_status, int num_servers, char* filename)
{
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
        if(argc != 3)
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
    if(access("./dfc.conf",F_OK)!=0)
    {
        fprintf(stderr, "./dfc.conf does not exist\n");
        return 1;
    }
    if(access("./dfc.conf",R_OK)!=0)
    {
        fprintf(stderr, "./dfc.conf is inaccessible\n");
        return 1;
    }

    //get number of servers
    int num_servers = 0;
    FILE* dfc_conf_file = fopen("./dfc.conf", "r");
    if(!dfc_conf_file)
    {
        fprintf(stderr, "Error opening ./dfc.conf\n");
        return 1;
    }

    char filenames[file_count][256];
    for(int i=0;i<file_count;i++)
        strcpy(filenames[i],argv[i+2]);

    char buf_temp[4096]; //max size dfc.conf
    memset(buf_temp,0,4096);
    while(fgets(buf_temp,sizeof(buf_temp),dfc_conf_file) != NULL)
        num_servers++;
    fprintf(stderr, "num_servers: %d\n",num_servers);
    fclose(dfc_conf_file);

    //store server addresses
    dfc_conf_file = fopen("./dfc.conf", "r");
    size_t len = 0;
    ssize_t line_read;
    char* line = NULL;
    char *server_type, *server_name, *server_address;
    int server_index = 0;
    struct sockaddr_in server_socket_addresses[num_servers];
    while ((line_read = getline(&line, &len, dfc_conf_file)) != -1) {
        server_type = strtok(line, " ");
        server_name = strtok(NULL, " ");
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

    for(int i=0;i<file_count;i++)
    {
        int socket_descriptors[num_servers];
        int server_status[num_servers];
        int num_alive_servers = 0;
        for(int j=0;j<num_servers;j++)
        {
            socket_descriptors[j] = socket(AF_INET, SOCK_STREAM, 0);
            if (socket_descriptors[j] == -1)
            {
                fprintf(stderr,"Socket creation failed\n");
                exit(0);
            }
            else
                fprintf(stderr,"Socket successfully created\n");

            if(connect(socket_descriptors[j], &server_socket_addresses[j], sizeof(server_socket_addresses[j])) != 0)
            {
                fprintf(stderr,"connection with server #%d failed with errno: %s\n", j, strerror(errno));
                server_status[j]=0;
            }
            else
            {
                fprintf(stderr,"connected to server #%d on socket no. %d\n", j, socket_descriptors[j]);
                server_status[j]=1;
                num_alive_servers++;
            }
        }

        if(strncmp(argv[1],"put",3)==0 && num_alive_servers!=num_servers)
        {
            fprintf(stderr, "cannot put without all servers being online.\n");
            return 1;
        }
        if(strncmp(argv[1],"get",3)==0 && num_alive_servers<=num_servers-2)
        {
            fprintf(stderr, "cannot get, two or more servers are offline.\n");
            return 1;
        }

        if(strncmp(argv[1],"list",4)==0)
        {
            list(socket_descriptors,server_status,num_servers,filenames[i]);
        }
        else if(strncmp(argv[1],"get",3)==0)
        {
            get(socket_descriptors,server_status,num_servers,filenames[i]);
        }
        else if(strncmp(argv[1],"put",3)==0)
        {
            put(socket_descriptors,num_servers,filenames[i]);
        }

//        for(int j=0;j<num_servers;j++)
//        {
//            write(socket_descriptors[j],"close",5);
//            close(socket_descriptors[j]);
//        }
    }
}
