#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>

char message[205];

int main() {
    int logfd = open("log.g",O_RDONLY);
    if (logfd < 0) return 1;
    while(1) {
        if(read(logfd,message,200) < 0) {
            perror("errror");
            return 2;
        }
        printf("%s\n",message);
    }
    return 0;
}