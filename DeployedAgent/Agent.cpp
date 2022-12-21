#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <vector>
#include "InfoSource.h"

#define AGENT_PORT 2077

pthread_mutex_t my_mutex = PTHREAD_MUTEX_INITIALIZER;

int control_sd, transfer_sd;

void init_control_connection(const char* ip) {

    control_sd = socket(AF_INET, SOCK_STREAM, 0);
    if( -1 == control_sd ) {
        perror("socket()");
        exit(1);
    }

    struct sockaddr_in server_sockaddr;

    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = inet_addr(ip);
    server_sockaddr.sin_port = htons (AGENT_CONTROL_PORT);

    while ( -1 == connect(control_sd, (struct sockaddr*) &server_sockaddr, sizeof(server_sockaddr))) {
        perror("Couldn't connect to server");
        sleep(5);
    }
}

void init_transfer_connection(const char* ip, short int transfer_port) {

    transfer_sd = socket(AF_INET, SOCK_STREAM, 0);
    if( -1 == transfer_sd ) {
        perror("socket()");
        exit(1);
    }

    struct sockaddr_in server_sockaddr; 
    bzero(&server_sockaddr, sizeof(server_sockaddr));

    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = inet_addr(ip);
    server_sockaddr.sin_port = htons (transfer_port);


    while ( -1 == connect(transfer_sd, (struct sockaddr*) &server_sockaddr, sizeof(server_sockaddr))) {
        perror("Couldn't initialise transfer connection to server");
        sleep(2);
    }
}

bool login() {
    return true;
}

void init_comms_to_server(const char* ip) {

    init_control_connection(ip);

    if (false == login() ) {
        printf("Login failed.\n");
        exit(0);
    }

    //recieve port for transfer connection

    unsigned short transfer_port;

    if ( 0 >= recv(control_sd, &transfer_port, sizeof(transfer_port), 0) ) {
        perror("recv");
        exit(1);
    }

    //init transfer connection
    
    printf("transfer port : %d\n", transfer_port);
    //transfer_port = ntohs(transfer_port);
    printf("transfer port : %d\n", transfer_port);

    init_transfer_connection(ip, transfer_port);

    printf("Connected to server!\n");
}


int main(int argc, char* argv[]) {
    if(argc < 2) {
        printf("Usage: %s ip\n", argv[0]);
        return 0;
    }

    init_comms_to_server(argv[1]);

    
    std::vector<InfoSource*> sources;
    sources.push_back(createIS("/var/log/syslog"));

    printf("Added syslog, listening for commands...\n");
    pause();
    /*char command[MSG_MAX_SIZE];
    char response[MSG_MAX_SIZE];

    fd_set actfds,readfds;

    FD_ZERO(&actfds);
    FD_SET(sockfd,&actfds);

    while(1) {
        bcopy ((char *) &actfds, (char *) &readfds, sizeof (readfds));
        bzero(command,sizeof(command));
        ///!! ar trebui sa fie un sockfd nou, pus cu TCP pe AGENT_TCP_PORT
        if (select(sockfd+1, &readfds, nullptr, nullptr, nullptr) < 0) {
            perror("select()");
            return 3;
        }
        if (recv(sockfd,command,sizeof(command),0) < 0) {
            perror("recv()");
            return 3;
        }
        
        if(strstr(command, "add ") == command) {
            InfoSource* to_add = createIS(command + 4, sockfd);
            if( to_add != nullptr ) {
                sources.push_back(to_add);
                ///!send to parent the new id
                strcpy(response,"Success\0");
            }
            else {
                strcpy(response,"Error\0");
            }
            if (send(sockfd, response, strlen(response), 0) < 0) {
                perror("send()");
                return 3;
            }
            
        }
        else {
            printf("Unknown command:%s\n",command);
        }
    }*/
    return 0;
}
