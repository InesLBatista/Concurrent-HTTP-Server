#ifndef SEMAPHORES_H
#define SEMAPHORES_H

#include <semaphore.h>
#include <unistd.h>

typedef struct {
    sem_t *empty_slots;           // Counts empty slots in queue
    sem_t *filled_slots;          // Counts filled slots in queue
    sem_t *queue_mutex;           // Binary semaphore for queue access
    sem_t *stats_mutex;           // Binary semaphore for statistics access
    sem_t *log_mutex;             // Binary semaphore for log access
    
    char empty_slots_name[64];    // Name for cleanup
    char filled_slots_name[64];
    char queue_mutex_name[64];
    char stats_mutex_name[64];
    char log_mutex_name[64];
} semaphores_t;

// Initialization and cleanup
int semaphores_init(semaphores_t *sems, int queue_size);
void semaphores_destroy(semaphores_t *sems);

// Semaphore operations (with error checking)
int semaphore_wait(sem_t *sem, const char *sem_name);
int semaphore_post(sem_t *sem, const char *sem_name);
int semaphore_trywait(sem_t *sem, const char *sem_name);
int semaphore_getvalue(sem_t *sem, int *value, const char *sem_name);

// Debugging
void semaphores_print_status(const semaphores_t *sems);

#endif // SEMAPHORES_H