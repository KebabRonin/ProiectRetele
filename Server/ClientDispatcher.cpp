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
#include <sys/wait.h>
#include <pthread.h>
#include <vector>
#include "Agent.h"
#include "../common_definitions.h"
#include "ag_cl_common.h"

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
    
    printf("ClReq Recieved:%s:\n",clreq);
    char type = clreq[0];
    char response[MSG_MAX_SIZE]; bzero(response, MSG_MAX_SIZE);
    switch (type) {
        case CLMSG_AGLIST: {
            printf("AGLIST\n");
            int pid;
            int pipefd[2];
            pipe(pipefd);
            
            switch(pid = fork()) {
                case -1:
                    perror("fork");
                    return nullptr;
                case 0: {
                    close(pipefd[0]);

                    int fd = open("aglist_script", O_CREAT | O_TRUNC | O_WRONLY, 0750);
                    write(fd, "ls -l logs/ | grep ^d | awk \'{print $9}\'", strlen("ls -l logs/ | grep ^d | awk \'{print $9}\'"));
                    close (fd);

                    if (dup2(pipefd[1], 1) == -1) {
                        perror("dup2");
                        exit(2);
                    }
                    execlp("./aglist_script","aglist_script", nullptr);
                    perror("execlp");
                    return nullptr;
                }
                default:
                    close(pipefd[1]);
                    if ( 0 > waitpid(pid, nullptr, 0)) {
                        perror("waitpid");
                        return nullptr;
                    }
            }

            char buf[MSG_MAX_SIZE]; bzero(buf, MSG_MAX_SIZE);
            if ( 0 > read(pipefd[0],buf, MSG_MAX_SIZE)) {
                perror("reading ag list from script");
            }
            close(pipefd[0]);
            char* p = strtok(buf, "\n");
            while(p != nullptr && strlen(p) > 0) {
                strcat(response,p);
                for(auto i : agent_list) {
                    if(strcmp(p,i->id) == 0)
                        strcat(response,"(*)");
                }
                strcat(response,"\n");
                p = strtok(nullptr, "\n");
            }
            
            if(strlen(response) == 0) {
                sprintf(response, "No agents");
            }
            break;
        }
        case CLMSG_AGPROP: {
            printf("AGPROP\n");
            for(auto i : agent_list) {
                if(strcmp(i->id, clreq+1) == 0) {
                    char path[300];
                    sprintf(path,"logs/%s/info",i->id);
                    int infofd = open(path, O_RDONLY);
                    if (infofd < 0) {
                        perror("open");
                        sprintf(response,"Unable to open info");
                        break;
                    }
                    if (0 > read(infofd, response, MSG_MAX_SIZE)) {
                        sprintf(response, "Error reading");
                        break;
                    }
                    close(infofd);
                    break;
                }
            }
            if(strlen(response) == 0) {
                sprintf(response, "Unknown agent");
            }
            break;
        }
        case CLMSG_ADDSRC: {
            printf("ADDSRC\n");

            //verify params
            char* my_id = clreq + 1;
            char* my_path = strchr(clreq, '\n');
            if (my_path == nullptr) {
                strcpy(response, "Error : Insufficient params");
                break;
            }

            my_path[0] = '\0';
            my_path = my_path + 1;

            Agent* my_agent = nullptr;

            for(auto i : agent_list) {
                if (0 == strcmp(i->id, my_id)) {
                    my_agent = i;
                }
            }

            if (my_agent == nullptr) {
                strcpy(response, "Error : Invalid Agent");
                break;
            }

            Request myReq(pthread_self(), response);
            register_Request(&myReq);

            printf("REGISTERED THREAD %lu\n", pthread_self());

            my_agent->send_request(pthread_self(), AGMSG_NEW_IS, my_path, strlen(my_path));
            
            //wait for response
            
            do {
                sleep(1);
            }while(strlen(myReq.rsp) == 0);

            printf("request!\n");

            unregister_Request(&myReq);
            break;
        }
        default:
            strcpy(response,"Unknown command");
    }
    printf("Sending to client:%s:\n",response);
    buffer_change_endian(response, strlen(response));
    if ( 0 > sendto(sockfd, response, strlen(response), 0, (struct sockaddr*)&clsock, sizeof(clsock)) ) {
        perror("sendto");
        pthread_exit(nullptr);
    }
    
    pthread_exit(nullptr);
}

void* fnc_client_dispatcher(void*) {
    
    struct sockaddr_in client_sockaddr;  bzero( &client_sockaddr , sizeof(client_sockaddr ) );

    int server_sockfd = init_server_to_port_UDP(CLIENT_PORT);
    fd_set actfds,readfds;

    FD_ZERO(&actfds);
    FD_SET(server_sockfd,&actfds);

    char buf[MSG_MAX_SIZE];
    int len;

    while(1) {
        bzero(buf,sizeof(buf));
        int length = sizeof (client_sockaddr);
        bcopy ((char *) &actfds, (char *) &readfds, sizeof (readfds));
        if (select(server_sockfd+1, &readfds, nullptr, nullptr, nullptr) < 0) {
            perror("select()");
            exit(3);
        }
        if( 0 > (len = recvfrom(server_sockfd,buf,MSG_MAX_SIZE, 0,(sockaddr*) &client_sockaddr,(socklen_t*) &length))) {
            perror("recv_from");
            continue;
        }
        struct clparam* cparam = new struct clparam;
        cparam->serv_sock = server_sockfd;
        buffer_change_endian(buf, len);
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
