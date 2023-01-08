#pragma once
#include "Agent.h"
#include <vector>

int init_server_to_port (int port) {
    struct sockaddr_in server_sockaddr; bzero( &server_sockaddr, sizeof(server_sockaddr) );
    int server_sockfd;
    if( -1 == (server_sockfd = socket(AF_INET, SOCK_STREAM, 0)) ) {
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

    if ( -1 == listen(server_sockfd, 5) ) {
        perror("Error listening socket");
        return -1;
    }
    printf("Bound to %d\n",port);
    return server_sockfd;
}
extern pthread_mutex_t agent_list_lock;
extern std::vector<Agent*> agent_list;

void* fnc_agent_control_dispatcher(void*) {
    int server_sockfd = init_server_to_port(AGENT_CONTROL_PORT);
    if(server_sockfd == -1) {
        printf("Error initialising control port\n");
        pthread_exit(nullptr);
    }

    int agent_sockfd;
    struct sockaddr_in agent_sockaddr;
    socklen_t len = sizeof(agent_sockaddr);

    while(1) {
        bzero( &agent_sockaddr , sizeof(agent_sockaddr) );

        agent_sockfd = accept(server_sockfd,(struct sockaddr*) &agent_sockaddr, &len);
        if( agent_sockfd < 0) {
            perror("accepting agent connection");
            //pthread_exit(nullptr);
        }

        new Agent(&agent_sockaddr, &agent_sockfd);
        printf("Connection found\n");
    }
    return nullptr;
}

void* fnc_agent_transfer_dispatcher(void*) {
    int server_sockfd = init_server_to_port(AGENT_TRANSFER_PORT);
    if(server_sockfd == -1) {
        printf("Error initialising transfer port\n");
        pthread_exit(nullptr);
    }
    
    int agent_sockfd;
    struct sockaddr_in agent_sockaddr;
    socklen_t len = sizeof(agent_sockaddr);

    while(1) {
        bzero( &agent_sockaddr , sizeof(agent_sockaddr) );

        agent_sockfd = accept(server_sockfd,(struct sockaddr*) &agent_sockaddr, &len);
        if( agent_sockfd < 0) {
            perror("accepting agent connection");
            continue;
        }

        printf("Transfer Connection found\n");

        bool ok = false;

        Agent* myAgent = nullptr;

        pthread_mutex_lock(&agent_list_lock);
        for (auto i : agent_list) {
            if ( agent_sockaddr.sin_addr.s_addr == i->agent_sockaddr.sin_addr.s_addr) {
                if ( i->agent_transfer_sd != -1) {
                    printf("WARNING: Overwriting existing transfersd!\n");
                    close(i->agent_transfer_sd);
                }
                i->agent_transfer_sd = agent_sockfd;
                ok = true;
                myAgent = i;
                break;
            }
        }
        pthread_mutex_unlock(&agent_list_lock);

        if ( ok == false ) {
            printf("Foreign Transfer Connection Found. Closing it..\n");
            close(agent_sockfd);
        }
        else {
            printf("Transfer Connection Found. Acknowledging it..\n");
            
            char ack = AGMSG_ACK;

            if ( 0 > send(myAgent->agent_control_sd, &ack, sizeof(ack), MSG_NOSIGNAL)) {
                perror("sending ack for legit transfer conn");
            }
        }
        
    }
    return nullptr;
}
