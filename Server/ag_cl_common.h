#pragma once
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include "../common_definitions.h"

struct Request{
    pthread_t tid;
    char* rsp;
    Request(pthread_t mytid, char* buffer) : tid(mytid), rsp(buffer) {};
};
#include <list>
std::list<Request*> requests_list;

Request* get_Request(pthread_t tid) {
    printf("Searching for thread %ld",tid);
    for(auto i : requests_list) {
        if (i->tid == tid) {
            return i;
        }
    }
    return nullptr;
}

pthread_mutex_t clreq_mutex = PTHREAD_MUTEX_INITIALIZER;

void register_Request(Request*myReq) {
    pthread_mutex_lock(&clreq_mutex);
    requests_list.push_back(myReq);
    pthread_mutex_unlock(&clreq_mutex);
}

void unregister_Request(Request*myReq) {
    pthread_mutex_lock(&clreq_mutex);
    requests_list.remove(myReq);
    pthread_mutex_unlock(&clreq_mutex);
}