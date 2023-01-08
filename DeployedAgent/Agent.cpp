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
#include <sys/wait.h>
#include <vector>
#include "InfoSource.h"

#define AGENT_PORT 2077

pthread_mutex_t transfer_sock_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t sources_mutex = PTHREAD_MUTEX_INITIALIZER;

int control_sd, transfer_sd;
int keep_on_keeping_on = true;

bool get_stdin_approval(int time) {
    fd_set actfd;
    FD_ZERO(&actfd);
    FD_SET(0,&actfd);
    
    timeval tv; tv.tv_sec = (time_t) time; tv.tv_usec = 0;
    
    if(0 > select(0 + 1, &actfd, nullptr, nullptr, &tv)) {
        perror("Select");
        return false;
    }
    if (FD_ISSET(0, &actfd)) {
        char ch = '\0';
        
        if(0 > read(0, &ch, sizeof(ch))) {
            perror("read");
            return false;
        }

        if (ch == 'y') {
            return true;
        }
        else {
            return false;
        }
    }
    else {
        return false;
    }
}

bool init_control_connection(const char* ip) {

    control_sd = socket(AF_INET, SOCK_STREAM, 0);
    if( -1 == control_sd ) {
        perror("socket()");
        return false;
    }

    struct sockaddr_in server_sockaddr;

    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = inet_addr(ip);
    server_sockaddr.sin_port = htons (AGENT_CONTROL_PORT);

    while ( -1 == connect(control_sd, (struct sockaddr*) &server_sockaddr, sizeof(server_sockaddr))) {
        perror("Couldn't connect to server");
        sleep(5);
    }

    return true;
}

bool init_transfer_connection(const char* ip) {

    struct sockaddr_in server_sockaddr; 
    bzero(&server_sockaddr, sizeof(server_sockaddr));

    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = inet_addr(ip);
    server_sockaddr.sin_port = htons (AGENT_TRANSFER_PORT);

    fd_set actfd, readfd;
    FD_ZERO(&actfd);
    FD_SET(control_sd, &actfd);

    timeval time;

    while (1) {
        printf("Initialising transfer conn..\n");

        transfer_sd = socket(AF_INET, SOCK_STREAM, 0);
        if( -1 == transfer_sd ) {
            perror("socket()");
            return false;
        }

        while ( -1 == connect(transfer_sd, (struct sockaddr*) &server_sockaddr, sizeof(server_sockaddr))) {
            perror("Couldn't initialise transfer connection to server");
            sleep(1);
        }


        memcpy(&readfd, &actfd, sizeof(actfd));

        time.tv_sec = 1;
        time.tv_usec = 0;

        if ( 0 > select (control_sd + 1, &readfd, nullptr, nullptr, &time)) {
            perror("select");
            close(transfer_sd);
            return false;
        }
        printf("Recieved ack, with %ld s %ld ms time left till timeout\n",time.tv_sec, time.tv_usec);
        if (FD_ISSET(control_sd, &readfd)) {
            
            char ack = '\0';
            if ( false == recv_fixed_length(control_sd, &ack, sizeof(ack), MSG_NOSIGNAL) ) {
                perror("Recieving transfer ack");
                close(transfer_sd);
                return false;
            }
            if (ack == AGMSG_ACK) {
                //conn established
                printf("Recieved transfer ack\n");
                break;
            }
            else {
                printf("recieved unknown ack!!\n");
                close(transfer_sd);
                return false;
            }
        }
        else {
            close(transfer_sd);
        }
        
    }

    return true;
}

bool login() {

    char conn_info[MSG_MAX_SIZE]; bzero(conn_info, sizeof(conn_info));

    int infofd = open("agent.info",O_RDONLY, 0);

    if(infofd < 0) {
        perror("open");
        return false;
    }

    int already_read = 0, rd = 0;

    while (0 < (rd = read(infofd, conn_info + already_read, sizeof(conn_info) - already_read)) ) already_read += rd;

    close(infofd);

    if (false == send_varmsg(control_sd, conn_info, strlen(conn_info), MSG_NOSIGNAL)) {
        printf("Error sending login\n");
        return false;
    }
    return true;
}

void init_comms_to_server(const char* ip) {
CommRetry:
    if (false == init_control_connection(ip) ) {
        goto CommRetry;
    }

    if (false == login() ) {
        printf("Login failed.\n");
        close(control_sd);
        goto CommRetry;
    }

    if (false == init_transfer_connection(ip) ) {
        close(control_sd);
        goto CommRetry;
    }

    printf("Connected to server!\n");
}

void init_agent_info() {
    int infofd = open("agent.info",O_WRONLY | O_CREAT, 0750);
    int pid;
    switch(pid = fork()) {
        case -1:
            perror("fork");
            exit(3);
        case 0 :
            dup2(infofd, 1);
            execlp("hostnamectl", "Agent info gatherer", NULL);
            perror("execlp");
            exit(3);
    }

    int stat;
    if ( -1 == waitpid(pid, &stat, 0) ) {
        perror("waitpid");
    }
    else if(stat != 0) {
        printf("error hostnamectl\n");
    }
    close(infofd);

}

std::vector<InfoSource*> sources;

bool send_ack(pthread_t request_ID, const char* info, const unsigned int info_size) {
    char ack[MSG_MAX_SIZE]; bzero(ack,sizeof(ack));

    if (info_size + 3 > MSG_MAX_SIZE) {
        printf("WARNING: ack info overflows MSG_MAX_SIZE\n");
    }

    ack[0] = AGMSG_ACK;
    ///@!
    memcpy(ack+1, &request_ID , sizeof(request_ID));
    memcpy(ack+1+sizeof(request_ID), info, info_size);

    buffer_change_endian(ack, 1 + sizeof(request_ID) + info_size);

    if ( false == send_varmsg(control_sd, ack, info_size + sizeof(request_ID) + 1, MSG_NOSIGNAL)) {
        perror("send");
        return false;
    }
    return true;
}

bool treat (char * command) {
    ///@!
    pthread_t request_ID = (*((pthread_t*) (command + 1)));
    char response[MSG_MAX_SIZE]; bzero(response, sizeof(response));
    char* params = command + 1 + sizeof(request_ID);
#ifdef cl_debug
    printf(COLOR_CL_DEB);
    printf("ClReq Recieved:%s:\n",params);
    printf(COLOR_OFF); fflush(stdout);
#endif
    char* p = strtok(params, "\n");
    while(p != nullptr) {
        p = strtok(NULL, "\n");
    }
    switch(command[0]){
        case CLMSG_ADDSRC: {
                InfoSource* to_add = createIS(params);
                bool found = false;
                pthread_mutex_lock(&sources_mutex);
                for (auto i : sources) {
                    if (i == to_add) {
                        found = true;
                        break;
                    }
                }
                pthread_mutex_unlock(&sources_mutex);
                if (found) {
                    strcpy(response,"Success");
                }
                else {
                    strcpy(response,"Failure");
                }
                break;
            }
        case CLMSG_ADDRLE: {
            InfoSource* my_is = nullptr;

            pthread_mutex_lock(&sources_mutex);
            for(auto i : sources) {
                if(0 == strcmp(i->path, params)) {
                    my_is = i;
                    break;
                }
            }
            pthread_mutex_unlock(&sources_mutex);

            if (my_is == nullptr) {
                strcpy(response, "Error: Unknown InfoSource");
                break;
            }

            char* p = strchr(params + strlen(params) + 1, '|');
            if (p == nullptr) {
                strcpy(response,"Invalid format");
                break;
            }

            p[0] = '\0';

            if(true == my_is->has_rule(params + strlen(params) + 1)) {
                strcpy(response, "Error: Rule name already in use");
                break;
            }

            p[0] = '|';
            
            if(false == my_is->add_rule(params + strlen(params) + 1)) {
                strcpy(response, "Error: Couldn't add rule");
                break;
            }
            strcpy(response, "Success");

            break;
        }
        case CLMSG_RMVRLE: {
            InfoSource* my_is = nullptr;

            pthread_mutex_lock(&sources_mutex);
            for(auto i : sources) {
                if(0 == strcmp(i->path, params)) {
                    my_is = i;
                    break;
                }
            }
            pthread_mutex_unlock(&sources_mutex);

            if (my_is == nullptr) {
                strcpy(response, "Error: Unknown InfoSource");
                break;
            }

            char* rule_name = params + strlen(params) + 1;
            if (strlen(rule_name) == 0) {
                strcpy(response,"Invalid format");
                break;
            }

            char name[MSG_MAX_SIZE];
            sprintf(name, "%s.fmt", my_is->path);
            for ( int i = 0; name[i] != '\0'; ++i) {
                if (name[i] == '/') {
                    name[i] = '_';
                }
            }

            int old_fd = open(name, O_RDONLY, 0);
            if (old_fd < 0) {
                perror("open");
                strcpy(response, "Error: Opening rule file");
                break;
            }

            char new_name[strlen(name) + 1];
            sprintf(new_name,"%s~", name);

            int new_fd = open(new_name, O_WRONLY | O_CREAT | O_TRUNC, 0750);
            if (new_fd < 0) {
                perror("open");
                strcpy(response, "Error: Making new file");
                break;
            }

            char rule[MSG_MAX_SIZE]; bzero(rule, MSG_MAX_SIZE);
            bool first_time = true;
            bool found = false;

            if (false == read_fmt_entry(old_fd, rule)) {
                sprintf(response, "Error: reading fmt file");
                goto after_treat;
            }

            while(strlen(rule) > 0) {
                char* ending = strchr(rule, '\n');
                if (ending != nullptr) {
                    ending[0] = '\0';
                }

                char* p = strchr(rule, '|');
                if(p == nullptr) {
                    printf("Error: Rule not well formatted in %s, removing it\n", name);
                }
                
                
                if(strstr(rule, rule_name) == rule && rule[strlen(rule_name)] == '|') {
                    found = true;
                }
                else if (p != nullptr) {//is formatted ok
                    if(!first_time) {
                        if ( 0 > write(new_fd, "\n", strlen("\n")) ) {
                            perror("write");
                            strcpy(response, "Error: Making new file");
                            close(old_fd);
                            close(new_fd);
                            goto after_treat;
                        }
                    }
                    if ( 0 > write(new_fd, rule, strlen(rule)) ) {
                        perror("write");
                        strcpy(response, "Error: Making new file");
                        close(old_fd);
                        close(new_fd);
                        goto after_treat;
                    }
                    first_time = false;
                }
                
                if (ending != nullptr) {
                    ending[0] = '\n';
                }

                if (false == read_fmt_entry(old_fd, rule)) {
                    sprintf(response, "Error: reading fmt file");
                    goto after_treat;
                }
            }

            close(old_fd);
            close(new_fd);

            pthread_mutex_lock(&my_is->rules_file_mutex);
            switch(int pid = fork()) {
                case -1: 
                    perror("fork");
                    strcpy(response, "Error: Making new file");
                    goto after_treat;
                case 0 :
                    execlp("mv", "mv", "-f", new_name, name, NULL);
                    perror("execlp");
                    exit(2);
                default:{
                    int stat;
                    if ( -1 == waitpid(pid, &stat, 0) ) {
                        perror("waitpid");
                    }
                    else if(stat != 0) {
                        printf("error mv\n");
                    }
                }
                    
            }
            pthread_mutex_unlock(&my_is->rules_file_mutex);


            if (found) {
                strcpy(response, "Success");
            }
            else {
                strcpy(response, "Error: Rule not found");
            }

            break;
        }
        case CLMSG_AG_LIST_SOURCES: {
            pthread_mutex_lock(&sources_mutex);
            for(auto i : sources) {
                strcat(response, i->path);
                strcat(response, "\n");
            }
            pthread_mutex_unlock(&sources_mutex);
            break;
        }  
        case CLMSG_AG_HOWMANY_RULEPAGES: {
            InfoSource* my_is = nullptr;

            pthread_mutex_lock(&sources_mutex);
            for(auto i : sources) {
                if(0 == strcmp(i->path, params)) {
                    my_is = i;
                    break;
                }
            }
            pthread_mutex_unlock(&sources_mutex);

            if (my_is == nullptr) {
                strcpy(response, "Error: Unknown InfoSource");
                break;
            }

            char name[MSG_MAX_SIZE];
            sprintf(name, "%s.fmt", my_is->path);
            for ( int i = 0; name[i] != '\0'; ++i) {
                if (name[i] == '/') {
                    name[i] = '_';
                }
            }

            int fd = open(name, O_RDONLY, 0);
            if(fd < 0) {
                strcpy(response, "Error: Couldn't open rule file");
                break;
            }

            char entry[MSG_MAX_SIZE]; bzero(entry, MSG_MAX_SIZE);
            int nr_entries = 0;

            if (false == read_fmt_entry(fd, entry)) {
                sprintf(response, "Error: reading fmt file");
                goto after_treat;
            }

            while(strlen(entry) > 0) {
                nr_entries += 1;

                char* ending = strchr(entry, '\n');
                if (ending != nullptr) {
                    ending[0] = '\0';
                }

                char* p = strchr(entry, '|');
                if(p == nullptr) {
                    nr_entries -= 1;
                    printf("Error: Rule not well formatted in %s, after %d-th rule\n", name, nr_entries);
                }

                if (ending != nullptr) {
                    ending[0] = '\n';
                }

                if (false == read_fmt_entry(fd, entry)) {
                    sprintf(response, "Error: reading fmt file");
                    goto after_treat;
                }
            }
            close(fd);

            sprintf(response, "%d", nr_entries/ENTRIESPERPAGE + (nr_entries % ENTRIESPERPAGE > 0));

            break;
        }  
        case CLMSG_AG_LIST_RULEPAGE: {
            InfoSource* my_is = nullptr;

            pthread_mutex_lock(&sources_mutex);
            for(auto i : sources) {
                if(0 == strcmp(i->path, params)) {
                    my_is = i;
                    break;
                }
            }
            pthread_mutex_unlock(&sources_mutex);

            if (my_is == nullptr) {
                strcpy(response, "Error: Unknown InfoSource");
                break;
            }

            int page = atoi(params + strlen(params) + 1);
            if (page == 0) {
                strcpy(response,"Invalid page");
                break;
            }

            char name[MSG_MAX_SIZE];
            sprintf(name, "%s.fmt", my_is->path);
            for ( int i = 0; name[i] != '\0'; ++i) {
                if (name[i] == '/') {
                    name[i] = '_';
                }
            }

            int fd = open(name, O_RDONLY, 0);
            if(fd < 0) {
                strcpy(response, "Error: Couldn't open rule file");
                break;
            }

            char entry[MSG_MAX_SIZE]; bzero(entry, MSG_MAX_SIZE);
            int nr_entries = 0;

            if (false == read_fmt_entry(fd, entry)) {
                sprintf(response, "Error: reading fmt file");
                goto after_treat;
            }

            while(strlen(entry) > 0 && (nr_entries/ENTRIESPERPAGE) + 1 <= page + 1) {
                nr_entries += 1;
                if(((nr_entries/ENTRIESPERPAGE) + 1 == page && (nr_entries%ENTRIESPERPAGE) != 0) || ((nr_entries/ENTRIESPERPAGE) + 1 == page + 1 && (nr_entries%ENTRIESPERPAGE) == 0)) {
                    char* ending = strchr(entry, '\n');
                    if (ending != nullptr) {
                        ending[0] = '\0';
                    }
                    
                    char* p = strchr(entry, '|');
                    if(p == nullptr) {
                        nr_entries -= 1;
                        printf("Error: Rule not well formatted in %s, after %d-th rule\n", name, nr_entries);
                    }
                    else {
                        p[0] = '\0';
                        strcat(response, entry);
                        strcat(response, ",");
                        p[0] = '|';
                    }
                    
                    if (ending != nullptr) {
                        ending[0] = '\n';
                    }
                }
                if (false == read_fmt_entry(fd, entry)) {
                    sprintf(response, "Error: reading fmt file");
                    goto after_treat;
                }
            }
            close(fd);
            if(strlen(response) > 0) {
                response[strlen(response) - 1] = '\0';
            }
            else {
                sprintf(response, "Error: Invalid page");
            }


            break;
        }
        case CLMSG_AG_SHOW_RULE: {
            InfoSource* my_is = nullptr;

            pthread_mutex_lock(&sources_mutex);
            for(auto i : sources) {
                if(0 == strcmp(i->path, params)) {
                    my_is = i;
                    break;
                }
            }
            pthread_mutex_unlock(&sources_mutex);

            char* my_entry = params + strlen(params) + 1;

            if (my_is == nullptr) {
                strcpy(response, "Error: Unknown InfoSource");
                break;
            }

            char name[MSG_MAX_SIZE];
            sprintf(name, "%s.fmt", my_is->path);
            for ( int i = 0; name[i] != '\0'; ++i) {
                if (name[i] == '/') {
                    name[i] = '_';
                }
            }

            int fd = open(name, O_RDONLY, 0);
            if(fd < 0) {
                strcpy(response, "Error: Couldn't open rule file");
                break;
            }

            char entry[MSG_MAX_SIZE]; bzero(entry, MSG_MAX_SIZE);
            int nr_entries = 0;

            if (false == read_fmt_entry(fd, entry)) {
                sprintf(response, "Error: reading fmt file");
                goto after_treat;
            }

            while(strlen(entry) > 0) {
                nr_entries += 1;

                char* ending = strchr(entry, '\n');
                if (ending != nullptr) {
                    ending[0] = '\0';
                }

                char* p = strchr(entry, '|');
                if(p == nullptr) {
                    nr_entries -= 1;
                    printf("Error: Rule not well formatted in %s, after %d-th rule\n", name, nr_entries);
                }
                
                p[0] = '\0';
                
                if( 0 == strcmp(entry, my_entry)) {
                    strcpy(response, p + 1);
                    break;
                }
                
                p[0] = '|';

                if (ending != nullptr) {
                    ending[0] = '\n';
                }

                if (false == read_fmt_entry(fd, entry)) {
                    sprintf(response, "Error: reading fmt file");
                    goto after_treat;
                }
            }
            close(fd);

            if(strlen(response) == 0) {
                strcpy(response, "Error: Rule not found");
            }
            break;
        }  
        case CLMSG_AG_RESTART: {
            keep_on_keeping_on = false;
            strcpy(response,"Success");
            break;
        }
        default:
            strcpy(response,"Unsupported");
            printf("Unknown command:%s\n",command);
    }
after_treat:
    
    #ifdef cl_debug
    printf(COLOR_CL_DEB);
    printf("Sending response:%s:\n", response);
    printf(COLOR_OFF); fflush(stdout);
    #endif

    if (send_ack(request_ID, response, strlen(response)) == false) {
        perror("send()");
        return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
    if(argc < 2) {
        printf("Usage: %s ip\n", argv[0]);
        return 0;
    }

    init_agent_info();
Retry:
    init_comms_to_server(argv[1]);
    
    if(nullptr == createIS("/var/log/syslog")) {
        printf("couldn't add syslog\n");
    }
    else {
        printf("Added syslog\n");
    }

    printf("Listening for commands...\n");

    char command[MSG_MAX_SIZE];

    fd_set actfds,readfds;

    FD_ZERO(&actfds);
    FD_SET(control_sd,&actfds);

    timeval time;
    int len;
    keep_on_keeping_on = true;

    while(keep_on_keeping_on) {
        bcopy ((char *) &actfds, (char *) &readfds, sizeof (readfds));
        bzero(command,sizeof(command));
        
        time.tv_sec = 5;
        time.tv_usec = 0;

        int retval;

        if ( 0 > (retval = select(control_sd+1, &readfds, nullptr, nullptr, &time))) {
            perror("select()");
            return 3;
        }
        else if (retval == 0) { //time expired
            char heartbeat = AGMSG_HEARTBEAT;
            if ( false == send_varmsg(control_sd, &heartbeat, sizeof(heartbeat), MSG_NOSIGNAL) ) {
                perror("send heartbeat");
                break;
            }
        }
        else {
            if (false == (len = recv_varmsg(control_sd,command, MSG_NOSIGNAL))) {
                perror("recv()");
                break;
            }

            buffer_change_endian(command, len);

            if (false == treat(command)) {
                perror("Treating request");
                break;
            }
        }
    }
    while(sources.size() > 0) {
        (*sources.begin())->unregister();
    }
    if(keep_on_keeping_on == false) {
        printf("Recieved restart request..\n");
        sleep(5);
        close(control_sd);
        close(transfer_sd);
        printf("Restarting..\n");
        goto Retry;
    }
        
    close(control_sd);
    close(transfer_sd);

    int t = 5;

    printf("Something went wrong. Exit? y/n (will reconnect automatically in %ds)\n", t);
    if (false == get_stdin_approval(t)) {    
        printf("Connection reset, reconnecting..\n");
        goto Retry;
    }
    return 0;
}
