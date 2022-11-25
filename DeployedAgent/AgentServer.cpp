#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>

char message[205];

int main() {
    int logfd = open("log.g",O_RDONLY);
    printf("opened file\n");
    if (logfd < 0) {
        perror("r");
        return 1;
    }
    while(1) {
        if(read(logfd,message,200) <= 0) {
            if (strlen(message) == 0) {
                printf("Done reading\n");
            }
            else {
                perror("errror");
            }
            break;
        }
        
        printf("%s",message);
        memset(message,0,200);
    }
    close(logfd);
    return 0;
}
