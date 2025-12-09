#include "semaphores.h"
#include <stdio.h>
#include <stdlib.h>

/*
 * Global Synchronization Primitives
 * These variables implement the classic Producer-Consumer synchronization pattern.
 *
 * - empty_slots: Semaphore counting the number of available spaces in the queue.
 * (Producer waits on this).
 * - filled_slots: Semaphore counting the number of items currently in the queue.
 * (Consumer waits on this).
 * - mutex: Mutual exclusion lock to protect the critical section (queue modification)
 * from concurrent access.
 */
sem_t empty_slots;
sem_t filled_slots;
pthread_mutex_t mutex;

/*
 * Initialize Semaphores
 * Purpose: Initializes the semaphores and mutex with the correct starting values
 * for a bounded buffer of size 'max_queue_size'.
 *
 * Parameters:
 * - max_queue_size: The capacity of the buffer/queue.
 *
 * Initialization Logic:
 * 1. empty_slots: Initialized to 'max_queue_size' because the buffer starts empty.
 * 2. filled_slots: Initialized to 0 because there are no items to consume yet.
 * 3. pshared=1: The second argument to sem_init is '1', indicating these semaphores
 * are intended to be shared between processes (requires the variables to be 
 * located in shared memory, though here they are global).
 *
 * Return:
 * - Void (Exits the program with status 1 on failure).
 */
void init_semaphores(int max_queue_size)
{
    /* Initialize empty_slots to the buffer capacity */
    if (sem_init(&empty_slots, 1, max_queue_size) != 0 ||
        /* Initialize filled_slots to 0 */
        sem_init(&filled_slots, 1, 0) != 0 ||
        /* Initialize the mutex with default attributes */
        pthread_mutex_init(&mutex, NULL) != 0)
    {
        perror("Semaphore or mutex init failed");
        exit(1);
    }
}