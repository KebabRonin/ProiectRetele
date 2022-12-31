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

pthread_mutex_t my_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t sources_mutex = PTHREAD_MUTEX_INITIALIZER;

int control_sd, transfer_sd;

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
        printf("Entering while..\n");

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
        printf("%ld s %ld ms time left\n",time.tv_sec, time.tv_usec);
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

void init_agent_info() {///!XML
    int infofd = open("agent.info",O_WRONLY | O_CREAT, 0750);
    int pid;
    switch(pid = fork()) {
        case -1:
            perror("fork");
            exit(3);
        case 0 :
            dup2(infofd, 1);
            execlp("hostnamectl", "Agent info gatherer", nullptr);
            perror("execlp");
            exit(3);
    }

    waitpid(pid, nullptr, 0);
    close(infofd);

}

std::vector<InfoSource*> sources;

bool send_ack(const char agmsg_type, const char* info, const unsigned int info_size) {
    char ack[MSG_MAX_SIZE]; bzero(ack,sizeof(ack));

    if (info_size + 3 > MSG_MAX_SIZE) {
        printf("WARNING: ack info overflows MSG_MAX_SIZE\n");
    }

    ack[0] = AGMSG_ACK;
    ack[1] = agmsg_type;
    memcpy(ack+2,info, info_size);

    if ( false == send_varmsg(control_sd, ack, info_size + 2, MSG_NOSIGNAL)) {
        perror("send");
        return false;
    }
    return true;
}

bool treat (char const* command) {
    char request_id = command[1];
    char response[MSG_MAX_SIZE]; bzero(response, sizeof(response));
    switch(command[0]){
        case AGMSG_NEW_IS:
            {
                InfoSource* to_add = new InfoSource(command + 2);
                if( to_add != nullptr ) {
                    sources.push_back(to_add);
                    ///!send to parent the new id
                    strcpy(response,"Success\0");
                }
                else {
                    strcpy(response,"Error\0");
                }
                break;
            }    
        default:
            strcpy(response,"Unsupported");
            printf("Unknown command:%s\n",command);
    }
    if (send_ack(AGMSG_NEW_IS, response, strlen(response)) == false) {
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
    
    
    sources.push_back(new InfoSource("/var/log/syslog"));

    printf("Added syslog, listening for commands...\n");

    char command[MSG_MAX_SIZE];

    fd_set actfds,readfds;

    FD_ZERO(&actfds);
    FD_SET(control_sd,&actfds);

    timeval time;

    while(1) {
        bcopy ((char *) &actfds, (char *) &readfds, sizeof (readfds));
        bzero(command,sizeof(command));
        
        time.tv_sec = 5;
        time.tv_usec = 0;

        int retval;

        if ( 0 > (retval = select(control_sd+1, &readfds, nullptr, nullptr, &time))) {
            perror("select()");
            return 3;
        }
        else if (retval == 0) {
            char heartbeat = AGMSG_HEARTBEAT;
            if ( false == send_varmsg(control_sd, &heartbeat, sizeof(heartbeat), MSG_NOSIGNAL) ) {
                perror("send heartbeat");
                break;
            }
        }
        else {
            if (false == recv_varmsg(control_sd,command, MSG_NOSIGNAL)) {
                perror("recv()");
                break;
            }
            
            if (false == treat(command)) {
                perror("Treating request");
                break;
            }
        }
    }
    while(sources.size() > 0) {
        (*sources.begin())->unregister();
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
