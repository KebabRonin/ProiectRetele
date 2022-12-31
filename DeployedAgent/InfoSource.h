#include <pthread.h>
#include <errno.h>
#include "../common_definitions.h"

extern pthread_mutex_t my_mutex;
extern pthread_mutex_t sources_mutex;
extern int control_sd, transfer_sd;
extern bool send_ack(const char, const char*, const unsigned int);

struct InfoSource {
    pthread_t tid;
    const char* path;
    const unsigned char id;
    static unsigned char globalID;

    int logfd;
    
    void read_entry(int fd, char* message);
    bool parse_entry(const char* message, char* parsed_message);
    void send_entry(int sockfd, char* message);
    void unregister();

    InfoSource(const char* path);
    ~InfoSource();
};//InfoSource daemon for restarting?
unsigned char InfoSource::globalID = 'a';

#include <vector>
extern std::vector<InfoSource*> sources;

struct arg{int logfd; InfoSource* self;};

void read_fmt_entry(int fd, char* message) {
    char* p;
    
    //am citit inainte o parte din urmatorul mesaj
    if ( nullptr != ( p = strchr(message, '\n') ) ) {
        size_t len =  strlen(p+1);
        strcpy(message, p+1);
        bzero(message + len, MSG_MAX_SIZE - len);
    }
    else {
        bzero(message, strlen(message));
    }

    int already_read = strlen(message);
    int read_now = 0;

    while( nullptr == ( p = strchr(message, '\n') ) ) {

        read_now = read(fd, message + already_read, MSG_MAX_SIZE - already_read - 1);
        
        if (read_now < 0) {
            perror("Reading");
            exit(1);
        }
        else if (read_now == 0) {
            break;
        }
        
        already_read += read_now;

        if(already_read >= MSG_MAX_SIZE) {
            printf("Message too long!\n%ld,%s\n",strlen(message), message);
            exit(1);
        }
        message[already_read] = '\0';
    }
}

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
    }
}

bool InfoSource::parse_entry(const char* message, char* parsed_message) {
    int len;
    if ( strchr(message, '\n') == nullptr ) {
        len = strlen(message);
    }
    else {
        len = strchr(message, '\n') - message;
    }


    bzero(parsed_message, 2*MSG_MAX_SIZE);
    strcpy(parsed_message,this->path);
    for ( int i = 0; parsed_message[i] != '\0'; ++i) {
        if (parsed_message[i] == '/') {
            parsed_message[i] = '_';
        }
    }
    strcat(parsed_message, "\t"); ///! change \t to \0 in Agent.h too!!!

    char name[MSG_MAX_SIZE];
    sprintf(name, "%s.fmt", path);
    for ( int i = 0; name[i] != '\0'; ++i) {
        if (name[i] == '/') {
            name[i] = '_';
        }
    }

    int fmtfd = open(name, O_RDONLY, 0);
    if (fmtfd < 0) {
        perror("open");
        exit(3);
    }


    int rule_nr = 0;
    char rule[MSG_MAX_SIZE]; bzero(rule, MSG_MAX_SIZE);
    char jsoned_msg[2*MSG_MAX_SIZE];
    read_entry(fmtfd, rule);

    bool could_parse = false;

    //check rules one by one

    while(!could_parse && strlen(rule) > 0) {
        ++rule_nr;
        bzero(jsoned_msg, 2*MSG_MAX_SIZE);
        int parser_at = 0, jsoned_at = 0;
        bool escaped_ch = false;

        int bound = strchr(rule, '\n') - rule;

        if (bound < 0) {
            bound = strlen(rule);
        }

        //get rule name

        len = 0;
        while((rule[len] >= 'A' && rule[len] <= 'Z') || (rule[len] >= '0' && rule[len] <= '9') || rule[len] == '_') {
            len += 1;
        }

        if (rule[len] != '|') {
            goto NewEntry;
        }

        strcpy(jsoned_msg + jsoned_at, "{\"rule\":\"");
        jsoned_at += strlen("{\"rule\":\"");
        strncpy(jsoned_msg + jsoned_at, rule + 0, len);
        jsoned_at += len;
        strcpy(jsoned_msg + jsoned_at, "\",\n\"args\":[\n");
        jsoned_at += strlen("\",\n\"args\":[\n");
        
        //the actual parser

        for (int i = len + 1; i <= bound && jsoned_at <= MSG_MAX_SIZE; ++i) {
            if (escaped_ch) {
                if (rule[i] == '\0' || rule[i] == '\n') {
                    goto NewEntry;
                }
                switch (rule[i]) {
                    case 'n'://number parameter
                    {i+=1;

                        if (strncmp(rule + i, ":\"_\"", strlen(":\"_\"")) == 0) {
                            //ignore parameters with name "_"
                            len = 0;
                            while(message[parser_at + len] >= '0' && message[parser_at + len] <= '9') {
                                len += 1;
                            }

                            if(len == 0) {
                                goto NewEntry;
                            }

                            parser_at += len;
                            i += strlen(":\"_\"") - 1;
                            break;
                        }

                        strcpy (jsoned_msg + jsoned_at, "\t{\"type\":\"number\",\"name\":\"");
                        jsoned_at += strlen("\t{\"type\":\"number\",\"name\":\"");

                        //name
                        
                        if (rule[i] != ':' || rule[i + 1] != '\"') {
                            goto NewEntry;
                        }
                        i += 2;
                        len = 0;
                        while((rule[i + len] >= 'A' && rule[i + len] <= 'Z') || (rule[i + len] >= '0' && rule[i + len] <= '9') || rule[i + len] == '_') {
                            len += 1;
                        }

                        if (rule[i + len] != '\"') {
                            goto NewEntry;
                        }
                        
                        strncpy(jsoned_msg + jsoned_at, rule + i, len);
                        i += len;
                        jsoned_at += len;

                        //value
                        
                        strcpy(jsoned_msg + jsoned_at, "\",\"value\":\""); 
                        jsoned_at += strlen("\",\"value\":\"");


                        len = 0;
                        while(message[parser_at + len] >= '0' && message[parser_at + len] <= '9') {
                            len += 1;
                        }

                        if(len == 0) {
                            goto NewEntry;
                        }

                        strncpy(jsoned_msg + jsoned_at, message + parser_at, len);
                        parser_at += len;
                        jsoned_at += len;

                        

                        strcpy (jsoned_msg + jsoned_at, "\"},\n");
                        jsoned_at += strlen("\"},\n");

                    }
                        break;
                    case 's'://string parameter
                    {i+=1;

                        if (strncmp(rule + i, ":\"_\"", strlen(":\"_\"")) == 0) {
                            //ignore parameters with name "_"
                            len = 0;
                            char delimiter = 0;
                            
                            if (rule[i] == '\\') {
                                if (rule[i + 1] == 'n' || rule[i + 1] == 's' || rule[i + 1] == 'd') {
                                    goto NewEntry;
                                } 
                                else {
                                    delimiter = rule[i + 1];
                                }
                            }
                            else {
                                delimiter = rule[i];
                            }
                
                            
                            
                            while(message[parser_at + len] != delimiter && message[parser_at + len] != '\"' && message[parser_at + len] >= 32 && message[parser_at + len] <= 126) { 
                                //visible chars, plus space, minus "
                                len += 1;
                            }

                            if(len == 0) {
                                goto NewEntry;
                            }

                            parser_at += len;
                            i += strlen(":\"_\"") - 1;
                            break;
                        }

                        strcpy (jsoned_msg + jsoned_at, "\t{\"type\":\"string\",\"name\":\"");
                        jsoned_at += strlen("\t{\"type\":\"string\",\"name\":\"");

                        //name
                        
                        if (rule[i] != ':' || rule[i + 1] != '\"') {
                            goto NewEntry;
                        }
                        i += 2;
                        len = 0;
                        while((rule[i + len] >= 'A' && rule[i + len] <= 'Z') || (rule[i + len] >= '0' && rule[i + len] <= '9') || rule[i + len] == '_') {
                            len += 1;
                        }

                        if (rule[i + len] != '\"') {
                            goto NewEntry;
                        }
                        
                        strncpy(jsoned_msg + jsoned_at, rule + i, len);
                        i += len + 1;
                        jsoned_at += len;

                        //value
                        
                        strcpy(jsoned_msg + jsoned_at, "\",\"value\":\""); 
                        jsoned_at += strlen("\",\"value\":\"");


                        len = 0;
                        char delimiter = 0;
                            
                        if (rule[i] == '\\') {
                            if (rule[i + 1] == 'n' || rule[i + 1] == 's' || rule[i + 1] == 'd') {
                                goto NewEntry;
                            } 
                            else {
                                delimiter = rule[i + 1];
                            }
                        }
                        else {
                            delimiter = rule[i];
                        }

                        while(message[parser_at + len] != delimiter && message[parser_at + len] != '\"' && message[parser_at + len] >= 32 && message[parser_at + len] <= 126) { 
                            //visible chars, plus space, minus "
                            len += 1;
                        }

                        if(len == 0) {
                            goto NewEntry;
                        }

                        strncpy(jsoned_msg + jsoned_at, message + parser_at, len);
                        parser_at += len;
                        jsoned_at += len;

                        

                        strcpy (jsoned_msg + jsoned_at, "\"},\n");
                        jsoned_at += strlen("\"},\n");
                        i -= 1;
                    }
                        break;
                    case 'd'://date parameter
                    {i+=1;

                        if (strncmp(rule + i, ":\"_\"", strlen(":\"_\"")) == 0) {
                            //ignore parameters with name "_"
                            int month = 0;
                                if(strncmp(message + parser_at, "Jan", 3) == 0) month = 1;
                            else if(strncmp(message + parser_at, "Feb", 3) == 0) month = 2;
                            else if(strncmp(message + parser_at, "Mar", 3) == 0) month = 3;
                            else if(strncmp(message + parser_at, "Apr", 3) == 0) month = 4;
                            else if(strncmp(message + parser_at, "May", 3) == 0) month = 5;
                            else if(strncmp(message + parser_at, "Jun", 3) == 0) month = 6;
                            else if(strncmp(message + parser_at, "Jul", 3) == 0) month = 7;
                            else if(strncmp(message + parser_at, "Aug", 3) == 0) month = 8;
                            else if(strncmp(message + parser_at, "Sep", 3) == 0) month = 9;
                            else if(strncmp(message + parser_at, "Oct", 3) == 0) month = 10;
                            else if(strncmp(message + parser_at, "Nov", 3) == 0) month = 11;
                            else if(strncmp(message + parser_at, "Dec", 3) == 0) month = 12;
                            else {goto NewEntry;}

                            
                            if (message[parser_at+3] != ' ') {
                                goto NewEntry;
                            }
                            parser_at += 4;

                            int day = atoi(message + parser_at);
                            if (day < 0 || day > 31) {
                                goto NewEntry;
                            }
                            parser_at += 1 + (day/10 > 0);
                            if (message[parser_at] != ' ') {
                                goto NewEntry;
                            }
                            parser_at += 1;

                            if(!(message[parser_at] >= '0' && message[parser_at] <= '9' && message[parser_at+1] >= '0' && message[parser_at+1] <= '9' && message[parser_at+2] == ':')) {
                                goto NewEntry;
                            }
                            int hour = (message[parser_at] - '0') * 10 + (message[parser_at + 1] - '0');
                            parser_at += 3;

                            if(!(message[parser_at] >= '0' && message[parser_at] <= '9' && message[parser_at+1] >= '0' && message[parser_at+1] <= '9' && message[parser_at+2] == ':')) {
                                goto NewEntry;
                            }
                            int minutes = (message[parser_at] - '0') * 10 + (message[parser_at + 1] - '0');
                            parser_at += 3;

                            if(!(message[parser_at] >= '0' && message[parser_at] <= '9' && message[parser_at+1] >= '0' && message[parser_at+1] <= '9')) {
                                goto NewEntry;
                            }
                            int seconds = (message[parser_at] - '0') * 10 + (message[parser_at + 1] - '0');
                            parser_at += 2;
                            i += strlen(":\"_\"") - 1;
                            break;
                        }

                        strcpy (jsoned_msg + jsoned_at, "\t{\"type\":\"date\",\"name\":\"");
                        jsoned_at += strlen("\t{\"type\":\"date\",\"name\":\"");

                        //name
                        
                        if (rule[i] != ':' || rule[i + 1] != '\"') {
                            goto NewEntry;
                        }
                        i += 2;
                        len = 0;
                        while((rule[i + len] >= 'A' && rule[i + len] <= 'Z') || (rule[i + len] >= '0' && rule[i + len] <= '9') || rule[i + len] == '_') {
                            len += 1;
                        }

                        if (rule[i + len] != '\"') {
                            goto NewEntry;
                        }
                        
                        strncpy(jsoned_msg + jsoned_at, rule + i, len);
                        i += len;
                        jsoned_at += len;

                        //value
                        
                        strcpy(jsoned_msg + jsoned_at, "\",\"value\":\""); 
                        jsoned_at += strlen("\",\"value\":\"");


                        int month = 0;
                             if(strncmp(message + parser_at, "Jan", 3) == 0) month = 1;
                        else if(strncmp(message + parser_at, "Feb", 3) == 0) month = 2;
                        else if(strncmp(message + parser_at, "Mar", 3) == 0) month = 3;
                        else if(strncmp(message + parser_at, "Apr", 3) == 0) month = 4;
                        else if(strncmp(message + parser_at, "May", 3) == 0) month = 5;
                        else if(strncmp(message + parser_at, "Jun", 3) == 0) month = 6;
                        else if(strncmp(message + parser_at, "Jul", 3) == 0) month = 7;
                        else if(strncmp(message + parser_at, "Aug", 3) == 0) month = 8;
                        else if(strncmp(message + parser_at, "Sep", 3) == 0) month = 9;
                        else if(strncmp(message + parser_at, "Oct", 3) == 0) month = 10;
                        else if(strncmp(message + parser_at, "Nov", 3) == 0) month = 11;
                        else if(strncmp(message + parser_at, "Dec", 3) == 0) month = 12;
                        else {goto NewEntry;}


                        if (message[parser_at+3] != ' ') {
                            goto NewEntry;
                        }
                        parser_at += 4;

                        int day = atoi(message + parser_at);
                        if (day < 0 || day > 31) {
                            goto NewEntry;
                        }
                        parser_at += 1 + (day/10 > 0);
                        if (message[parser_at] != ' ') {
                            goto NewEntry;
                        }
                        parser_at += 1;

                        if(!(message[parser_at] >= '0' && message[parser_at] <= '9' && message[parser_at+1] >= '0' && message[parser_at+1] <= '9' && message[parser_at+2] == ':')) {
                            goto NewEntry;
                        }
                        int hour = (message[parser_at] - '0') * 10 + (message[parser_at + 1] - '0');
                        parser_at += 3;

                        if(!(message[parser_at] >= '0' && message[parser_at] <= '9' && message[parser_at+1] >= '0' && message[parser_at+1] <= '9' && message[parser_at+2] == ':')) {
                            goto NewEntry;
                        }
                        int minutes = (message[parser_at] - '0') * 10 + (message[parser_at + 1] - '0');
                        parser_at += 3;

                        if(!(message[parser_at] >= '0' && message[parser_at] <= '9' && message[parser_at+1] >= '0' && message[parser_at+1] <= '9')) {
                            goto NewEntry;
                        }
                        int seconds = (message[parser_at] - '0') * 10 + (message[parser_at + 1] - '0');
                        parser_at += 2;

                        sprintf(jsoned_msg + jsoned_at, "%02d %02d %02d:%02d:%02d", month, day, hour, minutes, seconds);
                        jsoned_at += strlen("00 00 00:00:00");
    

                        strcpy (jsoned_msg + jsoned_at, "\"},\n");
                        jsoned_at += strlen("\"},\n");
                        
                    }
                        break;
                    case ' ':
                        if(message[parser_at++] == ' ') {
                            while(message[parser_at] == ' ' || message[parser_at] == '\t') parser_at+=1;
                            while(rule[i + 1] == ' ') i += 1;
                        }
                        else {
                            goto NewEntry;
                        }
                        break;
                    default:
                        if(message[parser_at] != rule[i]) {
                            goto NewEntry;
                        }
                        parser_at+=1;
                }
                escaped_ch = false;
            }
            else {
                if ((rule[i] == '\0' || rule[i] == '\n') && (message[parser_at] == '\n' || message[parser_at] == '\0')) {

                    if(jsoned_msg[jsoned_at - 2] == ',') {
                        jsoned_at -= 2;
                        jsoned_msg[jsoned_at] = '\0';
                    }
                    strcpy(jsoned_msg + jsoned_at, "\n]}");
                    jsoned_at += strlen("\n]}");
                    could_parse = true;
                    break;
                }
                else if ((rule[i] == '\0' || rule[i] == '\n') || (message[parser_at] == '\n' || message[parser_at] == '\0')) {
                    goto NewEntry;
                }
                if(rule[i] == '\\') {
                    escaped_ch = true;
                }
                else if (message[parser_at] != rule[i]) {
                    goto NewEntry;
                }
                else {
                    parser_at+=1;
                }
            }
        }
        if (jsoned_at > MSG_MAX_SIZE) {
            printf("Sending Json would overflow MSG_MAX_SIZE, so I won't send it.\n");
            could_parse = false;
        }
NewEntry:
        read_fmt_entry(fmtfd, rule);
    }
    
    strcat(parsed_message, jsoned_msg);
    printf("parsed\n");
    
    return could_parse;
}

void InfoSource::send_entry(int sockfd, char* message) {
    pthread_mutex_lock(&my_mutex);
    if (send_varmsg(sockfd, message, strlen(message), MSG_NOSIGNAL) == false) {
        pthread_mutex_unlock(&my_mutex);
        perror("Sending message");
        this->unregister();
        pthread_exit(nullptr);
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
    
    char message[MSG_MAX_SIZE], parsed_message[2*MSG_MAX_SIZE];

    bzero(message,MSG_MAX_SIZE);

    printf("Reading log...\n");

    while(1) {
        self->read_entry(self->logfd, message);
        printf("Read : %s..\n", message);
        if (true == self->parse_entry(message, parsed_message) ) {
            printf("AMERSOMFGMORTOS\n");
            self->send_entry(transfer_sd,parsed_message);
        }
        else {
            printf("N-a mers : %s\n",parsed_message);
        }
    }
    self->unregister();
    return nullptr;
}


InfoSource::InfoSource(const char* mypath) : path(mypath), id(globalID++) {
    pthread_mutex_lock(&sources_mutex);
    sources.push_back(this);
    pthread_mutex_unlock(&sources_mutex);
    logfd = open(path,O_RDONLY);
    if (logfd < 0) {
        perror("Opening InfoSource path");
        send_ack(AGMSG_NEW_IS, "Error: Opening path", strlen("Error: Opening path"));

        this->unregister();
        return;
    }
    lseek(logfd,0,SEEK_END);


    char name[MSG_MAX_SIZE];
    sprintf(name, "%s.fmt", path);
    for ( int i = 0; name[i] != '\0'; ++i) {
        if (name[i] == '/') {
            name[i] = '_';
        }
    }

    int rulesfd = 0;
    if((rulesfd = open(name, O_RDWR | O_CREAT, 0750)) < 0) {
        send_ack(AGMSG_NEW_IS, "Error: Opening fmt file", strlen("Error: Opening fmt file"));
        close(rulesfd);
        
        this->unregister();
        return;
    }
    close(rulesfd);


    if( 0 != pthread_create(&tid, nullptr, fnc_monitor_infosource, (void*)this)) {
        perror("Failed to make Info Source Monitor");
        this->unregister();
        exit(1);
    }
    
    send_ack(AGMSG_NEW_IS, this->path, strlen(this->path));
}

InfoSource::~InfoSource() {
    void* retval = nullptr;
    int err;
    if( pthread_self() != this->tid && 0 != (err = pthread_tryjoin_np(tid,&retval)) ) {
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
    close(logfd);
}