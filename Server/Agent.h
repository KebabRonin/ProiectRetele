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
    int agent_transfer_sd = -1;
    pthread_t agent_control_tid = 0;
    pthread_t agent_transfer_tid = 0;
    char id[20];
    //char syslog_format[200];

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

    if ( false == recv_varmsg(agent_control_sd, conn_info, MSG_NOSIGNAL)) {
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

void treat(const char* message) {
    switch(message[0]) {
        case AGMSG_ACK:
            switch(message[1]) {
                case AGMSG_NEW_IS:
                    if(message[2] == 0) {
                        //error
                    }
                    break;
                default:
                    printf("Unknown command ack\n");
            }
            break;
        case AGMSG_HEARTBEAT:
            printf("Inima mea bate\n");
            break;
        default:
            printf("Unknown msg type: %c\n", message[0]);
    }
}

void* fnc_agent_control_listener(void* p) {
    Agent* myAgent = (Agent*) p;

    fd_set actfd,readfd;
    FD_ZERO(&actfd);
    FD_SET(myAgent->agent_control_sd, &actfd);


    char   message[MSG_MAX_SIZE];

    timeval time;
    while(1) {
        time.tv_sec = 10;
        time.tv_usec = 0;
        memcpy(&readfd, &actfd, sizeof(actfd));

        int retval = select(myAgent->agent_control_sd + 1, &readfd, nullptr, nullptr, &time);
        if(0 > retval) {
            perror("select");
            break;
        }
        else if(!FD_ISSET(myAgent->agent_control_sd, &readfd)) {
            printf("time ran out\n");
            break;
        }

        bzero( message, sizeof(message) );
        if ( false == recv_varmsg(myAgent->agent_control_sd, message, MSG_NOSIGNAL) ) {
            perror("recv_varmsg() control");
            break;    
        }
        treat(message);
    }

    delete myAgent;
    return nullptr;
}

void* fnc_agent_transfer_listener(void* p) {
    Agent* myAgent = (Agent*) p;
    char log_path[100];
    sprintf(log_path,"logs/%s",myAgent->id);
    int log_path_base_len = strlen(log_path);
    
    char   message[MSG_MAX_SIZE];
    bzero( message, sizeof(message) );
    while(1) {
        if(recv_varmsg(myAgent->agent_transfer_sd,message, MSG_NOSIGNAL) == false) {
            perror("recv_varmsg() transfer");
            break;
        }
        else if(strlen(message) > 0) {
            printf("%s;\n",message);

            char* p = strchr(message, '\t'); ///! change \t to \0 in InfoSource.h too!!!
            *p = '\0';

            sprintf(log_path + log_path_base_len ,"/%s.log", message);
            printf("%s\n--\n",log_path);

            bool first_time = false;

            if ( 0 != access(log_path, F_OK)) {
                first_time = true;
            }

            int logfd = open(log_path,O_WRONLY | O_APPEND | O_CREAT, 0750); //u+rwx g+rx g-w o-rwx
            if (logfd < 0) {
                perror("Opening log");
                bzero( message, sizeof(message) );
                continue;
            }

            if (!first_time) {
                write(logfd, ",\n", strlen(",\n"));
            }

            write(logfd,p+1,strlen(p+1));

            close(logfd);

            bzero( message, sizeof(message) );
        }
    }

    //delete myAgent;
    printf("WARNING: Transfer thread exited\n");
    return nullptr;
}

bool Agent::init_transfer_connection() {

    int t = 0;
    const int timeout = 5;

    while(t < timeout) {
        printf("Waiting for transfer connection...\n");
        sleep(1);
        if(this->agent_transfer_sd != -1) {
            break;
        }
        t+=1;
    }
    if (t < timeout) {
        return true;
    }
    else {
        printf("Transfer init failed(timeout)\n");
        return false;
    }
}

void* fnc_agent_creator(void* p) {
    Agent* self = (Agent*) p;
    pthread_detach(pthread_self());
    //validate agent

    if ( false == self->login()) {
        delete self;
        pthread_exit(nullptr);
    }
    
    //register agent as online

    agent_list.push_back(self);


    //init transfer connection

    if ( false == self->init_transfer_connection()) {
        delete self;
        pthread_exit(nullptr);
    }

    //create listener threads

    if( 0 != pthread_create(&self->agent_control_tid, nullptr, fnc_agent_control_listener, self)) {
       perror("creating pthread");
       exit(1);
    }

    if( 0 != pthread_create(&self->agent_transfer_tid, nullptr, fnc_agent_transfer_listener, self)) {
       perror("creating pthread");
       exit(1);
    }

    printf("Agent operational\n");

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
    pthread_detach(pthread_self());
    if( agent_transfer_tid != 0 && agent_transfer_tid != pthread_self() && 0 != (err = pthread_tryjoin_np(agent_transfer_tid,&retval)) ) {
        if(err == EBUSY) {
            if( 0 != (err = pthread_cancel(agent_transfer_tid)) ) {
                perror("pthread_cancel()");
            }
            if(  0 != (err = pthread_join(agent_transfer_tid, &retval)) ) {
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
    if( agent_control_tid != 0 && agent_control_tid != pthread_self() && 0 != (err = pthread_tryjoin_np(agent_control_tid,&retval)) ) {
        printf("Asta trebuia sa fie inutila, inseamna ca altcineva a distrus un agent decat el insusi\n");
        if(err == EBUSY) {
            if( 0 != (err = pthread_cancel(agent_control_tid)) ) {
                perror("pthread_cancel()");
            }
            if(  0 != (err = pthread_join(agent_control_tid, &retval)) ) {
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
        agent_list.erase(agent_list.begin() + nr_ordine); nr_ordine -= 1;
        }
        nr_ordine += 1;
    }
    printf("Control thread: Agent destructed\n");
}
    
