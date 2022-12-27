#pragma once
#define MSG_MAX_SIZE 505
#define MSG_HEADER_SIZE 0
#define AGENT_CONTROL_PORT 2078
#define CLIENT_PORT 2048
#define CLMSG_AGLIST 'l'
#define CLMSG_ADDSRC 'a'
#define CLMSG_AGPROP 'p'
#define AGMSG_HEARTBEAT 'h'
#define AGMSG_ACK_NEW_IS 'n'
#define AGMSG_NEW_IS 'a'


bool recv_fixed_length(const int fd, char* message, const unsigned int length) {

    unsigned int read_bytes, total_read_bytes = 0;
    bzero(message, length + 1);

    while(total_read_bytes < length) {
        read_bytes = recv(fd, message + total_read_bytes, length - total_read_bytes, 0 );
        
        if(read_bytes < 0) {
            perror("recv fixed length message");
            return false;
        }

        total_read_bytes += read_bytes;
    }

    return true;
}

bool recv_varmsg(const int fd, char* message) {
    unsigned int length = 0;
    if ( false == recv_fixed_length(fd, (char*)&length, sizeof(length))) {
        printf("recv msg length\n");
        return false;
    }

    if ( false == recv_fixed_length(fd, message, length)) {
        printf("recv msg length\n");
        return false;
    }

    return true;
}

bool send_varmsg(const int fd, const void* message, const unsigned int length) {
    if ( 0 > send(fd, &length, sizeof(length), 0) ) { 
        perror("send length");
        return false;
    }
    if ( 0 > send(fd, message, length, 0) ) { 
        perror("send message");
        return false;
    }

    return true;

}