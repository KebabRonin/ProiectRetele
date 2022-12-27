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
#include <sys/wait.h>
#include "../common_definitions.h"

/*void get_input(char msg[MSG_MAX_SIZE]) {
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
        //sendto(sockfd,msg,strlen(msg),0, &server_sockaddr, );
        printf("sent %s to serv\n",msg);
        recv(sockfd,msg,MSG_MAX_SIZE,0);
        printf("[serv]>%s\n",msg);
    }
}
*/
//enum CLMSG{AGLIST};

static const char* server_address;

void get_request(const char* request, char response[MSG_MAX_SIZE]) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if ( -1 == sockfd ) {
        perror("socket()");
        exit(2);
    }
    fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);

    struct sockaddr_in server_sockaddr;
    bzero(&server_sockaddr, sizeof(server_sockaddr));
    

    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = inet_addr(server_address);
    server_sockaddr.sin_port = htons (CLIENT_PORT);


    bool recieved_response = false;
    bzero(response, MSG_MAX_SIZE);
    
    while(!recieved_response) {
        if (sendto(sockfd, request, strlen(request), 0, (struct sockaddr*)&server_sockaddr, sizeof(server_sockaddr)) < 0) {
            perror("sendto");
            exit(1);
        }
        printf("Sent :%s:\n",request);
        if(recv(sockfd, response, MSG_MAX_SIZE, 0) < 0) {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
               sleep(1);
            else {
                perror("recv()");
                exit(2);
            }
        }
        else {
            recieved_response = true;
        }
    }
}

void wid_agent_list() {
    char response[MSG_MAX_SIZE];
    char request[2];
    request[0] = CLMSG_AGLIST;
    request[1] = '\0';
    get_request(request, response);

    printf("%s\n",response);
}

void wid_agent_properties(char* id) {
    char response[MSG_MAX_SIZE];
    char request[MSG_MAX_SIZE];
    request[0] = CLMSG_AGPROP;
    sprintf(request+1, "%s", id);
    get_request(request, response);

    printf("%s\n",response);
}

int main(int argc, char* argv[]) {
    if(argc < 3) {
        printf("Usage: %s ip widget\n", argv[0]);
        return 0;
    }
    
    server_address = argv[1];


    switch(argv[2][0]) {
        case 'l':
            wid_agent_list();
            break;
        case 'p':
            if (argc < 4) {
                printf("Must specify agent\n");
                exit(2);
            }
            wid_agent_properties(argv[3]);
            break;
        default:
            printf("Unsupported widget\n");
    }
}
