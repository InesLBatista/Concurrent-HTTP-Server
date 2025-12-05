#ifndef WORKER_H
#define WORKER_H

#include "shared_mem.h"
#include "semaphores.h"
#include "config.h"
#include <pthread.h>

typedef struct {
    int worker_id;
    shared_data_t* shared_data;
    semaphores_t* semaphores;
    server_config_t* config;
    void* pool; // thread_pool_t*
} worker_thread_arg_t;

void worker_main(int worker_id, shared_data_t* shared_data, 
                 semaphores_t* semaphores, server_config_t* config);
void update_statistics(shared_data_t* data, semaphores_t* sems, 
                       int status_code, size_t bytes);

#endif