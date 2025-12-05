#include "thread_pool.h"
#include <stdlib.h>
#include <stdio.h>

void thread_pool_init(thread_pool_t* pool, int num_threads, int queue_size) {
    pool->threads = malloc(sizeof(pthread_t) * num_threads);
    pool->num_threads = num_threads;
    pool->shutdown = 0;
    
    pool->queue = malloc(sizeof(int) * queue_size);
    pool->queue_size = queue_size;
    pool->queue_front = 0;
    pool->queue_rear = 0;
    pool->queue_count = 0;
    
    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->cond, NULL);
    
    printf("Thread pool initialized with %d threads, queue size %d\n", 
           num_threads, queue_size);
}

void thread_pool_shutdown(thread_pool_t* pool) {
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);
}

void thread_pool_destroy(thread_pool_t* pool) {
    free(pool->threads);
    free(pool->queue);
    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->cond);
}