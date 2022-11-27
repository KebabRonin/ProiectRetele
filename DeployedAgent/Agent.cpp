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
#include <vector>
#include "InfoSource.h"

#define AGENT_PORT 2077
#define AGENT_CONN_INFO_SIZE 20

int main(int argc, char* argv[]) {
    if(argc < 2) {
        printf("Usage: %s ip\n", argv[0]);
        return 0;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if( -1 == sockfd ) {
        perror("socket()");
        return 1;
    }

    struct sockaddr_in server_sockaddr;

    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = inet_addr(argv[1]);
    server_sockaddr.sin_port = htons (AGENT_PORT);

    while ( -1 == connect(sockfd, (struct sockaddr*) &server_sockaddr, sizeof(server_sockaddr))) {
        perror("Couldn't connect to server");
        sleep(5);
    }
    printf("Connected to server!\n");
    char conn_info[AGENT_CONN_INFO_SIZE] = "v0.1";
    send(sockfd, conn_info, strlen(conn_info), 0);

    std::vector<InfoSource*> sources;
    sources.push_back(createIS("/var/log/syslog", sockfd));

    printf("Added syslog, listening for commands...\n");
    pause();
    char command[MSG_MAX_SIZE];
    char response[MSG_MAX_SIZE];

    fd_set actfds,readfds;

    FD_ZERO(&actfds);
    FD_SET(sockfd,&actfds);

    while(1) {
        bcopy ((char *) &actfds, (char *) &readfds, sizeof (readfds));
        bzero(command,sizeof(command));
        ///!! ar trebui sa fie un sockfd nou, pus cu TCP pe AGENT_TCP_PORT
        if (select(sockfd+1, &readfds, nullptr, nullptr, nullptr) < 0) {
            perror("select()");
            return 3;
        }
        if (recv(sockfd,command,sizeof(command),0) < 0) {
            perror("recv()");
            return 3;
        }
        
        if(strstr(command, "add ") == command) {
            InfoSource* to_add = createIS(command + 4, sockfd);
            if( to_add != nullptr ) {
                sources.push_back(to_add);
                ///!send to parent the new id
                strcpy(response,"Success\0");
            }
            else {
                strcpy(response,"Error\0");
            }
            if (send(sockfd, response, strlen(response), 0) < 0) {
                perror("send()");
                return 3;
            }
            
        }
        else {
            printf("Unknown command:%s\n",command);
        }
    }
    return 0;
}
