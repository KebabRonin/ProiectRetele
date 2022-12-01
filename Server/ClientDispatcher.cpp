#pragma once
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
#include <vector>
#include "Agent.h"
#include "../common_definitions.h"

extern std::vector<struct Agent*> agent_list;

struct clparam{
    int serv_sock;
    char* param;
    struct sockaddr_in* sock;
};

int init_server_to_port_UDP (int port) {
    struct sockaddr_in server_sockaddr; bzero( &server_sockaddr, sizeof(server_sockaddr) );
    int server_sockfd;
    if( -1 == (server_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) ) {
        perror("socket()");
        return -1;
    }
    

    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_sockaddr.sin_port = htons (port);

    int on=1;
    setsockopt(server_sockfd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));

    if ( -1 == bind(server_sockfd, (struct sockaddr*) &server_sockaddr, (socklen_t) sizeof(server_sockaddr))) {
        perror("Error binding socket");
        return -1;
    }
    printf("Bound to %d\n",port);
    return server_sockfd;
}

void* fnc_handle_client(void* p) {
    pthread_detach(pthread_self());
    struct clparam *cparam = (clparam*)p;
    int sockfd = cparam->serv_sock;
    char clreq[MSG_MAX_SIZE];
    bzero(clreq,MSG_MAX_SIZE);
    strcpy(clreq, cparam->param); delete[] cparam->param;
    
    struct sockaddr_in clsock = *cparam->sock; delete[] cparam->sock;
    delete cparam;
    
    printf("Recieved:%s:\n",clreq);
    char type = clreq[0];
    char response[MSG_MAX_SIZE]; bzero(response, MSG_MAX_SIZE);
    if (type == CLMSG_AGLIST) {
        printf("AGLIST\n");
        for(auto i : agent_list) {
            strcat(response,i->id);
            strcat(response,"\n");
        }
        if(strlen(response) == 0) {
            sprintf(response, "No agents");
        }
    }
    else if (type == CLMSG_AGPROP) {
        printf("AGPROP\n");
        for(auto i : agent_list) {
            if(strcmp(i->id, clreq+1) == 0) {
                sprintf(response,"id:%s\nversion:%s\n",i->id,i->version);
                break;
            }
        }
        if(strlen(response) == 0) {
            sprintf(response, "Unknown agent");
        }
    }
    else {
        strcpy(response,"Unknown command");
    }
    if ( 0 > sendto(sockfd, response, strlen(response), 0, (struct sockaddr*)&clsock, sizeof(clsock)) ) {
        perror("sendto");
        exit(3);
    }
    printf("Sent:%s:\n",response);
    pthread_exit(nullptr);
}

void* fnc_client_dispatcher(void*) {
    
    struct sockaddr_in client_sockaddr;  bzero( &client_sockaddr , sizeof(client_sockaddr ) );

    int server_sockfd = init_server_to_port_UDP(CLIENT_PORT);
    fd_set actfds,readfds;

    FD_ZERO(&actfds);
    FD_SET(server_sockfd,&actfds);

    char buf[MSG_MAX_SIZE];

    while(1) {
        bzero(buf,sizeof(buf));
        int length = sizeof (client_sockaddr);
        bcopy ((char *) &actfds, (char *) &readfds, sizeof (readfds));
        if (select(server_sockfd+1, &readfds, nullptr, nullptr, nullptr) < 0) {
            perror("select()");
            exit(3);
        }
        if( 0 > recvfrom(server_sockfd,buf,MSG_MAX_SIZE, 0,(sockaddr*) &client_sockaddr,(socklen_t*) &length)) {
            perror("recv_from");
            exit(7);
        }
        struct clparam* cparam = new struct clparam;
        cparam->serv_sock = server_sockfd;
        cparam->param = new char[strlen(buf) + 1]; sprintf(cparam->param,"%s",buf);
        cparam->sock = new sockaddr_in; bcopy(&client_sockaddr,cparam->sock, length);
        pthread_t tid;
        if ( 0 > pthread_create(&tid, nullptr, fnc_handle_client, (void*)cparam) ) {
            perror("pthread_create");
            exit(3);
        }
    }
    return nullptr;
}
