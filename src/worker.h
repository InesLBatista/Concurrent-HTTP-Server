#ifndef WORKER_H
#define WORKER_H

#include <pthread.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <signal.h>

/* Incluir headers necessários para os tipos */
#include "config.h"        /* Para server_config_t */
#include "shared_mem.h"    /* Para shared_data_t */
#include "semaphores.h"    /* Para semaphores_t */
#include "thread_pool.h"   /* Para thread_pool_t */

/* Estrutura para argumentos do thread worker */
typedef struct {
    int worker_id;
    shared_data_t* shared_data;
    semaphores_t* semaphores;
    server_config_t* config;
    void* pool;  /* thread_pool_t* */
} worker_thread_arg_t;

/* Variável global para controle de execução */
extern volatile sig_atomic_t worker_running;

/* Protótipos das funções */
void worker_signal_handler(int signum);
int dequeue_connection(shared_data_t* data, semaphores_t* sems);
void* worker_thread_func(void* arg);
void update_statistics(shared_data_t* data, semaphores_t* sems, 
                       int status_code, size_t bytes);
void update_cache_statistics(shared_data_t* data, int cache_hit);
void update_error_statistics(shared_data_t* data);
void worker_main(int worker_id, shared_data_t* shared_data, 
                 semaphores_t* semaphores, server_config_t* config);
void log_cache_operation(int worker_id, const char* filename, int cache_hit);
void log_operation_time(int worker_id, const char* operation, 
                       double start_time_ms, double end_time_ms);

#endif /* WORKER_H */