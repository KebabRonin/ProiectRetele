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
#include <fstream>
#include <jsoncpp/json/json.h>
#include "Agent.h"
#include "../common_definitions.h"
#include "ag_cl_common.h"

extern pthread_mutex_t agent_list_lock;
extern std::vector<struct Agent*> agent_list;

struct clparam{
    int serv_sock;
    int param_len;
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

void get_all_agents(char* buf) {
    bzero(buf, MSG_MAX_SIZE);
    int pipefd[2], pid;
    pipe(pipefd);
    
    switch(pid = fork()) {
        case -1:
            perror("fork");
            pthread_exit(nullptr);
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
            pthread_exit(nullptr);
        }
        default:
            close(pipefd[1]);
            if ( 0 > waitpid(pid, nullptr, 0)) {
                perror("waitpid");
                pthread_exit(nullptr);
            }
    }
    if ( 0 > read(pipefd[0],buf, MSG_MAX_SIZE)) {
        perror("reading ag list from script");
    }
    close(pipefd[0]);
}

void* fnc_treat_client(void* p) {
    pthread_detach(pthread_self());
    struct clparam *cparam = (clparam*)p;
    int sockfd = cparam->serv_sock;
    char msg[MSG_MAX_SIZE];
    bzero(msg,MSG_MAX_SIZE);
    memcpy(msg, cparam->param, cparam->param_len); delete[] cparam->param;
    
    struct sockaddr_in clsock = *cparam->sock; delete[] cparam->sock;
    delete cparam;
    
    char type = msg[0];
    pthread_t msg_id = *((pthread_t*)(msg + 1));
    char* clreq = msg + 1 + sizeof(pthread_t);
    char to_send[MSG_MAX_SIZE];

#ifdef cl_debug
    printf(COLOR_CL_DEB);
    printf("ClReq Recieved:%s:from th %ld\n", clreq, msg_id);
    printf(COLOR_OFF); fflush(stdout);
#endif

    memcpy(to_send, &msg_id, sizeof(pthread_t));
    char* response = to_send + sizeof(pthread_t);
    bzero(response, MSG_MAX_SIZE - sizeof(pthread_t));

    
    switch (type) {
        case CLMSG_AGLIST: {
#ifdef cl_debug
            printf(COLOR_CL_DEB);
            printf("AGLIST\n");
            printf(COLOR_OFF);fflush(stdout);
#endif
            char buf[MSG_MAX_SIZE];

            get_all_agents(buf);

            char* p = strtok(buf, "\n");

            while(p != nullptr) {
                strcat(response,p);
                pthread_mutex_lock(&agent_list_lock);
                for(auto i : agent_list) {
                    if(strcmp(p,i->id) == 0)
                        strcat(response,"(*)");
                }
                pthread_mutex_unlock(&agent_list_lock);
                strcat(response,"\n");
                p = strtok(nullptr, "\n");
            }
            
            if(strlen(response) == 0) {
                sprintf(response, "No agents");
            }
            break;
        }
        case CLMSG_AGPROP: {
#ifdef cl_debug
            printf(COLOR_CL_DEB);
            printf("AGPROP\n");
            printf(COLOR_OFF);fflush(stdout);
#endif
            char buf[MSG_MAX_SIZE];

            get_all_agents(buf);

            char* p = strtok(buf, "\n");

            while(p != nullptr) {
                if(strcmp(p , clreq) == 0) {
                    char path[300];
                    sprintf(path,"logs/%s/info", p);
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
                p = strtok(nullptr, "\n");
            }
               
            if(strlen(response) == 0) {
                sprintf(response, "Unknown agent");
            }

            break;
        }
        case CLMSG_COUNT_QUERY: {
#ifdef cl_debug
            printf(COLOR_CL_DEB);
            printf("CLMSG_COUNT_QUERY\n");
            printf(COLOR_OFF);fflush(stdout);
#endif

            char* ag_name = clreq;
            char* my_path = strchr(ag_name, '\n');
            if (my_path == nullptr) {
                sprintf(response, "Can't find argument: Path");
                break;
            }
            my_path[0] = '\0';
            my_path += 1;
            char* my_cond = strchr(my_path, '\n');
            if (my_cond == nullptr) {
                sprintf(response, "Can't find argument: Conditions");
                break;
            }
            my_cond[0] = '\0';
            my_cond += 1;

            //parse conditions

            struct condition {
                char* name;
                char op;
                char* value;
            }conditions[100];
            int nr_cond = 0;
            {
                char* p = strtok(my_cond,"\"");
                while(p != nullptr) {
                    if(p[0] == '&') {
                        p += 1;
                    }
                    conditions[nr_cond].name = p;
                        if ( nullptr != (p = strchr(conditions[nr_cond].name,'='))) {
                        conditions[nr_cond].op = '=';
                        p[0] = '\0';
                        p += 1;
                    }
                    else if ( nullptr != (p = strchr(conditions[nr_cond].name,'!'))) {
                        conditions[nr_cond].op = '!';
                        p[0] = '\0';
                        p += 1;
                    }
                    else if ( nullptr != (p = strchr(conditions[nr_cond].name,'<'))) {
                        conditions[nr_cond].op = '<';
                        p[0] = '\0';
                        p += 1;
                    }
                    else if ( nullptr != (p = strchr(conditions[nr_cond].name,'>'))) {
                        conditions[nr_cond].op = '>';
                        p[0] = '\0';
                        p += 1;
                    }
                    else {
                        sprintf(response, "Invalid condition: Missing operator(=,!,>,<)");
                        goto send_response;
                    }
                    
                    p = strtok(NULL, "\"");

                    if(p == nullptr) {
                        sprintf(response, "Invalid condition: \"<value>\" not found");
                        goto send_response;
                    }

                    conditions[nr_cond].value = p;

                    nr_cond += 1;

                    p = strtok(NULL, "\"");
                }
            }

            if(nr_cond == 0) {
                sprintf(response, "Invalid conditions: no conditions");
                break;
            }

#ifdef cl_debug
            printf(COLOR_CL_DEB);
            printf("Found %d rules:\n",nr_cond);
            for(int i = 0; i < nr_cond; ++i) {
                printf("\t:%s: %c \"%s\"\n",conditions[i].name,conditions[i].op,conditions[i].value);
            }
            printf(COLOR_OFF); fflush(stdout);
#endif


            char buf[MSG_MAX_SIZE];

            get_all_agents(buf);

            {
                char* p = strtok(buf, "\n");

                while(p != nullptr) {
                    if(strcmp(p , ag_name) == 0) {
                        break;
                    }
                    p = strtok(nullptr, "\n");
                }

                if( p == nullptr) {
                    sprintf(response, "Unknown agent");
                    break;
                }   
            }

            char name[MSG_MAX_SIZE];
            sprintf(name, "%s%s/%s.log", LOG_PATH, ag_name, my_path);

            for ( int i = strlen(LOG_PATH) + strlen(ag_name) + 1; name[i] != '\0'; ++i) {
                if (name[i] == '/') {
                    name[i] = '_';
                }
            }

            int fd = open(name, O_RDONLY, 0);
            if (fd == -1) {
                perror("open");
                sprintf(response, "Error: opening log file");
                break;
            }

            char buffer[MSG_MAX_SIZE]; bzero(buffer, sizeof(buffer));
            int count = 0;
            do {
                
                {
                    int err;
                    int len = strlen(buffer);
                    if( 0 >= (err = read(fd, buffer + len, MSG_MAX_SIZE - strlen(buffer)))) {
                        
                        if (err == 0) {
                            if(strlen(buffer) == 0) {
                                break;
                            }
                        }
                        else {
                            perror("error");
                            break;
                        }
                    }
                    buffer[len + err] = '\0';
                }

                

                char* next_entry = strstr(buffer, "]}");
                if(next_entry == nullptr) {
                    break;
                }

                next_entry+=2;
                if(next_entry[0] == ',') {
                    next_entry[0] = '\0';
                    next_entry += 1;
                    if (next_entry[0] == '\n') next_entry+=1;
                }
                else if(next_entry[0] != 0) {
                    printf("============================\nUnexpected character: %c!!\n", next_entry[0]);
                    break;
                }

                //verify conditions

                int i;

                for (i = 0; i < nr_cond; ++i) {
                    char* p = buffer;
                    if(0 == strcmp(conditions[i].name,"rule")) {
                        char* possible_match = strstr(buffer, "rule");
                        if(possible_match == nullptr) {
                            printf("Something is wrong with the json file\n");
#ifdef cl_debug
                            printf(COLOR_CL_DEB);
                            printf("My entry has no rule field:\n%s\n\n", buffer);
                            printf(COLOR_OFF);
                            fflush(stdout);                     
#endif
                            //sprintf(response, "Database compromised");
                            //goto send_response;
                            goto unmatch;
                        }
                        possible_match += strlen("rule\":\"");
                        char* ending = strchr(possible_match, '\"');
                        if(ending == nullptr) {
                            printf("Something is wrong with the json file\n");
#ifdef cl_debug
                            printf(COLOR_CL_DEB); 
                            printf("My entry has no rule closing \" :\n%s\n\n", buffer);
                            printf(COLOR_OFF);
                            fflush(stdout);                     
#endif
                            //sprintf(response, "Database compromised");
                            //goto send_response;
                            goto unmatch;
                        }
                        ending[0] = '\0';
                        switch(conditions[i].op) {
                            case '=':
                                if(0 == strcmp(conditions[i].value, possible_match)) {
                                    ending[0] = '\"';
                                    goto match;
                                }
                                break;
                            case '!':
                                if(0 != strcmp(conditions[i].value, possible_match)) {
                                    ending[0] = '\"';
                                    goto match;
                                }
                                break;
                            case '<':
                                if(0 < strcmp(conditions[i].value, possible_match)) {
                                    ending[0] = '\"';
                                    goto match;
                                }
                                break;
                            case '>':
                                if(0 > strcmp(conditions[i].value, possible_match)) {
                                    ending[0] = '\"';
                                    goto match;
                                }
                                break;
                        }
                        ending[0] = '\"';
                    }
                    else {
                        while((p = strstr(p, conditions[i].name)) != nullptr) {
                            char* possible_match = p + strlen(conditions[i].name);
                            if(possible_match == strstr(possible_match,"\",\"value\":\"")) {
                                possible_match += strlen("\",\"value\":\"");
                                char* ending = strchr(possible_match, '\"');
                                if(ending == nullptr) {
                                    printf("Something is wrong with the json file\n");
#ifdef cl_debug
                                    printf(COLOR_CL_DEB); 
                                    printf("My entry has no value closing \" :\n%s\n\n", buffer);
                                    printf(COLOR_OFF);
                                    fflush(stdout);                     
#endif
                                    //sprintf(response, "Database compromised");
                                    //goto send_response;
                                    goto unmatch;
                                }
                                ending[0] = '\0';
                                if(possible_match - strlen("number\",\"name\":\"") > buffer && strstr(possible_match - strlen("number\",\"name\":\""), "number") == possible_match - strlen("number\",\"name\":\"")) {
                                    switch(conditions[i].op) {
                                    case '=':
                                        if(atoi(conditions[i].value) == atoi(possible_match)) {
                                            ending[0] = '\"';
                                            goto match;
                                        }
                                        break;
                                    case '!':
                                        if(atoi(conditions[i].value) != atoi(possible_match)) {
                                            ending[0] = '\"';
                                            goto match;
                                        }
                                        break;
                                    case '<':
                                        if(atoi(conditions[i].value) < atoi(possible_match)) {
                                            ending[0] = '\"';
                                            goto match;
                                        }
                                        break;
                                    case '>':
                                        if(atoi(conditions[i].value) > atoi(possible_match)) {
                                            ending[0] = '\"';
                                            goto match;
                                        }
                                        break;
                                    }
                                }
                                else {
                                    switch(conditions[i].op) {
                                    case '=':
                                        if(0 == strcmp(conditions[i].value, possible_match)) {
                                            ending[0] = '\"';
                                            goto match;
                                        }
                                        break;
                                    case '!':
                                        if(0 != strcmp(conditions[i].value, possible_match)) {
                                            ending[0] = '\"';
                                            goto match;
                                        }
                                        break;
                                    case '<':
                                        if(0 < strcmp(conditions[i].value, possible_match)) {
                                            ending[0] = '\"';
                                            goto match;
                                        }
                                        break;
                                    case '>':
                                        if(0 > strcmp(conditions[i].value, possible_match)) {
                                            ending[0] = '\"';
                                            goto match;
                                        }
                                        break;
                                    }
                                }
                                
                                ending[0] = '\"';
                                p += 1;
                            }
                            p += 1;
                        }
                    }
unmatch:
                    break;
match:              ;
                }

                if(i==nr_cond) {
                    count+=1;
                }

                bzero(buffer, strlen(buffer));
                strcpy(buffer, next_entry);

            }while(strlen(buffer) > 0);

            close(fd);

            sprintf(response, "%d", count);

            break;
        }
        default: {
#ifdef cl_debug
            printf(COLOR_CL_DEB);
            printf("Some command : %c\n", type);
            printf(COLOR_OFF);fflush(stdout);
#endif

            //verify params
            char* my_id = clreq;
            char* next_params = strchr(my_id, '\n'); 
            if (next_params != nullptr ) {
                next_params[0] = '\0'; 
                next_params += 1;
            }

#ifdef cl_debug
            printf(COLOR_CL_DEB);
            printf("my_id:%s:\nnext_params:%s:\n",my_id, next_params);
            printf(COLOR_OFF);fflush(stdout);
#endif

            Agent* my_agent = nullptr;

            pthread_mutex_lock(&agent_list_lock);
            for(auto i : agent_list) {
                if (0 == strcmp(i->id, my_id)) {
                    my_agent = i;
                }
            }
            pthread_mutex_unlock(&agent_list_lock);

            if (my_agent == nullptr) {
                strcpy(response, "Error : Agent is not online");
                break;
            }

            Request myReq(pthread_self(), response);
            register_Request(&myReq);

#ifdef ag_debug
            printf(COLOR_AG_DEB);
            printf("REGISTERED THREAD %lu\n", pthread_self());
            printf(COLOR_OFF);fflush(stdout);
#endif

            my_agent->send_request(pthread_self(), type, next_params, next_params == nullptr ? 0 : strlen(next_params));
            
            //wait for response
            
            do {
                sleep(1);
            }while(strlen(myReq.rsp) == 0);
            
#ifdef ag_debug
            printf(COLOR_AG_DEB);
            printf("request!\n");
            printf(COLOR_OFF);fflush(stdout);
#endif

            unregister_Request(&myReq);
            break;
        }
    }
send_response:
#ifdef cl_debug
    printf(COLOR_CL_DEB);
    printf("Sending to client:%s:\n",response);
    printf(COLOR_OFF);fflush(stdout);
#endif
    int t_len = sizeof(pthread_t) + strlen(response);
    buffer_change_endian(to_send, t_len);
    if ( 0 > sendto(sockfd, to_send, t_len, 0, (struct sockaddr*)&clsock, sizeof(clsock)) ) {
        perror("sendto");
        pthread_exit(nullptr);
    }
    
    pthread_exit(nullptr);
}

void* fnc_client_dispatcher(void*) {
    
    struct sockaddr_in client_sockaddr;  bzero( &client_sockaddr , sizeof(client_sockaddr ) );

    int server_sockfd = init_server_to_port_UDP(CLIENT_PORT);
    if(server_sockfd == -1) {
        printf("Error initialising client port\n");
        exit(3);
    }
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
        cparam->param = new char[len + 1]; memcpy(cparam->param, buf, len); cparam->param[len] = '\0';
        cparam->param_len = len;
        cparam->sock = new sockaddr_in; bcopy(&client_sockaddr,cparam->sock, length);
        pthread_t tid;
        if ( 0 > pthread_create(&tid, nullptr, fnc_treat_client, (void*)cparam) ) {
            perror("pthread_create");
            exit(3);
        }
    }
    return nullptr;
}
