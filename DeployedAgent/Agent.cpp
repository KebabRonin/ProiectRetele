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

#define AGENT_PORT 2077

char message[205];

struct Rand {
    static unsigned int my_state;
    static int rand() {
        my_state ^= (my_state << 13);
        my_state ^= (my_state >> 17);
        my_state ^= (my_state << 5 );
        return my_state;
    }
};

unsigned int Rand::my_state = 12345;

int main(int argc, char* argv[]) {
    if(argc < 2) {
        printf("Usage: %s ip\n", argv[0]);
        return 0;
    }
    // int nr = atoi(argv[1]);
    // int logfd = open("log.g",O_WRONLY | O_CREAT | O_TRUNC);
    // if (logfd < 0) return 1;
    // int i = 0;
    // while(i < nr) {
    //     switch(Rand::rand() % 3) {
    //         case 0 : strcpy(message,"Connection Refused\n\0"); break;
    //         case 1 : strcpy(message,"Connected 192.168.2.1\n\0"); break;  // generam eventual 2 ip-uri random
    //         default: strcpy(message,"Security stuff\n\0"); break;
    //     }
    //     if (write(logfd,message,strlen(message)) < 0) {
    //         printf("My, my...\n");
    //         break;
    //     }
    //     ++i;
    // }
    // close(logfd);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if( -1 == sockfd ) {
        perror("darng'it");
        return 1;
    }
    struct sockaddr_in server_sockaddr;

    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = inet_addr(argv[1]);
    server_sockaddr.sin_port = htons (AGENT_PORT);

    if ( -1 == connect(sockfd, (struct sockaddr*) &server_sockaddr, sizeof(server_sockaddr))) {
        perror("Error opening socket");
        return 1;
    }
    //fork()
    //dup2(1,sockfd);
    //execlp("tail","tail","-F","/var/log/syslog");
    int logfd = open("/var/log/syslog",O_RDONLY);
    if (logfd < 0) {
        perror(" ");
        return 1;
    }
    lseek(logfd,0,SEEK_END);
    while(1) {
        read(logfd,message,200);
        if (strlen(message) > 0 ) printf("%s\n",message);
        if (write(sockfd,message,strlen(message)) < 0) {
            printf("My, my...\n");
            break;
        }
        memset(message,0,200);
    }
    close(logfd);
    return 0;
}