
#include "Agent.h"
#include <vector>

int init_server_to_port (int port) {
    struct sockaddr_in server_sockaddr; bzero( &server_sockaddr, sizeof(server_sockaddr) );
    int server_sockfd;
    if( -1 == (server_sockfd = socket(AF_INET, SOCK_STREAM, 0)) ) {
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

    if ( -1 == listen(server_sockfd, 5) ) {
        perror("Error listening socket");
        return -1;
    }
    printf("Bound to %d\n",port);
    return server_sockfd;
}

extern std::vector<Agent*> agent_list;

void* fnc_agent_dispatcher(void*) {
    
    struct sockaddr_in agent_sockaddr;  bzero( &agent_sockaddr , sizeof(agent_sockaddr ) );
    

    int server_sockfd = init_server_to_port(AGENT_INBOUND_PORT);
    
    socklen_t len = sizeof(agent_sockaddr);
    int agent_sockfd;
    while(1) {
        agent_sockfd = accept(server_sockfd,(struct sockaddr*) &agent_sockaddr, &len);
        if( agent_sockfd < 0) {
            perror("accepting");
            pthread_exit(nullptr);
        }
        printf("Connection established\n");
        agent_list.push_back(new Agent(&agent_sockaddr, &agent_sockfd));
        for(auto i : agent_list) {
            printf("%s\n",i->id);
        }
    }
    return nullptr;
}
