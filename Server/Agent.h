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
#include "../common_definitions.h"

#define AGENT_CONN_INFO_SIZE 20

struct Agent {
    struct sockaddr_in agent_sockaddr;
    int                agent_sockfd  ;
    int agent_pid;
    //enum AgentType {PC, CellTower, Mobile, IOT} type; //subclasses
    char id[10];
    char version[10];
    //char syslog_format[200];
    
    //static void* establish(void*);
    void get_conn_info(char*);
    static void* fnc_agent_listener(void*);

    Agent(sockaddr_in*, int*);
};

struct xorRand {
    static unsigned int my_state;
    static int rand() {
        my_state ^= (my_state << 13);
        my_state ^= (my_state >> 17);
        my_state ^= (my_state << 5 );
        return my_state;
    }
};
#include <time.h>
unsigned int xorRand::my_state = (unsigned int) time(nullptr);

void* Agent::fnc_agent_listener(void* p) {
    Agent* myAgent = (Agent*) p;
    char log_path[20];
    sprintf(log_path,"logs/%s",myAgent->id);
    if (access(log_path, F_OK) != 0) {
        if(mkdir(log_path,0750) < 0 ) {
            perror("Making path to log");
            return nullptr;
        }
    }
    else if (access(log_path, W_OK | X_OK) < 0) {
        perror("Not enough permissions");
        return nullptr;
    }
    sprintf(&log_path[strlen(log_path)],"/%s.log", myAgent->id);
    int logfd = open(log_path,O_WRONLY | O_APPEND | O_CREAT, 0750); //u+rwx g+rx g-w o-rwx
    if (logfd < 0 && errno != EEXIST) {
        perror("Opening log");
        pthread_exit(nullptr);
    }
    char   message[MSG_MAX_SIZE];
    bzero( message, sizeof(message) );
    while(1) {
        if(recv(myAgent->agent_sockfd,message,sizeof(message),0) < 0) {
            perror("recv()");
            break;
        }
        else if(strlen(message) > 0) {
            printf("%s\n",message);
            write(logfd,message,strlen(message));
            bzero( message, sizeof(message) );
        }
    }
    close(logfd);
    return nullptr;
}

void Agent::get_conn_info(char* conn_info) {
    strcpy(version,conn_info);
    printf("version:%s\n",version);
}

void rand_id(char g[]) {
    g[0] = 'I', g[1] = 'D', g[2] = '-';
    for(int i = 3; i < 10 - 1; ++i) {
        g[i] = '0' + ((unsigned char) xorRand::rand()) %10;
    }
    g[10 - 1] = '\0';
    printf("Generated %s\n",g);
}

Agent::Agent(sockaddr_in* agentsock, int* agentfd) : agent_sockaddr(*agentsock), agent_sockfd(*agentfd) {
    char conn_info[AGENT_CONN_INFO_SIZE];
    if(recv(agent_sockfd, conn_info,sizeof(conn_info), 0) < 0) {
        perror("recv() conn info");
        exit(1);
    }
    this->get_conn_info(conn_info);
    rand_id(this->id);
    switch (agent_pid = fork()) {
        case -1:
            perror("fork");
            exit(1);
        case 0:
            fnc_agent_listener(this);
            printf("Exited child function\n");
            exit(3);
    }
    //if( 0 != pthread_create(&agent_tid, nullptr, fnc_agent_listener, this)) {
    //    perror("creating pthread");
    //    exit(1);
    //}
    
} 
