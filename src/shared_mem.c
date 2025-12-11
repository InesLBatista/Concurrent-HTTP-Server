#include "shared_mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>        
#include <fcntl.h>           

/* Global pointers to shared memory regions */
connection_queue_t *queue = NULL;
server_stats_t *stats = NULL;

/*
 * Initialize Shared Connection Queue
 * Purpose: Allocates a shared memory block to hold the connection queue structure
 * and the actual array of file descriptors. It also initializes the synchronization
 * primitives (mutexes and semaphores) required for safe concurrent access.
 *
 * Parameters:
 * - max_queue_size: The capacity of the circular buffer.
 *
 * Logic:
 * 1. Calculates total size: struct size + (int size * max elements).
 * 2. Uses mmap with MAP_SHARED | MAP_ANONYMOUS to create a shared region reachable
 * by child processes (forked after this call).
 * 3. Initializes PTHREAD_PROCESS_SHARED mutexes so they work across process boundaries.
 */
void init_shared_queue(int max_queue_size)
{
    /* Calculate memory requirements */
    size_t queue_data_size = sizeof(int) * max_queue_size;
    size_t total_size = sizeof(connection_queue_t) + queue_data_size;

    /* Allocate shared memory */
    void *mem_block = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (mem_block == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }

    queue = (connection_queue_t *)mem_block;
    /* Point the data array to the memory immediately following the struct */
    queue->connections = (int *)(queue + 1);

    /* Initialize circular buffer indices */
    queue->head = 0;
    queue->tail = 0;
    queue->max_size = max_queue_size;
    queue->shutting_down = 0;
    
    /* Initialize Process-Shared Mutex for the Queue */
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    if (pthread_mutex_init(&queue->mutex, &mutex_attr) != 0) {
        perror("mutex init");
        exit(1);
    }
    pthread_mutexattr_destroy(&mutex_attr);

    /* Initialize Semaphore for Logging (Binary Semaphore / Mutex) */
    if (sem_init(&queue->log_mutex, 1, 1) != 0) {
        perror("sem init log_mutex");
        exit(1);
    }
    /* Initialize Producer-Consumer Semaphores */
    if (sem_init(&queue->empty_slots, 1, max_queue_size) != 0 ||
        sem_init(&queue->filled_slots, 1, 0) != 0) {
        perror("sem init");
        exit(1);
    }
}

/*
 * Initialize Shared Statistics
 * Purpose: Allocates a shared memory block for server metrics (requests, bytes, etc.).
 *
 * Logic:
 * - Uses mmap for shared access.
 * - Initializes a process-shared semaphore (stats->mutex) to protect counter updates.
 */
void init_shared_stats()
{
    void *mem_block = mmap(NULL, sizeof(server_stats_t), 
                           PROT_READ | PROT_WRITE, 
                           MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (mem_block == MAP_FAILED) {
        perror("mmap stats failed");
        exit(1);
    }

    stats = (server_stats_t *)mem_block;

    /* Zero out all counters */
    stats->total_requests = 0;
    stats->bytes_transferred = 0;
    stats->status_200 = 0;
    stats->status_404 = 0;
    stats->status_500 = 0;
    stats->active_connections = 0;
    stats->average_response_time = 0;

    /* Initialize binary semaphore (value 1) for mutual exclusion */
    if (sem_init(&stats->mutex, 1, 1) != 0) {
        perror("sem init stats");
        exit(1);
    }
}

/*
 * Enqueue Connection (Producer)
 * Purpose: Adds a client socket FD to the circular buffer.
 * * Parameters:
 * - client_socket: The file descriptor to add.
 *
 * Return:
 * - 0 on success.
 * - -1 if the queue is full (EAGAIN) or shutting down.
 *
 * Synchronization:
 * 1. Checks 'shutting_down' flag.
 * 2. Waits on 'empty_slots' (decrements available space). Uses trywait to avoid blocking if full.
 * 3. Locks mutex to update 'tail' index safely.
 * 4. Signals 'filled_slots' (increments item count).
 */
int enqueue(int client_socket) {
    if (queue->shutting_down) {
        return -1;
    }

    /* Non-blocking wait for a free slot */
    if (sem_trywait(&queue->empty_slots) != 0) {
        if (errno == EAGAIN) {
            return -1; /* Queue Full */
        }
        perror("sem_trywait");
        return -1; 
    }

    pthread_mutex_lock(&queue->mutex);

    queue->connections[queue->tail] = client_socket;
    queue->tail = (queue->tail + 1) % queue->max_size;

    pthread_mutex_unlock(&queue->mutex);
    sem_post(&queue->filled_slots);
    
    return 0; 
}

/*
 * Dequeue Connection (Consumer)
 * Purpose: Removes and returns a client socket FD from the circular buffer.
 *
 * Return:
 * - Valid file descriptor on success.
 * - -1 if the queue is shutting down and empty.
 *
 * Synchronization:
 * 1. Waits on 'filled_slots' (blocks until data is available).
 * 2. Locks mutex to update 'head' index safely.
 * 3. Signals 'empty_slots' (increments available space).
 */
int dequeue() {
    sem_wait(&queue->filled_slots);
    pthread_mutex_lock(&queue->mutex);

    /* Check if we woke up due to shutdown signal */
    if (queue->shutting_down && queue->head == queue->tail) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    int client_socket = queue->connections[queue->head];
    queue->head = (queue->head + 1) % queue->max_size;

    pthread_mutex_unlock(&queue->mutex);
    sem_post(&queue->empty_slots);
    
    return client_socket;
}

