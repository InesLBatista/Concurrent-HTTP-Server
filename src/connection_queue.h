#ifndef CONNECTION_QUEUE_H
#define CONNECTION_QUEUE_H

#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

// Default queue configuration
#define DEFAULT_QUEUE_CAPACITY 100
#define QUEUE_TIMEOUT_MS 5000  // 5 seconds timeout for queue operations

// Queue statistics structure
typedef struct {
    uint64_t enqueue_count;     // Total enqueued connections
    uint64_t dequeue_count;     // Total dequeued connections
    uint64_t timeout_count;     // Timeout operations
    uint64_t reject_count;      // Rejected connections (queue full)
    uint64_t error_count;       // Error operations
    uint64_t peak_size;         // Maximum queue size reached
    time_t   start_time;        // Queue creation time
} queue_stats_t;

// Connection information structure
typedef struct {
    int socket_fd;              // Client socket file descriptor
    time_t arrival_time;        // Time when connection arrived
    uint32_t client_ip;         // Client IP address (network byte order)
    uint16_t client_port;       // Client port (network byte order)
    int worker_id;              // Assigned worker ID (-1 if not assigned)
    int priority;               // Connection priority (higher = more important)
} connection_info_t;

// Thread-safe bounded circular buffer queue
typedef struct {
    connection_info_t *items;   // Array of connection items
    int capacity;               // Maximum number of items
    int size;                   // Current number of items
    int front;                  // Front index (dequeue from here)
    int rear;                   // Rear index (enqueue here)
    
    // Synchronization primitives
    pthread_mutex_t lock;       // Mutex for queue access
    pthread_cond_t not_empty;   // Condition variable: queue not empty
    pthread_cond_t not_full;    // Condition variable: queue not full
    pthread_cond_t shutdown_cv; // Condition variable: shutdown signal
    
    // Semaphores for producer-consumer pattern
    sem_t *empty_slots;         // Counts empty slots
    sem_t *filled_slots;        // Counts filled slots
    
    // Statistics
    queue_stats_t stats;
    
    // State flags
    int shutdown;               // Shutdown flag (1 = shutting down)
    int closed;                 // Closed flag (1 = queue closed)
    
    // Performance tracking
    double avg_enqueue_time_ms; // Average enqueue time
    double avg_dequeue_time_ms; // Average dequeue time
    uint64_t total_enqueue_time_ms; // Total enqueue time
    uint64_t total_dequeue_time_ms; // Total dequeue time
} connection_queue_t;

// Queue management functions
connection_queue_t *queue_create(int capacity, const char *sem_prefix);
void queue_destroy(connection_queue_t *queue);

// Core queue operations
int queue_enqueue(connection_queue_t *queue, connection_info_t *conn_info);
int queue_dequeue(connection_queue_t *queue, connection_info_t *conn_info);
int queue_try_enqueue(connection_queue_t *queue, connection_info_t *conn_info, int timeout_ms);
int queue_try_dequeue(connection_queue_t *queue, connection_info_t *conn_info, int timeout_ms);

// Non-blocking operations
int queue_try_enqueue_nonblock(connection_queue_t *queue, connection_info_t *conn_info);
int queue_try_dequeue_nonblock(connection_queue_t *queue, connection_info_t *conn_info);

// Queue status functions
int queue_is_empty(const connection_queue_t *queue);
int queue_is_full(const connection_queue_t *queue);
int queue_get_size(const connection_queue_t *queue);
int queue_get_capacity(const connection_queue_t *queue);

// Queue control functions
void queue_shutdown(connection_queue_t *queue);
void queue_close(connection_queue_t *queue);
int queue_is_shutdown(const connection_queue_t *queue);
int queue_is_closed(const connection_queue_t *queue);
void queue_clear(connection_queue_t *queue);

// Statistics functions
void queue_get_stats(const connection_queue_t *queue, queue_stats_t *stats);
void queue_print_stats(const connection_queue_t *queue);
double queue_get_utilization(const connection_queue_t *queue);
double queue_get_throughput_enqueue(const connection_queue_t *queue);  // enqueues/sec
double queue_get_throughput_dequeue(const connection_queue_t *queue);  // dequeues/sec
double queue_get_avg_wait_time(const connection_queue_t *queue);       // average wait time

// Utility functions
int queue_peek_front(const connection_queue_t *queue, connection_info_t *conn_info);
int queue_remove_oldest(connection_queue_t *queue, int max_age_seconds);
int queue_contains(const connection_queue_t *queue, int socket_fd);
void queue_set_priority(connection_queue_t *queue, int socket_fd, int priority);

// Thread safety utilities
void queue_lock(connection_queue_t *queue);
void queue_unlock(connection_queue_t *queue);
void queue_wait_not_empty(connection_queue_t *queue);
void queue_wait_not_full(connection_queue_t *queue);
void queue_signal_not_empty(connection_queue_t *queue);
void queue_signal_not_full(connection_queue_t *queue);

// Debugging functions
void queue_dump(const connection_queue_t *queue);
void queue_validate(const connection_queue_t *queue);

#endif // CONNECTION_QUEUE_H