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
#include "ag_cl_common.h"

#define AGENT_CONN_INFO_SIZE 20
extern pthread_mutex_t agent_list_lock;
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
    pthread_mutex_t ag_mutex = PTHREAD_MUTEX_INITIALIZER;
    void send_request(pthread_t tid, char type, const char* params, unsigned int len);

    Agent(sockaddr_in* agentsock, int* agent_control_fd);
    ~Agent();

    friend void* fnc_agent_creator(void*);
};

#include <vector>
extern std::vector<Agent*> agent_list;

void Agent::send_request(pthread_t tid, char type, const char* params, unsigned int len) {
    char message[MSG_MAX_SIZE+1]; bzero(message, MSG_MAX_SIZE);
    message[0] = type;
    ///@!
    memcpy(message + 1, &tid, sizeof(pthread_t));
    memcpy(message + 1 + sizeof(pthread_t), params, len);

    buffer_change_endian(message, 1 + sizeof(pthread_t) + len);

    pthread_mutex_lock(&ag_mutex);
    if (false == send_varmsg(agent_control_sd, message, 1 + sizeof(pthread_t) + len, MSG_NOSIGNAL)) {
        perror("send request");
    }
    pthread_mutex_unlock(&ag_mutex);
}

bool Agent::login() {
    char conn_info[MSG_MAX_SIZE+1]; bzero(conn_info, sizeof(conn_info));

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
            return false;
        }
    }
    else if (access(log_path, W_OK | X_OK) < 0) {
        perror("Not enough permissions");
        return false;
    }

    strcat(log_path, "/info");

    int infofd = open(log_path, O_CREAT | O_TRUNC | O_WRONLY, 0750);
    
    if (infofd < 0) {
        perror("open");
        return false;
    }

    int already_written = 0;
    while(already_written < strlen(conn_info)) {
        int wr = write(infofd, conn_info + already_written, strlen(conn_info) - already_written);
        if (wr <= 0) {
            perror("write");
            close(infofd);
            return false;
        }
        already_written += wr;
    }
    close(infofd);
    return true;
}

void treat(const char* message) {
    switch(message[0]) {
        case AGMSG_ACK:
        {
            ///@!
            pthread_t request_ID = (*((pthread_t*)(message+1)));
            Request* clreq = get_Request(request_ID);
            if (clreq != nullptr) {
                bcopy(message + 1 + sizeof(pthread_t), clreq->rsp, strlen(message + 1 + sizeof(pthread_t))+1);
            }
            else {
                printf("%s\n",message + 1 + sizeof(pthread_t));
            }
        /*    switch(message[1]) {
                case AGMSG_NEW_IS:
                    if(message[2] == 0) {
                        //error
                    }
                    break;
                default:
                    printf("Unknown command ack\n");
            }*/
            break;
        }
        case AGMSG_HEARTBEAT:
            #ifdef ag_debug
            printf(COLOR_AG_DEB);
            printf("Inima mea bate\n");
            printf(COLOR_OFF); fflush(stdout);
            #endif
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


    char   message[MSG_MAX_SIZE+1];
    int len;

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
        if ( false == (len = recv_varmsg(myAgent->agent_control_sd, message, MSG_NOSIGNAL) )) {
            #ifdef ag_debug
printf(COLOR_AG_DEB);
            perror("recv_varmsg() control");
            printf(COLOR_OFF);
fflush(stdout);
#endif
            break;    
        }
        buffer_change_endian(message, len);
        treat(message);
        if(myAgent->agent_transfer_sd == -1) {
            break;
        }
    }

    delete myAgent;
    return nullptr;
}

void* fnc_agent_transfer_listener(void* p) {
    Agent* myAgent = (Agent*) p;
    char log_path[100];
    sprintf(log_path,"logs/%s",myAgent->id);
    int log_path_base_len = strlen(log_path);
    int len;
    
    char   message[MSG_MAX_SIZE+1];

    while(1) {
        bzero( message, sizeof(message) );
        if( false == ( len = recv_varmsg(myAgent->agent_transfer_sd,message, MSG_NOSIGNAL))) {
#ifdef ag_debug
            printf(COLOR_AG_DEB);
            perror("recv_varmsg() transfer");
            printf(COLOR_OFF); fflush(stdout);
#endif
            break;
        }
        else if(len > 0) {
            buffer_change_endian(message, len);

            char* p = strchr(message, '\t'); ///! change \t to \0 in InfoSource.h too!!!
            *p = '\0';

            sprintf(log_path + log_path_base_len ,"/%s.log", message);

            bool first_time = false;

            if ( 0 != access(log_path, F_OK)) {
                first_time = true;
            }

            int logfd = open(log_path,O_WRONLY | O_APPEND | O_CREAT, 0750); //u+rwx g+rx g-w o-rwx
            if (logfd < 0) {
                perror("Opening log");
                continue;
            }

            if (!first_time) {
                if ( 0 > write(logfd, ",\n", strlen(",\n")) ) {
                    perror("write");
                }
            }

            if ( 0 > write(logfd,p+1,strlen(p+1))) {
                perror("write");
            }
            close(logfd);
        }
    }

    printf("WARNING:" COLOR_AGNAME " %s " COLOR_OFF "Transfer thread exited\n", myAgent->id);
    myAgent->agent_transfer_sd = -1;
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
    pthread_mutex_lock(&clreq_mutex);
    Agent* self = (Agent*) p;
    pthread_detach(pthread_self());
    //validate agent

    if ( false == self->login()) {
        delete self;
        pthread_exit(nullptr);
    }
    
    //register agent as online

    pthread_mutex_lock(&agent_list_lock);
    agent_list.push_back(self);
    pthread_mutex_unlock(&agent_list_lock);


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

    printf("Agent" COLOR_AGNAME " %s " COLOR_OFF "operational\n", self->id);
    pthread_mutex_unlock(&clreq_mutex);

    pthread_exit(nullptr);
}

Agent::Agent(sockaddr_in* agentsock, int* agent_control_fd) : agent_sockaddr(*agentsock), agent_control_sd(*agent_control_fd) {
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
    if( agent_control_tid != 0 && agent_control_tid != pthread_self()) {
        printf("Asta trebuia sa fie inutila, inseamna ca altcineva a distrus un agent decat el insusi\n");
        printf("self:%ld|control:%ld|transfer:%ld\n",pthread_self(),agent_control_tid, agent_transfer_tid);
        if(0 != (err = pthread_tryjoin_np(agent_control_tid,&retval)) ) {
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
    }
    close(agent_transfer_sd);
    close(agent_control_sd);
    int nr_ordine = 0;
    pthread_mutex_lock(&agent_list_lock);
    for(auto i : agent_list) {
        if (i == this) {
        agent_list.erase(agent_list.begin() + nr_ordine); nr_ordine -= 1;
        }
        nr_ordine += 1;
    }
    pthread_mutex_unlock(&agent_list_lock);
    printf("Control thread:" COLOR_AGNAME " %s " COLOR_OFF "Agent destructed\n", this->id);
}
    
