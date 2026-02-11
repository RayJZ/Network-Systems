/* 
 * udpserver.c - A simple UDP echo server 
 * usage: udpserver <port>
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFSIZE 1024

/*
 * error - wrapper for perror
 */
void error(char *msg) {
    perror(msg);
    exit(1);
}

int main(int argc, char **argv) {
    int sockfd; /* socket */
    int portno; /* port to listen on */
    int clientlen; /* byte size of client's address */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    struct hostent *hostp; /* client host info */
    char buf[BUFSIZE]; /* message buf */
    char *hostaddrp; /* dotted decimal host addr string */
    int optval; /* flag value for setsockopt */
    int n; /* message byte size */

    /*
     * check command line arguments
     */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    /*
     * socket: create the parent socket
     */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    /* setsockopt: Handy debugging trick that lets
     * us rerun the server immediately after we kill it;
     * otherwise we have to wait about 20 secs.
     * Eliminates "ERROR on binding: Address already in use" error.
     */
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
               (const void *)&optval , sizeof(int));

    /*
     * build the server's Internet address
     */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    /*
     * bind: associate the parent socket with a port
     */
    if (bind(sockfd, (struct sockaddr *) &serveraddr,
             sizeof(serveraddr)) < 0)
        error("ERROR on binding");


    /*
     * main loop: wait for a datagram, then echo it
     */
    clientlen = sizeof(clientaddr);
    while (1)
    {
        /*
         * recvfrom: receive a UDP datagram from a client
         */

        /*
        // Disable Timeout
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;
        int receive_timeout = setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        if(receive_timeout<0)
            error("Binding timeout failed.");
        */
        /* receive command */
        bzero(buf, BUFSIZE);
        n = recvfrom(sockfd, buf, BUFSIZE, 0,
                     (struct sockaddr *) &clientaddr, &clientlen);
        if (n < 0)
            error("ERROR in recvfrom");

        /*
         * gethostbyaddr: determine who sent the datagram
         */
        hostp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr,
                              sizeof(clientaddr.sin_addr.s_addr), AF_INET);
        if (hostp == NULL)
            error("ERROR on gethostbyaddr");
        hostaddrp = inet_ntoa(clientaddr.sin_addr);
        if (hostaddrp == NULL)
            error("ERROR on inet_ntoa\n");
        printf("server received datagram from %s (%s)\n",
               hostp->h_name, hostaddrp);
        printf("server received %d/%d bytes: %s", strlen(buf), n, buf);

        /*
         * sendto: echo the input back to the client
         */

        char command[BUFSIZE];
        char filename[BUFSIZE];
        sscanf(buf, "%s %s", command, filename);
        //printf("\nCommand: %s\n",command);
        printf("Received command: %s\n",command);
        if(strcmp(command,"ls")==0)
        {
            /* ls */
            FILE * file = popen("ls -l", "r");
            fread(buf,BUFSIZE,1,file);
            pclose(file);
            //printf("ls output: %s\n",buf);
            /* send the message to the client */
            n = sendto(sockfd, buf, strlen(buf), 0, (struct sockaddr *) &clientaddr, clientlen);
            if (n < 0)
                error("ERROR in sendto");
            printf("sent %d bytes to %s (%s)\n",n,hostp->h_name, hostaddrp);
            printf("ls done.\n");
        }
        else if(strcmp(command,"put")==0)
        {
            /* open file */
            FILE* file = fopen(filename,"w+b");
            if(!file) //cannot open file
            {
                n = sendto(sockfd, "Unable to open file.\n", 21, 0, (struct sockaddr *) &clientaddr, clientlen);
                if (n < 0)
                    error("ERROR in sendto");
            }
            else //can open file!
            {
                /* send ack */
                n = sendto(sockfd, "ack", 3, 0, (struct sockaddr *) &clientaddr, clientlen);
                if (n < 0)
                    error("ERROR in sendto");

                /* get filesize */
                bzero(buf, BUFSIZE);
                n = recvfrom(sockfd, buf, BUFSIZE, 0,
                             (struct sockaddr *) &clientaddr, &clientlen);
                if (n < 0)
                    error("ERROR in recvfrom");
                long file_size = atol(buf);

                /* send ack */
                n = sendto(sockfd, "ack", 3, 0, (struct sockaddr *) &clientaddr, clientlen);
                if (n < 0)
                    error("ERROR in sendto");

                /* start receiving chunks */
                long bytes_remaining = file_size;
                while(bytes_remaining>0)
                {
                    bzero(buf, BUFSIZE);
                    /* receive chunk */
                    n = recvfrom(sockfd, buf, BUFSIZE, 0, (struct sockaddr *) &clientaddr, &clientlen);
                    if (n < 0)
                        error("ERROR in recvfrom");
                    int packet_size = n;
                    bytes_remaining-=packet_size;
                    printf("Received %d bytes. %ld/%ld bytes remaining.\n", packet_size,bytes_remaining,file_size);

                    /* write chunk to file */
                    fwrite(buf,1,packet_size,file);

                    /* send ack */
                    n = sendto(sockfd, "ack", 3, 0, (struct sockaddr *) &clientaddr, clientlen);
                    if (n < 0)
                        error("ERROR in sendto");
                }
                //printf("sent %d bytes to %s (%s)\n",n,hostp->h_name, hostaddrp);
            }
            fclose(file);
        }
        else if(strcmp(command,"get")==0)
        {
            FILE* file = fopen(filename, "rb");
            if(file)
            {
                // get filesize
                fseek(file, 0, SEEK_END);
                long file_size = ftell(file);
                fseek(file, 0, SEEK_SET);

                /* send the leading packet to the client indicating the filesize */
                bzero(buf,BUFSIZE);
                sprintf(buf,"%ld",file_size);
                n = sendto(sockfd, buf, strlen(buf), 0, &clientaddr, clientlen);
                if (n < 0)
                    error("ERROR in sendto");

                /* wait for ack */
                bzero(buf,BUFSIZE);
                n = recvfrom(sockfd, buf, BUFSIZE, 0, &clientaddr, &clientlen);
                if (n < 0)
                    error("ERROR in recvfrom");
                if(strncmp(buf,"ack",3)!=0)
                    error("ack corrupted.");

                /* start sending those chunks out, big man! */
                long bytes_left = file_size;
                do
                {
                    bzero(buf,BUFSIZE);

                    /* calculate chunk */
                    int packet_size = BUFSIZE;
                    if(bytes_left<BUFSIZE)
                        packet_size = (int)bytes_left;
                    //printf("packet size: %d\n",packet_size);
                    int read_bytes = fread(buf,1,packet_size,file);
                    //printf("read bytes: %d\n",read_bytes);
                    //printf("buf: %s\n", buf);
                    printf("attempting to send %d bytes. %ld bytes should be left.\n",packet_size,bytes_left-packet_size);
                    /* send chunk */
                    n = sendto(sockfd, buf, read_bytes, 0, &clientaddr, clientlen);
                    if (n < 0)
                        error("ERROR in sendto");

                    /* wait for ack */
                    bzero(buf,BUFSIZE);
                    n = recvfrom(sockfd, buf, BUFSIZE, 0, &clientaddr, &clientlen);
                    if (n < 0)
                        error("ERROR in sendto");
                    if(strncmp(buf,"ack",3)!=0)
                        error("ack corrupted.");

                    /* update bytes remaining */
                    bytes_left-=packet_size;
                    //printf("bytes_left: %ld",bytes_left);
                } while(bytes_left>0);
                fclose(file);
                printf("get done.\n");
            }
            else
            {
                n = sendto(sockfd, "Invalid Filename\n", 17, 0, (struct sockaddr *) &clientaddr, clientlen);
                if (n < 0)
                    error("ERROR in sendto");
            }
        }
        else if(strcmp(command,"delete")==0)
        {
            /* send the message to the client */
            if(remove(filename)==0)
                strcpy(buf,"Successfully deleted file.\n");
            else
                strcpy(buf,"Failed to delete file.\n");
            //printf("buf: %s\n",buf);
            n = sendto(sockfd, buf, strlen(buf), 0, (struct sockaddr *) &clientaddr, clientlen);
            if (n < 0)
                error("ERROR in sendto");
            printf("sent %d bytes to %s (%s)\n",n,hostp->h_name, hostaddrp);
            printf("delete done.\n");
        }
        else if(strcmp(command,"exit")==0)
        {
            /* send the message to the client */
            n = sendto(sockfd, "Okay! Bye-bye!\n", 15, 0, (struct sockaddr *) &clientaddr, clientlen);
            if (n < 0)
                error("ERROR in sendto");
            printf("sent %d bytes to %s (%s)\n",n,hostp->h_name, hostaddrp);
            printf("exit done.\n");
        }
        else
        {
            n = sendto(sockfd, "Invalid Input\n", 14, 0, (struct sockaddr *) &clientaddr, clientlen);
            if (n < 0)
                error("ERROR in sendto");
            printf("sent %d bytes to %s (%s)\n",n,hostp->h_name, hostaddrp);
            printf("Invalid input received from client.\n");
        }
        printf("\n");
    }
}
