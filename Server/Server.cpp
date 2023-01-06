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

#define COLOR_AGNAME   "\033[1;34m"

#include "AgentDispatcher.cpp"
#include "ClientDispatcher.cpp"


void create_agent_dispatcher(pthread_t *agent_tid) {
    if( 0 != pthread_create(agent_tid, nullptr, fnc_agent_control_dispatcher, nullptr)) {
        perror("Failed to make Agent dispatcher");
        exit(1);
    }
    
    if( 0 != pthread_create(agent_tid, nullptr, fnc_agent_transfer_dispatcher, nullptr)) {
        perror("Failed to make Agent transfer dispatcher");
        exit(1);
    }
    
    printf("Made Agent dispatcher\n");
}
void create_client_dispatcher(pthread_t *client_tid) {
    if ( 0 != pthread_create(client_tid, nullptr, fnc_client_dispatcher, nullptr)) {
        perror("Failed to make Client dispatcher");
        exit(1);
    }
    printf("Made Client dispatcher\n");
}
void daemon_thread() {
    //compute general statistics occasionally?
}

void init_files() {
    if(access(LOG_PATH,F_OK) != 0) {
		if( -1 == mkdir(LOG_PATH,0740) ) {
			perror("Error creating log directory\n");
			exit(1);
		}
	}
    else if(access(LOG_PATH,X_OK | W_OK) != 0) {
		perror("Insufficient permissions on log directory\n");
        exit(1);
	}
}

std::vector<struct Agent*> agent_list;

int main() {
    pthread_t agent_tid, client_tid;

    init_files();

    create_agent_dispatcher (&agent_tid );
    create_client_dispatcher(&client_tid);
    //daemon_thread();
    pause();
    return 0;
}
