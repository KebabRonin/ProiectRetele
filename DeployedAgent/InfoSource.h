#include <pthread.h>
#include <errno.h>
#include "../common_definitions.h"

extern pthread_mutex_t my_mutex;
extern int control_sd, transfer_sd;

class InfoSource {
    pthread_t tid;
    static unsigned char globalID;
    InfoSource(const char*, int logfd);
    ~InfoSource();
    friend InfoSource* createIS(const char* path);
    void read_entry(int fd, char* message);
    void parse_entry(char* message, char* parsed_message);
    void send_entry(int sockfd, char* message);
public:
    const char* path;
    const unsigned char id;
    
    friend void* fnc_monitor_infosource(void* p);
};//InfoSource daemon for restarting
unsigned char InfoSource::globalID = 'a';

struct arg{int logfd; InfoSource* self;};

void InfoSource::read_entry(int fd, char* message) {
    char* p;
    
    //am citit inainte o parte din urmatorul mesaj
    if ( nullptr != ( p = strchr(message, '\n') ) ) {
        strcpy(message, p+1);
    }

    int already_read = 0;
    int read_now = 0;

    //fd_set actfd, readfd;

    while( nullptr == ( p = strchr(message, '\n') ) ) {

        read_now = read(fd, message + already_read, MSG_MAX_SIZE - already_read);
        
        if (read_now < 0) {
            perror("Reading");
            exit(1);
        }
        else if (read_now == 0) {
            sleep(1);
        }
        
        already_read += read_now;

        if(already_read >= MSG_MAX_SIZE) {
            printf("Message too long!\n%ld,%s\n",strlen(message), message);
            exit(1);
        }
        message[already_read] = '\0';
        printf("%d %s..\n",already_read, message);
    }
    //p[0] = '\0';
    printf("Final:%s %s\n", p, message);
    
    //lseek(fd, SEEK_CUR, -1*strlen(p+1));
    //printf("deleted %ld %s\n",-1*strlen(p+1), p+1);
    
    printf("read\n");
}

void InfoSource::parse_entry(char* message, char* parsed_message) {
    char* p = strchr(message, '\n');
    if (p == nullptr) {
        p = message + strlen(message);
    }
    //write(my_parser_prog_sokpairfd, message, strlen(message));
    //while (..) read(my_parser_prog_sokpairfd, parsed_message);
    parsed_message[0] = this->id;
    strncpy(parsed_message+1, message, p - message);
    printf("parsed\n");
}

void InfoSource::send_entry(int sockfd, char* message) {
    int len;
    if ( strchr(message, '\n') == nullptr ) {
        len = strlen(message);
    }
    else {
        len = strchr(message, '\n') - message;
    } 
    pthread_mutex_lock(&my_mutex);
    if (send(sockfd, message, len, 0) < 0) {
        pthread_mutex_unlock(&my_mutex);
        perror("Sending message\n");
        exit(2);
    }
    pthread_mutex_unlock(&my_mutex);
    printf("sent\n");
}

void* fnc_monitor_infosource(void* p) {
    arg* my_arg = (arg*) p;
    int logfd = my_arg->logfd;
    InfoSource* self = my_arg->self;
    delete (arg*)p;
    
    char message[2*MSG_MAX_SIZE], parsed_message[MSG_MAX_SIZE];

    bzero(message,2*MSG_MAX_SIZE);

    printf("Reading log...\n");

    while(1) {
        self->read_entry(logfd, message);
        bzero(parsed_message, MSG_MAX_SIZE);
        self->parse_entry(message, parsed_message);
        self->send_entry(transfer_sd,message); ///!parsed_message!!
    }
    close(logfd);
    return nullptr;
}
//pentru sincronizarea cu serverul, ar trebui ca la initializarea unui InfoSource sa se comunice TCP Serverului indexul noului fisier
//si id ar fi atunci chiar logfd, sa nu se umple tabela cu fd
InfoSource::InfoSource(const char* mypath, int logfd) : path(mypath), id(globalID++) {
    arg* p = new arg(); 
    p->logfd = logfd; 
    p->self = this;
    fnc_monitor_infosource(p);
    /*if( 0 != pthread_create(&tid, nullptr, fnc_monitor_infosource, (void*)p)) {
        perror("Failed to make Info Source Monitor");
        exit(1);
    }*/
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

InfoSource* createIS(const char* path) {
    int logfd = open(path,O_RDONLY);
    if (logfd < 0) {
        perror("Opening InfoSource path");
        //transmit this to server aswell
        errno = 0;
        return nullptr;
    }
    lseek(logfd,0,SEEK_END);
    return new InfoSource(path, logfd);
}
