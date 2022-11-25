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

#define AGENT_PORT 2077


struct sockaddr_in server_sockaddr;
struct sockaddr_in agent_sockaddr;

char message[205];


int main() {
    int server_sockfd;
    if( -1 == (server_sockfd = socket(AF_INET, SOCK_STREAM, 0)) ) {
        perror("darng'it");
        return 1;
    }
    bzero(&server_sockaddr, sizeof(server_sockaddr) );
    bzero(&agent_sockaddr , sizeof(agent_sockaddr ) );

    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_sockaddr.sin_port = htons (AGENT_PORT);

    int on=1;
    setsockopt(server_sockfd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));

    if ( -1 == bind(server_sockfd, (struct sockaddr*) &server_sockaddr, (socklen_t) sizeof(server_sockaddr))) {
        perror("Error opening socket");
        return 1;
    }

    if ( -1 == listen(server_sockfd, 5) ) {
        perror("Error listening socket");
        return 1;
    }
    int logfd = open("agent.log",O_WRONLY | O_APPEND | O_CREAT);
    if (logfd < 0) {
        perror("Opening log");
        return 2;
    }
    socklen_t len = sizeof(agent_sockaddr);
    int agent_sockfd = accept(server_sockfd,(struct sockaddr*) &agent_sockaddr, &len);
    if( agent_sockfd < 0) {
        perror("accepting");
        return 1;
    }
    printf("Connection established\n");
    while(1) {

        if(read(agent_sockfd,message,200) < 0) {
            perror("errror");
            break;
        }
        if(strlen(message) > 0) {
            printf("%s\n",message);
            write(logfd,message,strlen(message));
            memset(message,0,200);
        }
    }
    close(logfd);
    return 0;
}
