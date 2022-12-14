#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/wait.h>
#include <time.h>
#include "../common_definitions.h"
#include <list>

static const char* server_address;

bool get_request(char* request, char response[MSG_MAX_SIZE+1]) {
    unsigned int retry_counter = 0;
    int len_send = strlen(request);
    int len;
#ifdef cl_debug
    printf(COLOR_CL_DEB); 
    printf("Sending :%s:\n",request);
    printf(COLOR_OFF);
    fflush(stdout);
#endif
    buffer_change_endian(request, len_send);

Retry_get_request:
	retry_counter+=1;

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if ( -1 == sockfd ) {
        perror("socket()");
        goto Retry_get_request;
    }

    struct sockaddr_in server_sockaddr;
    bzero(&server_sockaddr, sizeof(server_sockaddr));
    

    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = inet_addr(server_address);
    server_sockaddr.sin_port = htons (CLIENT_PORT);


    bool recieved_response = false;
    bzero(response, MSG_MAX_SIZE+1);
    
    while(!recieved_response && retry_counter < 5) {
        if (sendto(sockfd, request, len_send, 0, (struct sockaddr*)&server_sockaddr, sizeof(server_sockaddr)) < 0) {
            perror("sendto");
            close(sockfd);
            goto Retry_get_request;
        }
        //printf("."); fflush(stdout);
        sleep(1);
        if( 0 > (len = recv(sockfd, response, MSG_MAX_SIZE+1, MSG_DONTWAIT))) {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
               ;
            else {
                perror("recv()");
                close(sockfd);
                goto Retry_get_request;
            }
        }
        else {
            buffer_change_endian(response, len);
            recieved_response = true;
        }
        retry_counter+=1;
    }
    if(retry_counter >= 5) {
        return false;
    }
    strcpy(response, response);
    close(sockfd);
    return true;
}

void wid_agent_list() {
    char response[MSG_MAX_SIZE+1];
    char request[MSG_MAX_SIZE+1];
    request[0] = CLMSG_AGLIST; 
    
    request[1] = '\0';
    if ( false == get_request(request, response) ) {
        printf("Error getting request. Is server down?\n");
        return;
    }

    

    char* p = strtok(response, "\n");

    while( p != NULL) {
        if (0 == strcmp(p + strlen(p) - 3, "(*)")) {
            p[strlen(p) - 3] = '\0';
            printf("online: %s\n", p);
            //add_online_agent(p);
            p[strlen(p) - 3] = '(';
        }
        else {
            printf("%s\n", p);
            //add_offline_agent(p);
        }
        
        p = strtok(NULL, "\n");
    }

    
}

void wid_agent_properties(const char* id) {
    char response[MSG_MAX_SIZE+1];
    char request[MSG_MAX_SIZE+1];
    request[0] = CLMSG_AGPROP;
    
    sprintf(request+1, "%s", id);
    if ( false == get_request(request, response) ) {
        printf("Error getting request\n");
        return;
    }

    
    printf("%s\n",response);
    
}

void wid_restart(const char* id) {
    char response[MSG_MAX_SIZE+1];
    char request[MSG_MAX_SIZE+1];
    request[0] = CLMSG_AG_RESTART;
    
    sprintf(request+ 1, "%s", id);
    if ( false == get_request(request, response) ) {
        printf("Error getting request\n");
        return;
    }

    
    printf("%s\n",response);
    
}

void wid_agent_add_is(const char* id, const char* path) {
    char response[MSG_MAX_SIZE+1];
    char request[MSG_MAX_SIZE+1];
    request[0] = CLMSG_ADDSRC;
    
    sprintf(request+1, "%s\n%s", id, path);
    if ( false == get_request(request, response) ) {
        printf("Error getting request\n");
        return;
    }

    
    printf("%s\n",response);
    
}

void wid_agent_add_rule(const char* id, const char* path, const char* rule_name, const char* rule) {
    char response[MSG_MAX_SIZE+1];
    char request[MSG_MAX_SIZE+1];
    request[0] = CLMSG_ADDRLE;
    
    if(MSG_MAX_SIZE < sprintf(request+1, "%s\n%s\n%s|%s", id, path, rule_name, rule))
        printf("WARNING: message too long!\n");
    else
    if ( false == get_request(request, response) ) {
        printf("Error getting request\n");
        return;
    }

    
    printf("%s\n",response);
    
}

void wid_agent_c_query(char* output, const char* id, const char* path, const char* conditions) {
    char response[MSG_MAX_SIZE+1];
    char request[MSG_MAX_SIZE+1];
    request[0] = CLMSG_COUNT_QUERY;
    
    
    if(MSG_MAX_SIZE < sprintf(request+1, "%s\n%s\n%s", id, path, conditions))
        printf("WARNING: message too long!\n");
    else
    if ( false == get_request(request, response) ) {
        printf("Error getting request\n");
        if(output != nullptr) sprintf(output, "Error: Couldn't get request");
        return;
    }

    
    if(output != nullptr) {
        sprintf(output,"%s",response);
    }
}

struct cqargs{const char* id; const char* path; char* _date_stuff; const char* glob_conditions;};

void* fnc_graph_th_query(void* p) {
    struct cqargs* args = (struct cqargs*) p;
    char conditions[MSG_MAX_SIZE+1];
    char response[MSG_MAX_SIZE+1]; bzero(response, MSG_MAX_SIZE+1);
    sprintf(conditions, "%s%s", args->glob_conditions, args->_date_stuff);
    delete[] (args->_date_stuff);
    wid_agent_c_query(response, args->id, args->path, conditions);
    if(atoi(response) == 0 && response[0] != '0') {
        return nullptr;
    }
    return new int(atoi(response));
}

void wid_graph(const char* id, const char* path, const char* samples_text, char* data1, char* data2, const char* conditions) {
    int samples = atoi(samples_text);
    time_t time1, time2;
    tm time;
    bzero(&time, sizeof(struct tm));
    char* err = strptime(data1, "%m_%d_%H:%M:%S", &time);
    if(err == 0 || err[0] != '\0') {
        printf("Invalid date 1 - parsing\n");
        return;
    }
    time1 = mktime(&time);
    if (time1 == -1) {
        printf("Invalid date 1 - mktime\n");
        return;
    }
    bzero(&time, sizeof(struct tm));
    err = strptime(data2, "%m_%d_%H:%M:%S", &time);
    if(err == 0 || err[0] != '\0') {
        printf("Invalid date 2 - parsing\n");
        return;
    }
    time2 = mktime(&time);
    if (time2 == -1) {
        printf("Invalid date 2 - mktime\n");
        return;
    }

    double rest, interval_float, interval, total_interval = difftime(time2,time1);
    if(samples > 0) interval = total_interval / (samples);
    else interval = total_interval;
    if(interval > 0) {
        time_t temp;
        temp = time1;
        time1 = time2;
        time2 = temp;
    }
    else {
        interval *= -1;
    }
    interval_float = interval - (int)interval;
    printf("Time per sample interval is:%fs\n", interval);
    interval = (int) interval;

    char aug_cond[MSG_MAX_SIZE+1];
    sprintf(aug_cond,"%s", conditions);
    char* date_stuff = aug_cond + strlen(conditions);
    int counts[samples]; bzero(counts, samples * sizeof(int));
    time_t t1, t2 = time2;
    char _date1[20], _date2[20];
    struct tm* for_text;

    struct cqargs args = {id, path, 0, conditions};
    struct cqargs my_args[2*samples]; bzero(my_args, 2 * samples * sizeof(args));
    pthread_t cqtid[2*samples]; memset(cqtid, 0, samples * sizeof(pthread_t));
    
    int i;
    bool had_err = false;
    for( i = 0 ; i < samples; ++i ) {
        
        
        
        t1 = t2 + (long) interval + (long) rest;
        if(i == samples && rest > 0) t1 += 1;
        if (rest >= 1) rest -= (int) rest;
        rest += interval_float;
    
        for_text = localtime(&t1);
        if (0 == strftime(_date1, 16, "%m %d %H:%M:%S", for_text)) {
            printf("Error\n");
            break;
        }
        for_text = localtime(&t2);
        if (0 == strftime(_date2, 16, "%m %d %H:%M:%S", for_text)) {
            printf("Error\n");
            break;
        }
        sprintf(date_stuff,"&__LOG_TIMESTAMP>\"%s\"&__LOG_TIMESTAMP<\"%s\"",_date2, _date1);
        {
            my_args[2*i] = args;
            my_args[2*i]._date_stuff =  new char[strlen(date_stuff)+1];
            strncpy(((my_args[2*i])._date_stuff), date_stuff, strlen(date_stuff)+1);
            
            if( 0 > pthread_create(&(cqtid[2*i]), 0, fnc_graph_th_query, &my_args[2*i])) {
                perror("creating pthread ");
                break;
            }
        }

        sprintf(date_stuff,"&__LOG_TIMESTAMP=\"%s\"",_date2);
        
        {
            my_args[2*i+1] = args;
            my_args[2*i+1]._date_stuff = new char[strlen(date_stuff)+1];
            sprintf(((my_args[2*i+1])._date_stuff), "%s", date_stuff);
            
            if( 0 > pthread_create(&(cqtid[2*i+1]), 0, fnc_graph_th_query, &my_args[2*i+1])) {
                perror("creating pthread ");
                break;
            }
        }

        t2 = t1;      
    }
    if (samples >= 1 && i == samples) {
        i -= 1;
    }
    for ( int j = 0; j <= 2*i+1; ++j ) {
        int* val = nullptr;
        if ( -1 == pthread_join(cqtid[j], (void**) &val) ) {
            perror("pthread_join");
        }
        else {
            if(val != nullptr) {
                counts[j/2] += *val;
                delete val;
            }
            else {
                had_err = true;
                counts[j/2] = -10;
            }
        }
    }

    int maxval = 0;
    printf("\n");
    for(int i = 0; i < samples; ++i) {
        if (counts[i] > maxval) maxval = counts[i];
        printf("%d>",counts[i]);
    }
    printf("\n");
    if (had_err == true) {
        printf("There were some errors\n");
    }

    const int height = 10;
    const double unit = ((double)maxval/height) > 0 ? ((double)maxval/height) : 1;
    const double epsilon = (double)1/2;
    for(int j = height; j >= 0; --j) {
        printf(":");
        for(int i = 0; i < samples; ++i) {
            if(((double)j)-epsilon <= ((double)counts[i])/unit && ((double)counts[i])/unit <= ((double)j)+epsilon) {
                printf("*");
            }
            else if (((double)j)-epsilon <= ((double)counts[i])/unit) {
                printf("|");
            }
            else {
                printf(" ");
            }
        }
        printf(":\n");
    }
    
}

void wid_agent_rm_rule(const char* id, const char* path, const char* rule_name) {
    char response[MSG_MAX_SIZE+1];
    char request[MSG_MAX_SIZE+1];
    request[0] = CLMSG_RMVRLE;
    
    
    sprintf(request + 1, "%s\n%s\n%s", id, path, rule_name);
    if ( false == get_request(request, response) ) {
        printf("Error getting request\n");
        return;
    }

    
    printf("%s\n",response);
    
}

void wid_agent_howmany(const char* id, const char* path) {
    char response[MSG_MAX_SIZE+1];
    char request[MSG_MAX_SIZE+1];
    request[0] = CLMSG_AG_HOWMANY_RULEPAGES;
        
    sprintf(request+1, "%s\n%s", id, path);
    if ( false == get_request(request, response) ) {
        printf("Error getting request\n");
        return;
    }

    if (atoi(response) != 0) {
        printf("Agent has %d rule pages\n", atoi(response));
    }
    else {
        printf("%s\n", response);
    }
    
}

void wid_agent_rulenames(const char* id, const char* path, const char* page_nr) {
    char response[MSG_MAX_SIZE+1];
    char request[MSG_MAX_SIZE+1];
    request[0] = CLMSG_AG_LIST_RULEPAGE;

    sprintf(request+1, "%s\n%s\n%s", id, path, page_nr);
    if ( false == get_request(request, response) ) {
        printf("Error getting request\n");
        return;
    }

    
    printf("Rules on page %s: %s\n", page_nr, response);
    
}

void wid_agent_showrule(const char* id, const char* path, const char* rule_name) {
    char response[MSG_MAX_SIZE+1];
    char request[MSG_MAX_SIZE+1];
    request[0] = CLMSG_AG_SHOW_RULE;
    
    sprintf(request+1, "%s\n%s\n%s", id, path, rule_name);
    if ( false == get_request(request, response) ) {
        printf("Error getting request\n");
        return;
    }

    
    printf("Rule %s: %s\n", rule_name, response);
    
}

void wid_agent_lsinfo(const char* id) {
    char response[MSG_MAX_SIZE+1];
    char request[MSG_MAX_SIZE+1];
    request[0] = CLMSG_AG_LIST_SOURCES;
    
    sprintf(request+1, "%s", id);
    if ( false == get_request(request, response) ) {
        printf("Error getting request\n");
        return;
    }

    
    printf("%s\n", response);
    
}

void help() {
    #define MYCOLOR  "\033[1;33m"
    #define NOTDONE  "\033[1;31m"

    printf(MYCOLOR "======================" COLOR_OFF "\n");
    printf(MYCOLOR "exit" COLOR_OFF " - \n");
    printf(MYCOLOR "help" COLOR_OFF " - show this\n");
    printf(MYCOLOR "list" COLOR_OFF " - list all agents\n");
    printf(MYCOLOR "run <file-name>" COLOR_OFF " - run <file-name> as script in client terminal\n");
    printf(MYCOLOR "prop <agent-name>" COLOR_OFF " - show info on <agent-name>\n");
    printf(MYCOLOR "lsinfo <agent-name>" COLOR_OFF " - show active info sources of <agent-name>\n");
    printf(MYCOLOR "restart <agent-name>" COLOR_OFF " -\n");
    printf(MYCOLOR "howmany <agent-name> <path>" COLOR_OFF " - show number of rule pages (there are %d rules/page)\n", ENTRIESPERPAGE);
    printf(MYCOLOR "add-source <agent-name> <path>" COLOR_OFF " - add file from <path> to the info sources of <agent-name>\n");
    printf(MYCOLOR "rulenames <agent-name> <path> <page>" COLOR_OFF " - show names of all rules on page <page>\n");
    printf(MYCOLOR "rm-rule <agent-name> <path> <rule-name>" COLOR_OFF " - remove rule (referred to as <rule-name>) to watch in file from <path> of <agent-name>\n");
    printf(MYCOLOR "c-query <agent-name> <path> `<conditions>" COLOR_OFF " - ask for entries matching <conditions> from the log of <path> in <agent-name>\n");
    printf(NOTDONE "<conditions>" COLOR_OFF " - \'&\' separated conditions of type :<name><op>\"<value>\":\nWhere <op> can be one of {=!<>}\nNO SPACES OUTSIDE \"\"!!\n");
    printf(MYCOLOR "showrule <agent-name> <path> <rule-name>" COLOR_OFF " - show actual rule refered to as <rule-name>\n");
    printf(MYCOLOR "add-rule <agent-name> <path> <rule-name> `<rule>" COLOR_OFF " - add <rule> (referred to as <rule-name>) to watch in file from <path> of <agent-name>\n");
    printf(MYCOLOR "graph <agent-name> <path> <sample-count> <date1> <date2> `<conditions>" COLOR_OFF " - graphs the count of entries which match <conditions> in the date interval with <sample-count> samples\n");
    printf(NOTDONE "<date>" COLOR_OFF " - format : \'MM_DD_HH:mm:ss\'\n");
    printf(MYCOLOR "======================" COLOR_OFF "\n");
}

int main(int argc, char* argv[]) {

    if(argc < 2) {
        printf("Usage: %s ip \n", argv[0]);
        return 0;
    }
    
    server_address = argv[1];

    help();

    char buffer[300+1]; bzero(buffer, 300+1);
    int already_read = 0, read_now;

    char* p;

    int stdin_backup = dup(0);

    bool done_reading_file = false;

    while(1) {
        already_read = strlen(buffer);
        if(0 >= (read_now = read(0, buffer + already_read, 300 - already_read))) {
            if(read_now == 0) {
                if(done_reading_file == false) {
                    done_reading_file = true;
                    strcat(buffer,"\n");
                    already_read += 1;
                }
                if (already_read == 0) {
                    dup2(stdin_backup, 0);
                    bzero(buffer, 300);
                    printf("Done executing script\n");
                    printf("===============\n");
                    continue;
                } 
            }
            else {
                perror("read");
                return 1;
            }
            
        }
        already_read += read_now;
        buffer[already_read] = '\0';

        p = strstr(buffer, "\\\n");

        while(p != nullptr) {
            p[0] = ' ';
            for(int i = 1; i <= strlen(p); ++i) {
                p[i] = p[i+1];
            }
            p = strstr(buffer, "\\\n");
        }

        if (nullptr != (p = strchr(buffer, '\n'))) {
            p[0] = '\0';
            p += 1;
            char* prev = buffer, *tok = buffer;
            char* args[10];
            int nr_args = 0;

            while( tok != nullptr && nr_args < 10) {

                tok = strchr(tok, ' ');
                if(tok != nullptr) {
                    if(tok[1] == '`') {
                        tok[0] = '\0';
                        args[nr_args++] = prev;
                        prev = tok + 2;
                        args[nr_args++] = prev;
                        break;
                    }
                    else {
                        tok[0] = '\0';
                        tok += 1;
                    }
                }

                args[nr_args++] = prev;
                prev = tok;
            }
            if(nr_args < 1 || nr_args > 7) {
                continue;
            }
            if(strcmp(args[0], "help") == 0) {
                help();
            }
            else if(strcmp(args[0], "exit") == 0) {
                return 0;
            }
            else if(strcmp(args[0], "list") == 0) {
                printf("===============\n");
                wid_agent_list();
                printf("===============\n");
            }
            else if(strcmp(args[0], "prop") == 0) {
                if(nr_args < 2) {
                    printf("\nUsage: prop <agent-name> - show info on <agent-name>\n");
                }
                else {
                    printf("===============\n");
                    wid_agent_properties(args[1]);
                    printf("===============\n");
                }
            }
            else if(strcmp(args[0], "add-source") == 0) {
                if(nr_args < 3) {
                    printf("\nUsage: add-source <agent-name> <path> - add file from <path> to the info sources of <agent-name>\n");
                }
                else {
                    printf("===============\n");
                    wid_agent_add_is(args[1], args[2]);
                    printf("===============\n");
                }
            }
            else if(strcmp(args[0], "add-rule") == 0) {
                if(nr_args < 5) {
                    printf("\nUsage: add-rule <agent-name> <path> <rule-name> `<rule> - add <rule> (referred to as <rule-name>) to watch in file from <path> of <agent-name>\n");
                }
                else {
                    printf("===============\n");
                    wid_agent_add_rule(args[1], args[2], args[3], args[4]);
                    printf("===============\n");
                }
            }
            else if(strcmp(args[0], "rm-rule") == 0) {
                if(nr_args < 4) {
                    printf("\nUsage: rm-rule <agent-name> <path> <rule-name> - remove rule (referred to as <rule-name>) to watch in file from <path> of <agent-name>\n");
                }
                else {
                    printf("===============\n");
                    wid_agent_rm_rule(args[1], args[2], args[3]);
                    printf("===============\n");
                }
            }
            else if(strcmp(args[0], "run") == 0) {
                if(nr_args < 2) {
                    printf("\nUsage: run <file-name> - run <file-name> as script in client terminal\n");
                }
                else {
                    int fd = open(args[1], O_RDONLY, 0);
                    if(fd < 0) {
                        perror("open");
                    }
                    else {
                        done_reading_file = false;
                        printf("===============\n");
                        printf("Running script..\n");
                        dup2(fd, 0);
                        close(fd);
                        bzero(buffer, 300);
                    }
                }
            }
            else if(strcmp(args[0], "howmany") == 0) {
                if(nr_args < 3) {
                    printf("\nUsage: howmany <agent-name> <path> - show number of rule pages (there are %d rules/page)\n", ENTRIESPERPAGE);
                }
                else {
                    printf("===============\n");
                    wid_agent_howmany(args[1], args[2]);
                    printf("===============\n");
                }
            }
            else if(strcmp(args[0], "rulenames") == 0) {
                if(nr_args < 4) {
                    printf("\nUsage: rulenames <agent-name> <path> <page> - show names of all rules on page <page>\n");
                }
                else {
                    printf("===============\n");
                    wid_agent_rulenames(args[1], args[2], args[3]);
                    printf("===============\n");
                }
            }
            else if(strcmp(args[0], "showrule") == 0) {
                if(nr_args < 4) {
                    printf("\nUsage: showrule <agent-name> <path> <rule-name> - show actual rule refered to as <rule-name>\n");
                }
                else {
                    printf("===============\n");
                    wid_agent_showrule(args[1], args[2], args[3]);
                    printf("===============\n");
                }
            }
            else if(strcmp(args[0], "lsinfo") == 0) {
                if(nr_args < 2) {
                    printf("\nUsage: lsinfo <agent-name>" COLOR_OFF " - show active info sources of <agent-name>\n");
                }
                else {
                    printf("===============\n");
                    wid_agent_lsinfo(args[1]);
                    printf("===============\n");
                }
            }
            else if(strcmp(args[0], "c-query") == 0) {
                if(nr_args < 4) {
                    printf("\nUsage: c-query <agent-name> <path> `<conditions> - ask for entries matching <conditions> from the log of <path> in <agent-name>\n");
                    printf(NOTDONE "<conditions>" COLOR_OFF " - \'&\' separated conditions of type :<name><op>\"<value>\":\nWhere <op> can be one of {=!<>}\nNO SPACES OUTSIDE \"\"!!\n");
                }
                else {
                    printf("===============\n");
                    char response[MSG_MAX_SIZE+1];
                    wid_agent_c_query(response, args[1], args[2], args[3]);
                    if(atoi(response) == 0 && response[0] != '0') {
                        printf("%s\n", response);
                    }
                    else {
                        printf("There were %s entries matching the conditions.\n",response);
                    }
                    printf("===============\n");
                }
            }
            else if(strcmp(args[0], "graph") == 0) {
                if(nr_args < 7) {
                    printf("\nUsage: graph <agent-name> <path> <sample-count> <date1> <date2> `<conditions> - graphs the count of entries which match <conditions> in the date interval with <sample-count> samples\n");
                    printf(NOTDONE "<conditions>" COLOR_OFF " - \'&\' separated conditions of type :<name><op>\"<value>\":\nWhere <op> can be one of {=!<>}\nNO SPACES OUTSIDE \"\"!!\n");
                    printf(NOTDONE "<date>" COLOR_OFF " - format : \'MM_DD_HH:mm:ss\'\n");
                }
                else {
                    printf("===============\n");
                    wid_graph(args[1], args[2], args[3], args[4], args[5], args[6]);
                    printf("===============\n");
                }
            }
            else if(strcmp(args[0], "restart") == 0) {
                if(nr_args < 2) {
                    printf("\nUsage: restart <agent-name> -\n");
                }
                else {
                    printf("===============\n");
                    wid_restart(args[1]);
                    printf("===============\n");
                }
            }
            else if (strlen(args[0]) > 0) {
                printf("Unrecognised command: %s\n", args[0]);
                //help();
            }

            for( int i = 0; i <= strlen(p); ++i) {
                buffer[i] = p[i];
            }
        }
    }
}
