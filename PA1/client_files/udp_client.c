/* 
 * udpclient.c - A simple UDP client
 * usage: udpclient <host> <port>
 */
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFSIZE 1024

/*
 * error - wrapper for perror
 */
void error(char *msg) {
    perror(msg);
    exit(0);
}

int main(int argc, char **argv) {
    int sockfd, portno, n;
    int serverlen;
    struct sockaddr_in serveraddr;
    struct hostent *server;
    char *hostname;
    char buf[BUFSIZE];

    /* check command line arguments */
    if (argc != 2) {
        fprintf(stderr,"usage: %s <port>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    /* gethostbyname: get the server's DNS entry */
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host as %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,
          (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(portno);

    /*
    //Timeout
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    int receive_timeout = setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    int send_timeout = setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    if(receive_timeout<0 || send_timeout<0)
        error("Binding timeout failed.");
    */

    char command[BUFSIZE];
    char filename[BUFSIZE];
    char inputBuffer[BUFSIZE];
    while(1)
    {
        bzero(command, BUFSIZE);
        bzero(filename, BUFSIZE);
        bzero(buf, BUFSIZE);
        bzero(inputBuffer, BUFSIZE);
        printf("Please enter a command: ");
        fgets(inputBuffer, BUFSIZE, stdin);
        //printf("buf: %s, inputbuf: %s", buf, inputBuffer);
        sscanf(inputBuffer, "%s %s", command, filename);
        //printf("command: %s, args: %s\n", command,filename);
        serverlen = sizeof(serveraddr);
        if (strcmp(command, "ls") == 0)
        {
            /* send the message to the server */
            n = sendto(sockfd, inputBuffer, strlen(inputBuffer), 0, &serveraddr, serverlen);
            //printf("sent %d bytes\n",n);
            if (n < 0)
                error("ERROR in sendto");

            /* print the server's reply */
            //printf("pre-receive buf: %s. buflen: %lu\n",buf,strlen(buf));
            n = recvfrom(sockfd, buf, BUFSIZE, 0, &serveraddr, &serverlen);
            //printf("post-receive buf: %s\n",buf);
            //printf("received %d bytes\n",n);
            if (n < 0)
                error("ERROR in recvfrom");
            printf("%s", buf);
        } else if (strcmp(command, "put") == 0)
        {
            FILE *file = fopen(filename, "rb");
            if (file)
            {
                // get filesize
                fseek(file, 0, SEEK_END);
                long file_size = ftell(file);
                fseek(file, 0, SEEK_SET);

                /* send the first packet to the server with the filename and announcing the put */
                n = sendto(sockfd, inputBuffer, strlen(inputBuffer), 0, &serveraddr, serverlen);
                if (n < 0)
                    error("ERROR in sendto");

                /* wait for ack */
                n = recvfrom(sockfd, buf, BUFSIZE, 0, &serveraddr, &serverlen);
                if (n < 0)
                    error("ERROR in sendto");
                if (strncmp(buf, "Unable", 6) == 0)
                    error("Server unable to open file.");
                if (strncmp(buf, "ack", 3) != 0)
                    error("ack corrupted.");

                /* send the leading packet to the server indicating the filesize */
                bzero(buf, BUFSIZE);
                sprintf(buf, "%ld", file_size);
                n = sendto(sockfd, buf, strlen(buf), 0, &serveraddr, serverlen);
                if (n < 0)
                    error("ERROR in sendto");

                /* wait for ack */
                bzero(buf, BUFSIZE);
                n = recvfrom(sockfd, buf, BUFSIZE, 0, &serveraddr, &serverlen);
                if (n < 0)
                    error("ERROR in sendto");
                if (strncmp(buf, "ack", 3) != 0)
                    error("ack corrupted.");

                /* start sending those chunks out, big man! */
                long bytes_left = file_size;
                do
                {
                    bzero(buf, BUFSIZE);

                    /* calculate chunk */
                    int packet_size = BUFSIZE;
                    if (bytes_left < BUFSIZE)
                        packet_size = (int) bytes_left;
                    //printf("packet size: %d\n",packet_size);
                    int read_bytes = fread(buf, 1, packet_size, file);
                    //printf("read bytes: %d\n",read_bytes);
                    //printf("buf: %s\n", buf);
                    printf("attempting to send %d bytes. %ld bytes should be left.\n", packet_size,
                           bytes_left - packet_size);
                    /* send chunk */
                    n = sendto(sockfd, buf, read_bytes, 0, &serveraddr, serverlen);
                    if (n < 0)
                        error("ERROR in sendto");

                    /* wait for ack */
                    bzero(buf, BUFSIZE);
                    n = recvfrom(sockfd, buf, BUFSIZE, 0, &serveraddr, &serverlen);
                    if (n < 0)
                        error("ERROR in sendto");
                    if (strncmp(buf, "ack", 3) != 0)
                        error("ack corrupted.");

                    /* update bytes remaining */
                    bytes_left -= packet_size;
                    //printf("bytes_left: %ld",bytes_left);
                } while (bytes_left > 0);
                fclose(file);
                printf("put done.\n");
            } else
                printf("Invalid Filename.\n");
        } else if (strcmp(command, "get") == 0)
        {
            /* open file */
            FILE *file = fopen(filename, "w+b");
            if (!file) //cannot open file
                printf("Invalid Filename.\n");
            else //can open file!
            {
                /* send get with filename */
                n = sendto(sockfd, inputBuffer, strlen(inputBuffer), 0, (struct sockaddr *) &serveraddr, serverlen);
                if (n < 0)
                    error("ERROR in sendto");

                /* get filesize */
                bzero(buf, BUFSIZE);
                n = recvfrom(sockfd, buf, BUFSIZE, 0,
                             (struct sockaddr *) &serveraddr, &serverlen);
                if (n < 0)
                    error("ERROR in recvfrom");
                long file_size = atol(buf);

                /* send ack */
                n = sendto(sockfd, "ack", 3, 0, (struct sockaddr *) &serveraddr, serverlen);
                if (n < 0)
                    error("ERROR in sendto");

                /* start receiving chunks */
                long bytes_remaining = file_size;
                while (bytes_remaining > 0)
                {
                    bzero(buf, BUFSIZE);
                    /* receive chunk */
                    n = recvfrom(sockfd, buf, BUFSIZE, 0, (struct sockaddr *) &serveraddr, &serverlen);
                    if (n < 0)
                        error("ERROR in recvfrom");
                    int packet_size = n;
                    bytes_remaining -= packet_size;
                    printf("Received %d bytes. %ld/%ld bytes remaining.\n", packet_size, bytes_remaining, file_size);

                    /* write chunk to file */
                    fwrite(buf, 1, packet_size, file);

                    /* send ack */
                    n = sendto(sockfd, "ack", 3, 0, (struct sockaddr *) &serveraddr, serverlen);
                    if (n < 0)
                        error("ERROR in sendto");
                }
                //printf("sent %d bytes to %s (%s)\n",n,hostp->h_name, hostaddrp);
            }
            fclose(file);
            printf("Completed downloading file %s.\n", filename);
        } else if (strcmp(command, "delete") == 0)
        {
            /* send the message to the server */
            n = sendto(sockfd, inputBuffer, strlen(inputBuffer), 0, &serveraddr, serverlen);
            //printf("\nsent %d bytes",n);
            if (n < 0)
                error("ERROR in sendto");

            /* print the server's reply */
            n = recvfrom(sockfd, buf, BUFSIZE, 0, &serveraddr, &serverlen);
            printf("received %d bytes\n", n);
            if (n < 0)
                error("ERROR in recvfrom");
            printf("%s", buf);
        } else if (strcmp(command, "exit") == 0)
        {
            /* send the message to the server */
            n = sendto(sockfd, "exit", 4, 0, &serveraddr, serverlen);
            if (n < 0)
                error("ERROR in sendto");

            /* print the server's reply */
            n = recvfrom(sockfd, buf, BUFSIZE, 0, &serveraddr, &serverlen);
            if (n < 0)
                error("ERROR in recvfrom");
            printf("%s", buf);
            close(sockfd);
            return 0;
        } else
        {
            printf("Invalid Input.");
            //invalid input
        }
    }
}
