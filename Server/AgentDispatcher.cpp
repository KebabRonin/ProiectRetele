
#include "Agent.h"
#include <vector>

void* fnc_agent_dispatcher(void*) {
    
    struct sockaddr_in agent_sockaddr;  bzero( &agent_sockaddr , sizeof(agent_sockaddr ) );
    std::vector<Agent*> agent_list;

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
    }
    return nullptr;
}
