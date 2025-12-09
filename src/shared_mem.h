#ifndef SHARED_MEM_H
#define SHARED_MEM_H

#include <semaphore.h>
#include <pthread.h>

typedef struct
{
    int *connections;
    int head;
    int tail;
    int max_size;

    sem_t empty_slots;
    sem_t filled_slots;
    pthread_mutex_t mutex;
    sem_t log_mutex;
    int shutting_down; 
} connection_queue_t;

typedef struct
{
    long total_requests;
    long bytes_transferred;
    long status_200;
    long status_404;
    long status_500;
    int active_connections;
    int average_response_time;
    sem_t mutex;
} server_stats_t;


extern connection_queue_t *queue;
extern server_stats_t *stats;

void init_shared_queue(int max_queue_size);
void init_shared_stats();
int enqueue(int client_socket);
int dequeue();

#endif