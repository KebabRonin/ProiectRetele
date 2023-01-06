#pragma once
#define MSG_MAX_SIZE 1024
#define ENTRIESPERPAGE 10
#define MSG_HEADER_SIZE 0
#define AGENT_CONTROL_PORT 2078
#define AGENT_TRANSFER_PORT 2079
#define CLIENT_PORT 2048
#define CLMSG_AGLIST                'l'
#define CLMSG_ADDSRC                'a'
#define CLMSG_ADDRLE                'r'
#define CLMSG_RMVRLE                'v'
#define CLMSG_AGPROP                'p'
#define CLMSG_AG_HOWMANY_RULEPAGES  'h'
#define CLMSG_AG_LIST_RULEPAGE      'g'
#define CLMSG_AG_LIST_SOURCES       'i'
#define CLMSG_AG_SHOW_RULE          's'
#define AGMSG_HEARTBEAT     'h'
#define AGMSG_ACK           'a'
#define LOG_PATH "logs/"

#define cl_debug 0
#define ag_debug 0
#define COLOR_OFF   "\e[m"
#define COLOR_AG_DEB "\033[1;30m"
#define COLOR_CL_DEB "\033[1;32m"

#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>

void buffer_change_endian(char* buffer, unsigned long length) {
    char temp;
    if (htonl(1) != 1) {
        for (int i = 0; i < length/2; ++i) {
            temp = buffer[i];
            buffer[i] = buffer[length - 1 - i];
            buffer[length - 1 - i] = temp;
        }
    }
}

int recv_fixed_length(const int fd, char* message, const unsigned int length, int flags) {

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
            
            return 0;
        }

        total_read_bytes += read_bytes;
    }

    if (ntohl(1) != 1) {
        buffer_change_endian(message, length);
    }

    return 1;
}

int recv_varmsg(const int fd, char* message, int flags) {
    unsigned short int length = 0;
    if ( 0 == recv_fixed_length(fd, (char*)&length, sizeof(length), flags)) {
        //printf("Error : recv varlen length\n");
        return 0;
    }

    if ( 0 == recv_fixed_length(fd, message, length, flags)) {
        //printf("Error : recv varlen msg\n");
        return 0;
    }

    buffer_change_endian(message, length);

    return length;
}

int send_varmsg(const int fd, const char* message, const unsigned short int length, int flags) {
    unsigned short int myLen = htons(length);
    if ( 0 > send(fd, &myLen, sizeof(myLen), flags) ) { 
        perror("send length");
        return 0;
    }
    if ( 0 > send(fd, message, length, 0) ) { 
        perror("send message");
        return 0;
    }

    return 1;

}
