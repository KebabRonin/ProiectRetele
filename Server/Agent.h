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
    char id[20];
    //char syslog_format[200];
    
    static void* fnc_agent_listener(void*);
    bool login();
    bool init_transfer_connection();

    Agent(sockaddr_in* agent_sockaddr, int* agent_transfer_sd);
    ~Agent();

    friend void* fnc_agent_creator(void*);
};

#include <vector>
extern std::vector<Agent*> agent_list;

bool Agent::login() {
    char conn_info[MSG_MAX_SIZE]; bzero(conn_info, sizeof(conn_info));

    if ( false == recv_varmsg(agent_control_sd, conn_info)) {
        return false;
    }

    printf("%s\n", conn_info);

    char* p = strstr(conn_info, ":");
    p += 2; //": "
    bzero(this->id, 20);
    strncpy(this->id,p,strchr(p,'\n') - p);

    char log_path[50];
    sprintf(log_path,"logs/%s",this->id);
    if (access(log_path, F_OK) != 0) {
        if(mkdir(log_path,0750) < 0 ) {
            perror("Making path to log");
            exit(4);
        }
    }
    else if (access(log_path, W_OK | X_OK) < 0) {
        perror("Not enough permissions");
        exit(4);
    }

    strcat(log_path, "/info");
    printf("\n--\n%s\n", log_path);

    int infofd = open(log_path, O_CREAT | O_TRUNC | O_WRONLY, 0750);
    
    if (infofd < 0) {
        perror("open");
        exit(4);
    }

    int already_written = 0;
    while(already_written < strlen(conn_info)) {
        int wr = write(infofd, conn_info + already_written, strlen(conn_info) - already_written);
        if (wr <= 0) {
            perror("write");
            exit(4);
        }
        already_written += wr;
    }
    close(infofd);
    return true;
}

void* Agent::fnc_agent_listener(void* p) {
    Agent* myAgent = (Agent*) p;
    char log_path[100];
    sprintf(log_path,"logs/%s",myAgent->id);
    int log_path_base_len = strlen(log_path);

    
    
    char   message[MSG_MAX_SIZE];
    bzero( message, sizeof(message) );
    while(1) {
        if(recv_varmsg(myAgent->agent_transfer_sd,message) == false) {
            perror("recv_varmsg()");
            break;
        }
        else if(strlen(message) > 0) {
            printf("%s;\n",message);

            char* p = strchr(message, '\t'); ///! change \t to \0 in InfoSource.h too!!!
            *p = '\0';

            sprintf(log_path + log_path_base_len ,"/%s.log", message);
            printf("%s\n--\n",log_path);
            int logfd = open(log_path,O_WRONLY | O_APPEND | O_CREAT, 0750); //u+rwx g+rx g-w o-rwx
            if (logfd < 0) {
                perror("Opening log");
                bzero( message, sizeof(message) );
                continue;
            }

            write(logfd,p+1,strlen(p+1));
            write(logfd,"\n",1);

            close(logfd);

            bzero( message, sizeof(message) );
        }
    }
    return nullptr;
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

Agent::~Agent() {
    void* retval = nullptr;
    int err;
    if( 0 != (err = pthread_tryjoin_np(agent_tid,&retval)) ) {
        if(err == EBUSY) {
            if( 0 != (err = pthread_cancel(agent_tid)) ) {
                perror("pthread_cancel()");
            }
            if(  0 != (err = pthread_join(agent_tid, &retval)) ) {
                perror("pthread_join()");
            }
        }
        else {
            perror("pthread_tryjoin()");
        }
    }
    else {
        //printf("pthread exited with %d", *(int*)retval);
    }
    close(agent_transfer_sd);
    close(agent_control_sd);
    int nr_ordine = 0;
    for(auto i : agent_list) {
        if (i == this) {
        agent_list.erase(agent_list.begin() + nr_ordine);
        }
        nr_ordine += 1;
    }
}
    
