#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <pthread.h>
#include "../common_definitions.h"
#define MSG_MAX_SIZE 205



int main(int argc, char* argv[]) {
    if(argc < 2) {
        printf("Usage: %s ip\n", argv[0]);
        return 0;
    }
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if( -1 == sockfd ) {
        perror("socket()");
        return 1;
    }

    struct sockaddr_in server_sockaddr;

    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = inet_addr(argv[1]);
    server_sockaddr.sin_port = htons (CLIENT_PORT);

    //USE GTK+ FOR THIS
    ///!UDP
    char msg[MSG_MAX_SIZE];
    while(1) {
        
        //post_process(msg); //tokens

        bzero(msg,MSG_MAX_SIZE);
        printf("[client]>"); fflush(stdout);
        read(0,msg,MSG_MAX_SIZE);
        printf("read -%s-\n",msg);
        if(strstr(msg,"exit") == msg) {
            close(sockfd);
            exit(0);
        }
        send(sockfd,msg,strlen(msg),0);
        printf("sent %s to serv\n",msg);
        recv(sockfd,msg,MSG_MAX_SIZE,0);
        printf("[serv]>%s\n",msg);
    }   
}