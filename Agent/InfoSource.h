#include <pthread.h>
#include <errno.h>
#include "../common_definitions.h"
#define COLOR_LOG "\033[2;31m"
extern pthread_mutex_t transfer_sock_mutex;
extern pthread_mutex_t sources_mutex;
extern int control_sd, transfer_sd;

struct InfoSource {
    pthread_t tid;
    char* path;
    int logfd;

    pthread_mutex_t rules_file_mutex = PTHREAD_MUTEX_INITIALIZER;
    
    bool read_entry(int fd, char* message);
    int parse_entry(const char* message, char* parsed_message);
    bool send_entry(int sockfd, char* message);
    void unregister();

    bool has_rule(const char* rule_name);
    bool add_rule(char* rule);

    friend InfoSource* createIS(const char* mypath);

    ~InfoSource();
private: 
    InfoSource(const char* mypath, int mylogfd) : logfd(mylogfd) {
        path = new char[strlen(mypath)+1];
        bzero(path, strlen(mypath) + 1);
        strncpy(path,mypath, strlen(mypath));
    }
};

#include <vector>
extern std::vector<InfoSource*> sources;

struct arg{int logfd; InfoSource* self;};

bool read_fmt_entry(int fd, char* message) {
    char* p;
    
    //am citit inainte o parte din urmatorul mesaj
    if ( nullptr != ( p = strchr(message, '\n') ) ) {
        size_t len =  strlen(p+1);
        sprintf(message, "%s", p+1);
        bzero(message + len, MSG_MAX_SIZE - len);
    }
    else {
        bzero(message, strlen(message));
    }

    int already_read = strlen(message);
    int read_now = 0;

    while( nullptr == ( p = strchr(message, '\n') ) ) {

        read_now = read(fd, message + already_read, MSG_MAX_SIZE - already_read);
        
        if (read_now < 0) {
            perror("Reading");
            return false;
        }
        else if (read_now == 0) {
            break;
        }
        
        already_read += read_now;

        if(already_read > MSG_MAX_SIZE) {
            printf("Message too long!\n%ld,%s\n",strlen(message), message);
            return false;
        }
        message[already_read] = '\0';
    }

    return true;
}

bool InfoSource::read_entry(int fd, char* message) {
    char* p;
    
    //am citit inainte o parte din urmatorul mesaj
    if ( nullptr != ( p = strchr(message, '\n') ) ) {
        size_t len =  strlen(p+1);
        bcopy(message, p+1, len);
        bzero(message + len, MSG_MAX_SIZE - len);
    }

    int already_read = strlen(message);
    int read_now = 0;

    while( nullptr == ( p = strchr(message, '\n') ) ) {

        read_now = read(fd, message + already_read, MSG_MAX_SIZE - already_read);
        
        if (read_now < 0) {
            perror("Reading");
            return false;
        }
        else if (read_now == 0) {
            sleep(1);
        }
        
        already_read += read_now;

        if(already_read > MSG_MAX_SIZE) {
            printf("Message too long!\n%ld,%s\n",strlen(message), message);
            return false;
        }
        message[already_read] = '\0';
    }
    return true;
}

int InfoSource::parse_entry(const char* message, char* parsed_message) {
    int len;
    if ( strchr(message, '\n') == nullptr ) {
        len = strlen(message);
    }
    else {
        len = strchr(message, '\n') - message;
    }


    bzero(parsed_message, MSG_MAX_SIZE+1);
    strcpy(parsed_message,this->path);
    for ( int i = 0; parsed_message[i] != '\0'; ++i) {
        if (parsed_message[i] == '/') {
            parsed_message[i] = '_';
        }
    }
    strcat(parsed_message, "\t"); ///! change \t to \0 in Agent.h too!!!

    char name[MSG_MAX_SIZE+1];
    sprintf(name, "%s.fmt", path);
    for ( int i = 0; name[i] != '\0'; ++i) {
        if (name[i] == '/') {
            name[i] = '_';
        }
    }

    pthread_mutex_lock(&rules_file_mutex);

    int fmtfd = open(name, O_RDONLY, 0);
    if (fmtfd < 0) {
        pthread_mutex_unlock(&rules_file_mutex);
        perror("open");
        return -1;
    }


    int rule_nr = 0;
    char rule[MSG_MAX_SIZE+1]; bzero(rule, MSG_MAX_SIZE+1);
    char jsoned_msg[MSG_MAX_SIZE+1];
    if (false == read_fmt_entry(fmtfd, rule)) {
        close(fmtfd);
        return -1;
    }

    bool could_parse = false;

    //check rules one by one

    while(!could_parse && strlen(rule) > 0) {
        ++rule_nr;
        bzero(jsoned_msg, MSG_MAX_SIZE+1);
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

        for (int i = len + 1; i <= bound && jsoned_at < MSG_MAX_SIZE; ++i) {
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
                            parser_at += 2;
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
                        parser_at += 2;
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
                        while(message[parser_at] == ' ' || message[parser_at] == '\t') parser_at+=1;
                        while(rule[i + 1] == ' ') i += 1;
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

        if (jsoned_at >= MSG_MAX_SIZE) {
            printf("Sending Json would overflow MSG_MAX_SIZE, so I won't send it.\n");
            could_parse = false;
        }
NewEntry:
        if (false == read_fmt_entry(fmtfd, rule)) {
            close(fmtfd);
            return -1;
        };
    }
    close(fmtfd);
    pthread_mutex_unlock(&rules_file_mutex);

    strcat(parsed_message, jsoned_msg);
    
    return could_parse;
}

bool InfoSource::send_entry(int sockfd, char* message) {
    pthread_mutex_lock(&transfer_sock_mutex);
    if (send_varmsg(sockfd, message, strlen(message), MSG_NOSIGNAL) == false) {
        pthread_mutex_unlock(&transfer_sock_mutex);
        perror("Sending message");
        return false;
    }
    pthread_mutex_unlock(&transfer_sock_mutex);
    return true;
}

void InfoSource::unregister() {
    int nr_ord = 0;
    pthread_mutex_lock(&sources_mutex);
    st:
    for (auto i : sources) {
        if ( i == this) {
            sources.erase(sources.begin() + nr_ord);
            goto st;
        }
    }
    pthread_mutex_unlock(&sources_mutex);
    delete this;
}

bool InfoSource::has_rule(const char* rule_name) {

    char name[MSG_MAX_SIZE+1];
    sprintf(name, "%s.fmt", path);
    for ( int i = 0; name[i] != '\0'; ++i) {
        if (name[i] == '/') {
            name[i] = '_';
        }
    }

    int fd = open(name, O_RDONLY, 0);
    if (fd < 0) {
        perror("open");
        return false;
    }

    char rule[MSG_MAX_SIZE+1]; bzero(rule, sizeof(rule));

    if (false == read_fmt_entry(fd, rule)) {
        close(fd);
        return false;
    }

    while(strlen(rule) > 0) {
        if(strstr(rule, rule_name) == rule && rule[strlen(rule_name)] == '|') {
            close(fd);
            return true;
        }
        if (false == read_fmt_entry(fd, rule)) {
            close(fd);
            return false;
        }
    }

    close(fd);

    return false;
}

bool InfoSource::add_rule(char* rule) {

    char name[MSG_MAX_SIZE+1];
    sprintf(name, "%s.fmt", path);
    for ( int i = 0; name[i] != '\0'; ++i) {
        if (name[i] == '/') {
            name[i] = '_';
        }
    }

    char* p = strchr(rule, '|');
    if(p == nullptr) {
        return false;
    }

    p[0] = '\0';

    if(true == has_rule(rule)) {
        return false;
    }

    p[0] = '|';

    pthread_mutex_lock(&rules_file_mutex);

    int fd = open(name, O_WRONLY, 0);
    if (fd < 0) {
        pthread_mutex_unlock(&rules_file_mutex);
        return false;
    }
    
    if ( 0 != lseek(fd, 0, SEEK_END)) {
        if ( 0 > write(fd, "\n", strlen("\n")) ) {
            pthread_mutex_unlock(&rules_file_mutex);
            perror("write");
            close(fd);
            return false;
        }
    }

    if ( 0 > write(fd, rule, strlen(rule)) ) {
        pthread_mutex_unlock(&rules_file_mutex);
        perror("write");
        close(fd);
        return false;
    }

    close(fd);

    pthread_mutex_unlock(&rules_file_mutex);

    return true;
}

void* fnc_monitor_infosource(void* p) {
    InfoSource* self = (InfoSource*) p;
    
    char message[MSG_MAX_SIZE+1], parsed_message[MSG_MAX_SIZE+1];

    bzero(message,sizeof(message));

    printf("Reading log...\n");

    while(1) {
        if(false == self->read_entry(self->logfd, message)) {
            printf("%s > Error reading from log.", self->path);
            continue;
        }
        #ifdef ag_debug
            char* p = strchr(message, '\n');
            if (p != nullptr) p[0] = '\0';
            printf(COLOR_LOG "Read : %s..\n" COLOR_OFF, message);
            if (p != nullptr) p[0] = '\n';
        #endif
        int ret_state = self->parse_entry(message, parsed_message);
        if (true ==  ret_state) {
            buffer_change_endian(parsed_message, strlen(parsed_message));
            if(false == self->send_entry(transfer_sd,parsed_message)) {
                printf("%s > Error sending log.", self->path);
                continue;
            }
            //printf("Am trimis\n");
        }
        else if (-1 == ret_state){
            printf("%s > Error parsing log.", self->path);
            continue;
        }
#ifdef ag_debug
        else if (false == ret_state){
            printf("N-am trimis:%s\n",parsed_message);
        }
#endif
    }
    self->unregister();
    return nullptr;
}

InfoSource* createIS(const char* mypath) {
    printf("Adding %s..\n",mypath);
    pthread_mutex_lock(&sources_mutex);
    for(auto i : sources) {
        if (0 == strcmp(i->path,mypath)) {
            printf("Source already active\n");
            pthread_mutex_unlock(&sources_mutex);
            return nullptr;
        }
    }
    pthread_mutex_unlock(&sources_mutex);

    int logfd = open(mypath,O_RDONLY);
    if (logfd < 0) {
        perror("Opening InfoSource path");
        return nullptr;
    }
    lseek(logfd,0,SEEK_END);


    char name[MSG_MAX_SIZE+1];
    sprintf(name, "%s.fmt", mypath);
    for ( int i = 0; name[i] != '\0'; ++i) {
        if (name[i] == '/') {
            name[i] = '_';
        }
    }

    int rulesfd = 0;
    if((rulesfd = open(name, O_RDWR | O_CREAT, 0750)) < 0) {
        close(logfd);
        return nullptr;
    }
    close(rulesfd);

    InfoSource* myIS = new InfoSource(mypath, logfd);
    pthread_t tid;

    if( 0 != pthread_create(&tid, nullptr, fnc_monitor_infosource, (void*)myIS)) {
        perror("Failed to make Info Source Monitor");
        close(logfd);
        exit(1);
    }

    myIS->tid = tid;

    pthread_mutex_lock(&sources_mutex);
    sources.push_back(myIS);
    pthread_mutex_unlock(&sources_mutex);
    
    return myIS;
}

InfoSource::~InfoSource() {
    void* retval = nullptr;
    int err;
    if( pthread_self() != this->tid) {
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
    }
    else {
        pthread_detach(pthread_self());

    }
    
    close(logfd);
    printf("InfoSource %s destroyed\n", this->path);
    delete[] path;
}