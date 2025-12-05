#include "semaphores.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#define SEMAPHORE_NAMESPACE "/webserver_sem_"

int semaphores_init(semaphores_t *sems, int queue_size) {
    if (!sems) {
        return -1;
    }
    
    // Initialize all semaphores to NULL
    memset(sems, 0, sizeof(semaphores_t));
    
    // Create unique semaphore names using process ID
    pid_t pid = getpid();
    char sem_name[64];
    
    // Empty slots semaphore (initially has queue_size empty slots)
    snprintf(sem_name, sizeof(sem_name), "%s_empty_%d", SEMAPHORE_NAMESPACE, pid);
    sems->empty_slots = sem_open(sem_name, O_CREAT | O_EXCL, 0644, queue_size);
    if (sems->empty_slots == SEM_FAILED) {
        fprintf(stderr, "Failed to create empty_slots semaphore: %s\n", strerror(errno));
        return -1;
    }
    strncpy(sems->empty_slots_name, sem_name, sizeof(sems->empty_slots_name));
    
    // Filled slots semaphore (initially 0 filled slots)
    snprintf(sem_name, sizeof(sem_name), "%s_filled_%d", SEMAPHORE_NAMESPACE, pid);
    sems->filled_slots = sem_open(sem_name, O_CREAT | O_EXCL, 0644, 0);
    if (sems->filled_slots == SEM_FAILED) {
        fprintf(stderr, "Failed to create filled_slots semaphore: %s\n", strerror(errno));
        semaphores_destroy(sems);
        return -1;
    }
    strncpy(sems->filled_slots_name, sem_name, sizeof(sems->filled_slots_name));
    
    // Queue mutex semaphore (binary semaphore)
    snprintf(sem_name, sizeof(sem_name), "%s_queue_%d", SEMAPHORE_NAMESPACE, pid);
    sems->queue_mutex = sem_open(sem_name, O_CREAT | O_EXCL, 0644, 1);
    if (sems->queue_mutex == SEM_FAILED) {
        fprintf(stderr, "Failed to create queue_mutex semaphore: %s\n", strerror(errno));
        semaphores_destroy(sems);
        return -1;
    }
    strncpy(sems->queue_mutex_name, sem_name, sizeof(sems->queue_mutex_name));
    
    // Statistics mutex semaphore (binary semaphore)
    snprintf(sem_name, sizeof(sem_name), "%s_stats_%d", SEMAPHORE_NAMESPACE, pid);
    sems->stats_mutex = sem_open(sem_name, O_CREAT | O_EXCL, 0644, 1);
    if (sems->stats_mutex == SEM_FAILED) {
        fprintf(stderr, "Failed to create stats_mutex semaphore: %s\n", strerror(errno));
        semaphores_destroy(sems);
        return -1;
    }
    strncpy(sems->stats_mutex_name, sem_name, sizeof(sems->stats_mutex_name));
    
    // Log mutex semaphore (binary semaphore)
    snprintf(sem_name, sizeof(sem_name), "%s_log_%d", SEMAPHORE_NAMESPACE, pid);
    sems->log_mutex = sem_open(sem_name, O_CREAT | O_EXCL, 0644, 1);
    if (sems->log_mutex == SEM_FAILED) {
        fprintf(stderr, "Failed to create log_mutex semaphore: %s\n", strerror(errno));
        semaphores_destroy(sems);
        return -1;
    }
    strncpy(sems->log_mutex_name, sem_name, sizeof(sems->log_mutex_name));
    
    printf("Semaphores initialized successfully\n");
    return 0;
}

void semaphores_destroy(semaphores_t *sems) {
    if (!sems) {
        return;
    }
    
    // Close and unlink all semaphores
    if (sems->empty_slots != SEM_FAILED) {
        sem_close(sems->empty_slots);
        sem_unlink(sems->empty_slots_name);
    }
    
    if (sems->filled_slots != SEM_FAILED) {
        sem_close(sems->filled_slots);
        sem_unlink(sems->filled_slots_name);
    }
    
    if (sems->queue_mutex != SEM_FAILED) {
        sem_close(sems->queue_mutex);
        sem_unlink(sems->queue_mutex_name);
    }
    
    if (sems->stats_mutex != SEM_FAILED) {
        sem_close(sems->stats_mutex);
        sem_unlink(sems->stats_mutex_name);
    }
    
    if (sems->log_mutex != SEM_FAILED) {
        sem_close(sems->log_mutex);
        sem_unlink(sems->log_mutex_name);
    }
    
    printf("Semaphores destroyed\n");
}

int semaphore_wait(sem_t *sem, const char *sem_name) {
    if (sem_wait(sem) == -1) {
        fprintf(stderr, "Failed to wait on semaphore %s: %s\n", 
                sem_name, strerror(errno));
        return -1;
    }
    return 0;
}

int semaphore_post(sem_t *sem, const char *sem_name) {
    if (sem_post(sem) == -1) {
        fprintf(stderr, "Failed to post on semaphore %s: %s\n", 
                sem_name, strerror(errno));
        return -1;
    }
    return 0;
}

int semaphore_trywait(sem_t *sem, const char *sem_name) {
    if (sem_trywait(sem) == -1) {
        if (errno != EAGAIN) {
            fprintf(stderr, "Failed to trywait on semaphore %s: %s\n", 
                    sem_name, strerror(errno));
        }
        return -1;
    }
    return 0;
}

int semaphore_getvalue(sem_t *sem, int *value, const char *sem_name) {
    if (sem_getvalue(sem, value) == -1) {
        fprintf(stderr, "Failed to get value of semaphore %s: %s\n", 
                sem_name, strerror(errno));
        return -1;
    }
    return 0;
}

void semaphores_print_status(const semaphores_t *sems) {
    if (!sems) {
        return;
    }
    
    int empty_value, filled_value, queue_value, stats_value, log_value;
    
    sem_getvalue(sems->empty_slots, &empty_value);
    sem_getvalue(sems->filled_slots, &filled_value);
    sem_getvalue(sems->queue_mutex, &queue_value);
    sem_getvalue(sems->stats_mutex, &stats_value);
    sem_getvalue(sems->log_mutex, &log_value);
    
    printf("\n=== Semaphore Status ===\n");
    printf("Empty slots: %d\n", empty_value);
    printf("Filled slots: %d\n", filled_value);
    printf("Queue mutex: %s\n", queue_value > 0 ? "unlocked" : "locked");
    printf("Stats mutex: %s\n", stats_value > 0 ? "unlocked" : "locked");
    printf("Log mutex: %s\n", log_value > 0 ? "unlocked" : "locked");
}