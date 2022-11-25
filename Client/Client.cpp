#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

char msg[105];

int main() {
    int ServerSock = socket(AF_INET,SOCK_STREAM,0);
    //USE IMGUI/GTK+/QT FOR THIS
    while(1) {
        read(0,msg,100);
        write(ServerSock,msg,strlen(msg));
        read(ServerSock,msg,100);
        printf("%s\n",msg);
    }
}