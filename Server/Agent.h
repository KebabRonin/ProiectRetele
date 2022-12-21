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
    int agent_control_sd ;
    int agent_transfer_sd;
    pthread_t agent_tid;
    //enum AgentType {PC, CellTower, Mobile, IOT} type; //subclasses
    char id[10];
    char version[10];
    //char syslog_format[200];
    
    //static void* establish(void*);
    void get_conn_info(char*);
    static void* fnc_agent_listener(void*);
    bool login() {return true;}
    bool init_transfer_connection();

    Agent(sockaddr_in* agent_sockaddr, int* agent_transfer_sd);

    friend void* fnc_agent_creator(void*);
};

#include <vector>
extern std::vector<Agent*> agent_list;

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
        if(recv(myAgent->agent_transfer_sd,message,sizeof(message),0) < 0) {
            perror("recv()");
            break;
        }
        else if(strlen(message) > 0) {
            //printf("%s\n",message);
            write(logfd,message,strlen(message));
            write(logfd,"\n",1);
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

bool Agent::init_transfer_connection() {

    //create randevous socket

    int agent_transfer_sd_temp = socket(AF_INET, SOCK_STREAM, 0);
    if ( agent_transfer_sd_temp < 0 ) {
        perror("creating transfer socket for agent");
        return false;
    }

    struct sockaddr_in transfer_sockaddr;
    transfer_sockaddr.sin_family = AF_INET;
    transfer_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    transfer_sockaddr.sin_port = 0;

    if ( 0 > bind(agent_transfer_sd_temp, (struct sockaddr*) &transfer_sockaddr, (socklen_t) sizeof(transfer_sockaddr) ) ) {
        perror("creating transfer socket for agent");
        close(agent_transfer_sd_temp);
        return false;
    }

    if ( -1 == listen(agent_transfer_sd_temp, 5) ) {
        perror("Error listening socket");
        close(agent_transfer_sd_temp);
        return false;
    }

    //send port to agent

    struct sockaddr_in randevous; 
    bzero(&randevous,sizeof(randevous));
    socklen_t len = sizeof(randevous);

    getsockname(agent_transfer_sd_temp, (struct sockaddr*) &randevous, &len);

    printf("Expecting port: %d\n",randevous.sin_port);

    unsigned short port = htons(randevous.sin_port);

    printf("Expecting port: %d\n",randevous.sin_port);

    if ( 0 > send(agent_control_sd, &port, sizeof(port), 0)) {
        perror("sending agent control");
        exit(2);
    }

    //wait for connection

    struct sockaddr_in ag_temp; 
    len = sizeof(ag_temp);

    while (1) {
        
        bzero(&ag_temp, sizeof(ag_temp));

        agent_transfer_sd = accept(agent_transfer_sd_temp, (struct sockaddr*) &ag_temp, &len);
        if ( agent_transfer_sd < 0 ) {
            perror("accepting transfer connection");
            close(agent_transfer_sd_temp);
            return false;
        }

        printf("Myaddr: %d\tHisaddr: %d\n", agent_sockaddr.sin_addr.s_addr, ag_temp.sin_addr.s_addr);

        if(ag_temp.sin_addr.s_addr == agent_sockaddr.sin_addr.s_addr) {
            printf("Conn accepted\n");
            break;
        }
        else {
            printf("Unexpected agent connection recieved (expected %d, recieved %d)\n", agent_sockaddr.sin_addr.s_addr, ag_temp.sin_addr.s_addr);
            close(agent_transfer_sd);
        }

    }
    
    close(agent_transfer_sd_temp);
    return true;
}

void* fnc_agent_creator(void* p) {
    Agent* self = (Agent*) p;
    pthread_detach(pthread_self());
    
    //validate agent

    if ( false == self->login()) {
        delete self;
        pthread_exit(nullptr);
    }

    //init transfer connection

    if ( false == self->init_transfer_connection()) {
        delete self;
        pthread_exit(nullptr);
    }

    //create listener thread

    if( 0 != pthread_create(&self->agent_tid, nullptr, Agent::fnc_agent_listener, self)) {
       perror("creating pthread");
       exit(1);
    }

    //register agent as online

    agent_list.push_back(self);

    printf("Agent online\n");
    
    pthread_exit(nullptr);
}

Agent::Agent(sockaddr_in* agentsock, int* agentfd) : agent_sockaddr(*agentsock), agent_control_sd(*agentfd) {
    pthread_t temp_tid;

    if( 0 != pthread_create(&temp_tid, nullptr, fnc_agent_creator, this)) {
       perror("creating pthread");
       exit(1);
    }    
    
} 
