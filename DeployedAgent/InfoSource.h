#include <pthread.h>
#include <errno.h>
#define MSG_MAX_SIZE 205
#define MSG_HEADER_SIZE 0

class InfoSource {
    pthread_t tid;
    static unsigned char globalID;
    InfoSource(const char*, int logfd, int sockfd);
    ~InfoSource();
    friend InfoSource* createIS(const char* path, int sockfd);
public:
    const char* path;
    const unsigned char id;
    
};//Factory methods
//InfoSource daemon for restarting

unsigned char InfoSource::globalID = 1;

struct arg{int logfd, sockfd;};

void* fnc_monitor_infosource(void* p) {
    arg* my_arg = (arg*) p;
    int logfd = my_arg->logfd;
    int sockfd = my_arg->sockfd;
    delete (arg*)p;
    char package[MSG_MAX_SIZE + MSG_HEADER_SIZE]; bzero(package, sizeof(package));
    char* message = package + MSG_HEADER_SIZE;
    printf("Reading log...\n");
    while(1) {
        //!select readfds pentru performanta?
        read(logfd,message,MSG_MAX_SIZE);
        if (strlen(message) > 0 ) {
            //printf("%s\n",message);
            if (send(sockfd,package,strlen(message),0) < 0) {
                perror("Sending message\n");
                break;
            }
        }
        bzero(message, MSG_MAX_SIZE);
    }
    close(logfd);
    return nullptr;
}
//pentru sincronizarea cu serverul, ar trebui ca la initializarea unui InfoSource sa se comunice TCP Serverului indexul noului fisier
//si id ar fi atunci chiar logfd, sa nu se umple tabela cu fd
InfoSource::InfoSource(const char* mypath, int logfd, int sockfd) : path(mypath), id(globalID++) {
    arg* p = new arg(); p->logfd = logfd; p->sockfd = sockfd;
    if( 0 != pthread_create(&tid, nullptr, fnc_monitor_infosource, (void*)p)) {
        perror("Failed to make Info Source Monitor");
        exit(1);
    }
}

InfoSource::~InfoSource() {
    void* retval = 0;
    int err;
    if( 0 != (err = pthread_tryjoin_np(tid,&retval)) ) {
        if(err == EBUSY) {
            if( 0 != (err = pthread_cancel(tid)) ) {
                perror("pthread_cancel()");
            }
        }
        else {
            perror("pthread_join()");
        }
    }
    else {
        //printf("pthread exited with %d", *(int*)retval);
    }
}

InfoSource* createIS(const char* path, int sockfd) {
    int logfd = open(path,O_RDONLY);
    if (logfd < 0) {
        perror("Opening InfoSource path");
        //transmit this to server aswell
        errno = 0;
        return nullptr;
    }
    lseek(logfd,0,SEEK_END);
    return new InfoSource(path, logfd, sockfd);
}