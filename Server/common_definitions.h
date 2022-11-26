#define MSG_MAX_SIZE 205
#define AGENT_INBOUND_PORT 2077
#define AGENT_TCP_PORT 2078
#define CLIENT_PORT 2048

int init_server_to_port (int port) {
    struct sockaddr_in server_sockaddr; bzero( &server_sockaddr, sizeof(server_sockaddr) );
    int server_sockfd;
    if( -1 == (server_sockfd = socket(AF_INET, SOCK_STREAM, 0)) ) {
        perror("socket()");
        return -1;
    }
    

    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_sockaddr.sin_port = htons (AGENT_INBOUND_PORT);

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