#pragma once
#define MSG_MAX_SIZE 1024
#define MSG_HEADER_SIZE 0
#define AGENT_CONTROL_PORT 2078
#define AGENT_TRANSFER_PORT 2079
#define CLIENT_PORT 2048
#define CLMSG_AGLIST 'l'
#define CLMSG_ADDSRC 'a'
#define CLMSG_AGPROP 'p'
#define AGMSG_HEARTBEAT 'h'
#define AGMSG_ACK 'a'
#define AGMSG_NEW_IS 'i'


bool recv_fixed_length(const int fd, char* message, const unsigned int length, int flags) {

    unsigned int read_bytes, total_read_bytes = 0;
    bzero(message, length + 1);

    while(total_read_bytes < length) {
        read_bytes = recv(fd, message + total_read_bytes, length - total_read_bytes, flags );
        
        if(read_bytes <= 0) {
            if (read_bytes < 0) {
                perror("recv_fixed_length");
            }
            else {
                printf("recv_fixed_length: FD is nonblocking or socket was closed\n");
            }
            
            return false;
        }

        total_read_bytes += read_bytes;
    }

    return true;
}

bool recv_varmsg(const int fd, char* message, int flags) {
    unsigned short int length = 0;
    if ( false == recv_fixed_length(fd, (char*)&length, sizeof(length), flags)) {
        printf("Error : recv varlen length\n");
        return false;
    }

    length = ntohs(length);

    if ( false == recv_fixed_length(fd, message, length, flags)) {
        printf("Error : recv varlen msg\n");
        return false;
    }

    return true;
}

bool send_varmsg(const int fd, const char* message, const unsigned short int length, int flags) {
    unsigned short int myLen = htons(length);
    if ( 0 > send(fd, &myLen, sizeof(myLen), flags) ) { 
        perror("send length");
        return false;
    }
    if ( 0 > send(fd, message, length, 0) ) { 
        perror("send message");
        return false;
    }

    return true;

}