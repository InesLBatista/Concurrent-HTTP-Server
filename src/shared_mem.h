#ifndef SHARED_MEM_H
#define SHARED_MEM_H

#include <time.h>
#include <stddef.h>
#include <pthread.h>

#define SHARED_QUEUE_MAX_SIZE 1000

typedef struct {
    int sockets[SHARED_QUEUE_MAX_SIZE];
    int capacity;
    int size;
    int front;
    int rear;
} shared_queue_t;

/* Incluir seu stats.h existente */
#include "stats.h"

typedef struct {
    shared_queue_t queue;
    stats_t stats;           /* Usando sua estrutura stats_t */
    pthread_mutex_t stats_mutex; /* Mutex para estatísticas compartilhadas */
    char shm_name[64];
} shared_data_t;

// Shared memory management
shared_data_t *shared_memory_create(size_t queue_size);
shared_data_t *shared_memory_attach(const char *shm_name);
void shared_memory_destroy(shared_data_t *data);

// Queue operations
int shared_queue_enqueue(shared_queue_t *queue, int socket_fd);
int shared_queue_dequeue(shared_queue_t *queue);
int shared_queue_is_empty(const shared_queue_t *queue);
int shared_queue_is_full(const shared_queue_t *queue);

// Statistics synchronization
void shared_stats_update_request(shared_data_t *data, int status_code, 
                                size_t bytes_sent, double response_time_ms);
void shared_stats_update_connection(shared_data_t *data, int is_new_connection);
void shared_stats_update_cache(shared_data_t *data, int cache_hit);
void shared_stats_update_error(shared_data_t *data);

// Debugging
void shared_memory_print_status(const shared_data_t *data);
void shared_memory_print_stats(const shared_data_t *data);

#endif // SHARED_MEM_H