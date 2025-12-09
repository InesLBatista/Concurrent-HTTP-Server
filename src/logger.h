#ifndef LOGGER_H
#define LOGGER_H

#include <semaphore.h>
#include <stddef.h>

#define MAX_LOG_FILE_SIZE (10 * 1024 * 1024) 
#define LOG_BUFFER_SIZE 4096

void init_logger();

void log_request(sem_t *log_sem, const char *client_ip, const char *method,
                 const char *path, int status, size_t bytes);

void flush_logger(sem_t *log_sem);
void flush_logger(sem_t *log_sem);

void *logger_flush_thread(void *arg);

void logger_request_shutdown();

#endif