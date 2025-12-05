#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>

typedef struct {
    pthread_t* threads;
    int num_threads;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int shutdown;
    
    // Work queue
    int* queue;
    int queue_size;
    int queue_front;
    int queue_rear;
    int queue_count;
} thread_pool_t;

void thread_pool_init(thread_pool_t* pool, int num_threads, int queue_size);
void thread_pool_shutdown(thread_pool_t* pool);
void thread_pool_destroy(thread_pool_t* pool);

#endif