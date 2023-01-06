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
#include "../common_definitions.h"

static const char* server_address;

bool get_request(char* request, char response[MSG_MAX_SIZE]) {
    unsigned int retry_counter = 0;
    int len;
    #ifdef cl_debug
printf(COLOR_CL_DEB); 
    printf("Sending :%s:",request); fflush(stdin);
    printf(COLOR_OFF);
fflush(stdout);
#endif
    buffer_change_endian(request, strlen(request));

Retry_get_request:
	retry_counter+=1;
	if(retry_counter >= 5) {
        printf("\n");
		return false;
	}
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if ( -1 == sockfd ) {
        perror("socket()");
        goto Retry_get_request;
    }
    //fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);

    struct sockaddr_in server_sockaddr;
    bzero(&server_sockaddr, sizeof(server_sockaddr));
    

    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = inet_addr(server_address);
    server_sockaddr.sin_port = htons (CLIENT_PORT);


    bool recieved_response = false;
    bzero(response, MSG_MAX_SIZE);
    
    while(!recieved_response) {
        if (sendto(sockfd, request, strlen(request), MSG_NOSIGNAL, (struct sockaddr*)&server_sockaddr, sizeof(server_sockaddr)) < 0) {
            perror("sendto");
            close(sockfd);
            goto Retry_get_request;
        }
        printf("."); fflush(stdout);
        sleep(1);
        if( 0 > (len = recv(sockfd, response, MSG_MAX_SIZE, MSG_DONTWAIT | MSG_NOSIGNAL))) {
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
    }
    printf("\n");
    close(sockfd);
    return true;
}

void wid_agent_list() {
    char response[MSG_MAX_SIZE];
    char request[2];
    request[0] = CLMSG_AGLIST;
    request[1] = '\0';
    get_request(request, response);

    

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

void wid_agent_properties(char* id) {
    char response[MSG_MAX_SIZE];
    char request[MSG_MAX_SIZE];
    request[0] = CLMSG_AGPROP;
    sprintf(request+1, "%s", id);
    get_request(request, response);

    
    printf("%s\n",response);
    
}

void wid_agent_add_is(char* id, char* path) {
    char response[MSG_MAX_SIZE];
    char request[MSG_MAX_SIZE];
    request[0] = CLMSG_ADDSRC;
    sprintf(request+1, "%s\n%s", id, path);
    get_request(request, response);

    
    printf("%s\n",response);
    
}

void wid_agent_add_rule(char* id, char* path, char* rule_name, char* rule) {
    char response[MSG_MAX_SIZE];
    char request[MSG_MAX_SIZE];
    request[0] = CLMSG_ADDRLE;
    if(MSG_MAX_SIZE < sprintf(request+1, "%s\n%s\n%s|%s", id, path, rule_name, rule))
        printf("WARNING: message too long!\n");
    else
        get_request(request, response);

    
    printf("%s\n",response);
    
}

void wid_agent_rm_rule(char* id, char* path, char* rule_name) {
    char response[MSG_MAX_SIZE];
    char request[MSG_MAX_SIZE];
    request[0] = CLMSG_RMVRLE;
    sprintf(request+1, "%s\n%s\n%s", id, path, rule_name);
    get_request(request, response);

    
    printf("%s\n",response);
    
}

void wid_agent_howmany(char* id, char* path) {
    char response[MSG_MAX_SIZE];
    char request[MSG_MAX_SIZE];
    request[0] = CLMSG_AG_HOWMANY_RULEPAGES;
    sprintf(request+1, "%s\n%s", id, path);
    get_request(request, response);

    if (atoi(response) != 0) {
        printf("Agent has %d rule pages\n", atoi(response));
    }
    else {
        printf("%s\n", response);
    }
    
}

void wid_agent_rulenames(char* id, char* path, char* page_nr) {
    char response[MSG_MAX_SIZE];
    char request[MSG_MAX_SIZE];
    request[0] = CLMSG_AG_LIST_RULEPAGE;
    sprintf(request+1, "%s\n%s\n%s", id, path, page_nr);
    get_request(request, response);

    
    printf("Rules on page %s: %s\n", page_nr, response);
    
}

void wid_agent_showrule(char* id, char* path, char* rule_name) {
    char response[MSG_MAX_SIZE];
    char request[MSG_MAX_SIZE];
    request[0] = CLMSG_AG_SHOW_RULE;
    sprintf(request+1, "%s\n%s\n%s", id, path, rule_name);
    get_request(request, response);

    
    printf("Rule %s: %s\n", rule_name, response);
    
}

void wid_agent_lsinfo(char* id) {
    char response[MSG_MAX_SIZE];
    char request[MSG_MAX_SIZE];
    request[0] = CLMSG_AG_LIST_SOURCES;
    sprintf(request+1, "%s", id);
    get_request(request, response);

    
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
    printf(MYCOLOR "lsinfo <agent-name>" COLOR_OFF " - show active info sources of <>agent-name>\n");
    printf(MYCOLOR "howmany <agent-name> <path>" COLOR_OFF " - show number of rule pages (there are %d rules/page)\n", ENTRIESPERPAGE);
    printf(MYCOLOR "add-source <agent-name> <path>" COLOR_OFF " - add file from <path> to the info sources of <agent-name>\n");
    printf(MYCOLOR "rulenames <agent-name> <path> <page>" COLOR_OFF " - show names of all rules on page <page>\n");
    printf(NOTDONE "!!query <agent-name> <path> <condition>" COLOR_OFF " - ask for <condition> from the log of <path> in <agent-name>\n");
    printf(MYCOLOR "rm-rule <agent-name> <path> <rule-name>" COLOR_OFF " - remove rule (referred to as <rule-name>) to watch in file from <path> of <agent-name>\n");
    printf(MYCOLOR "showrule <agent-name> <path> <rule-name>" COLOR_OFF " - show actual rule refered to as <rule-name>\n");
    printf(MYCOLOR "add-rule <agent-name> <path> <rule-name> <rule>" COLOR_OFF " - add <rule> (referred to as <rule-name>) to watch in file from <path> of <agent-name>\n");
    printf(MYCOLOR "======================" COLOR_OFF "\n");
}

int main(int argc, char* argv[]) {
    if(argc < 2) {
        printf("Usage: %s ip \n", argv[0]);
        return 0;
    }
    
    server_address = argv[1];

    help();

    char buffer[300]; bzero(buffer, 300);
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

            while( tok != nullptr) {

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
            if(nr_args < 1) {
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
                    printf("\nUsage : prop <agent-name> - show info on <agent-name>\n");
                }
                else {
                    printf("===============\n");
                    wid_agent_properties(args[1]);
                    printf("===============\n");
                }
            }
            else if(strcmp(args[0], "add-source") == 0) {
                if(nr_args < 3) {
                    printf("\nUsage : add-source <agent-name> <path> - add file from <path> to the info sources of <agent-name>\n");
                }
                else {
                    printf("===============\n");
                    wid_agent_add_is(args[1], args[2]);
                    printf("===============\n");
                }
            }
            else if(strcmp(args[0], "add-rule") == 0) {
                if(nr_args < 5) {
                    printf("\nUsage : add-rule <agent-name> <path> <rule-name> <rule> - add <rule> (referred to as <rule-name>) to watch in file from <path> of <agent-name>\n");
                }
                else {
                    printf("===============\n");
                    wid_agent_add_rule(args[1], args[2], args[3], args[4]);
                    printf("===============\n");
                }
            }
            else if(strcmp(args[0], "rm-rule") == 0) {
                if(nr_args < 4) {
                    printf("\nUsage : rm-rule <agent-name> <path> <rule-name> - remove rule (referred to as <rule-name>) to watch in file from <path> of <agent-name>\n");
                }
                else {
                    printf("===============\n");
                    wid_agent_rm_rule(args[1], args[2], args[3]);
                    printf("===============\n");
                }
            }
            else if(strcmp(args[0], "run") == 0) {
                if(nr_args < 2) {
                    printf("\nUsage : run <file-name> - run <file-name> as script in client terminal\n");
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
                    printf("\nUsage : howmany <agent-name> <path> - show number of rule pages (there are %d rules/page)\n", ENTRIESPERPAGE);
                }
                else {
                    printf("===============\n");
                    wid_agent_howmany(args[1], args[2]);
                    printf("===============\n");
                }
            }
            else if(strcmp(args[0], "rulenames") == 0) {
                if(nr_args < 4) {
                    printf("\nUsage : rulenames <agent-name> <path> <page> - show names of all rules on page <page>\n");
                }
                else {
                    printf("===============\n");
                    wid_agent_rulenames(args[1], args[2], args[3]);
                    printf("===============\n");
                }
            }
            else if(strcmp(args[0], "showrule") == 0) {
                if(nr_args < 4) {
                    printf("\nshowrule <agent-name> <path> <rule-name> - show actual rule refered to as <rule-name>\n");
                }
                else {
                    printf("===============\n");
                    wid_agent_showrule(args[1], args[2], args[3]);
                    printf("===============\n");
                }
            }
            else if(strcmp(args[0], "lsinfo") == 0) {
                if(nr_args < 2) {
                    printf("lsinfo <agent-name>" COLOR_OFF " - show active info sources of <>agent-name>\n");
                }
                else {
                    printf("===============\n");
                    wid_agent_lsinfo(args[1]);
                    printf("===============\n");
                }
            }
            else if(strcmp(args[0], "lsinfo") == 0) {
                if(nr_args < 2) {
                    printf("lsinfo <agent-name>" COLOR_OFF " - show active info sources of <>agent-name>\n");
                }
                else {
                    printf("===============\n");
                    wid_agent_lsinfo(args[1]);
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

            //sprintf(buffer, "%s", p+1);
        }
    }
}
