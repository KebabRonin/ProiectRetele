#include <pthread.h>
#include <errno.h>
#include "../common_definitions.h"

extern pthread_mutex_t my_mutex;
extern int control_sd, transfer_sd;

struct InfoSource {
    pthread_t tid;
    const char* path;
    const unsigned char id;
    static unsigned char globalID;

    int logfd, rulesfd;
    
    void read_entry(int fd, char* message);
    void parse_entry(char* message, char* parsed_message);
    void send_entry(int sockfd, char* message);
    void unregister();

    InfoSource(const char* path);
    ~InfoSource();
};//InfoSource daemon for restarting?
unsigned char InfoSource::globalID = 'a';

#include <vector>
extern std::vector<InfoSource*> sources;

struct arg{int logfd; InfoSource* self;};

void InfoSource::read_entry(int fd, char* message) {
    char* p;
    
    //am citit inainte o parte din urmatorul mesaj
    if ( nullptr != ( p = strchr(message, '\n') ) ) {
        size_t len =  strlen(p+1);
        strcpy(message, p+1);
        bzero(message + len, MSG_MAX_SIZE - len);
        printf("..reduced msg\n");
    }

    int already_read = strlen(message);
    int read_now = 0;

    //fd_set actfd, readfd;

    while( nullptr == ( p = strchr(message, '\n') ) ) {

        read_now = read(fd, message + already_read, MSG_MAX_SIZE - already_read - 1);
        
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
    p[0] = '\0';
    printf("Final:%s\n", message);
    p[0] = '\n';
    //lseek(fd, SEEK_CUR, -1*strlen(p+1));
    //printf("deleted %ld %s\n",-1*strlen(p+1), p+1);
    
    printf("read\n");
}

void InfoSource::parse_entry(char* message, char* parsed_message) {
    int len;
    if ( strchr(message, '\n') == nullptr ) {
        len = strlen(message);
    }
    else {
        len = strchr(message, '\n') - message;
    }

    //write(my_parser_prog_sokpairfd, message, strlen(message));
    //while (..) read(my_parser_prog_sokpairfd, parsed_message);

    bzero(parsed_message, MSG_MAX_SIZE);
    strcpy(parsed_message,this->path);
    for ( int i = 0; parsed_message[i] != '\0'; ++i) {
        if (parsed_message[i] == '/') {
            parsed_message[i] = '_';
        }
    }
    strcat(parsed_message, "\t"); ///! change \t to \0 in Agent.h too!!!
    strncat(parsed_message, message, len);
    printf("parsed\n");
}

void InfoSource::send_entry(int sockfd, char* message) {
    pthread_mutex_lock(&my_mutex);
    if (send_varmsg(sockfd, message, strlen(message)) == false) {
        pthread_mutex_unlock(&my_mutex);
        perror("Sending message\n");
        exit(2);
    }
    pthread_mutex_unlock(&my_mutex);
    printf("sent\n");
}

void InfoSource::unregister() {
    int nr_ord = 0;
    for (auto i : sources) {
        if ( i == this) {
            sources.erase(sources.begin() + nr_ord);
        }
    }
    delete this;
}

void* fnc_monitor_infosource(void* p) {
    InfoSource* self = (InfoSource*) p;
    
    char message[MSG_MAX_SIZE], parsed_message[MSG_MAX_SIZE];

    bzero(message,MSG_MAX_SIZE);

    printf("Reading log...\n");

    while(1) {
        self->read_entry(self->logfd, message);
        self->parse_entry(message, parsed_message);
        self->send_entry(transfer_sd,parsed_message);
    }
    self->unregister();
    return nullptr;
}
//pentru sincronizarea cu serverul, ar trebui ca la initializarea unui InfoSource sa se comunice TCP Serverului indexul noului fisier
//si id ar fi atunci chiar logfd, sa nu se umple tabela cu fd

void agmsg_ack_new_is(const char id, const char* path) {
    char ack[MSG_MAX_SIZE]; bzero(ack,sizeof(ack));
    ack[0] = AGMSG_ACK_NEW_IS;
    ack[1] = id;
    size_t len = strlen(path);
    memcpy(ack + 2, &len, sizeof(len));
    strcpy(ack+5,path);

    if ( 0 >= send(control_sd, ack, strlen(ack), 0)) {
        perror("send");
        exit(2);
    }
}

InfoSource::InfoSource(const char* mypath) : path(mypath), id(globalID++) {
    char name[MSG_MAX_SIZE];
    sprintf(name, "%s.fmt", path);
    for ( int i = 0; name[i] != '\0'; ++i) {
        if (name[i] == '/') {
            name[i] = '_';
        }
    }

    if((rulesfd = open(name, O_RDWR | O_CREAT, 0750)) < 0) {
        agmsg_ack_new_is(0, "Error: Opening fmt file");
        close(rulesfd);
        
        this->unregister();
        return;
    }

    logfd = open(path,O_RDONLY);
    if (logfd < 0) {
        perror("Opening InfoSource path");
        agmsg_ack_new_is(0, "Error: Opening path");

        this->unregister();
        return;
    }
    lseek(logfd,0,SEEK_END);

    if( 0 != pthread_create(&tid, nullptr, fnc_monitor_infosource, (void*)this)) {
        perror("Failed to make Info Source Monitor");
        this->unregister();
        exit(1);
    }
    
    agmsg_ack_new_is(this->id, this->path);
}

InfoSource::~InfoSource() {
    void* retval = nullptr;
    int err;
    if( 0 != (err = pthread_tryjoin_np(tid,&retval)) ) {
        if(err == EBUSY) {
            if( 0 != (err = pthread_cancel(tid)) ) {
                perror("pthread_cancel()");
            }
            if(  0 != (err = pthread_join(tid, &retval)) ) {
                perror("pthread_join()");
            }
        }
        else {
            perror("pthread_tryjoin()");
        }
    }
    else {
        printf("pthread joined\n"/*, *(int*)retval*/);
    }
    close(rulesfd);
    close(logfd);
}