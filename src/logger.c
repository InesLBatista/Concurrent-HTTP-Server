// logger.c
// Inês Batista, Maria Quinteiro

// Implementa sistema de logging thread-safe que regista pedidos HTTP
// em formato Apache Combined. Usa semáforos para garantir que logs
// de múltiplas threads não se intercalem no ficheiro.

#include "logger.h"
#include <stdio.h>
#include <time.h>

void log_request(sem_t* log_sem, const char* client_ip, const char* method,
                const char* path, int status, size_t bytes) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%d/%b/%Y:%H:%M:%S %z", tm_info);
    
    sem_wait(log_sem);
    FILE* log = fopen("access.log", "a");
    if (log) {
        fprintf(log, "%s - - [%s] \"%s %s HTTP/1.1\" %d %zu\n",
                client_ip, timestamp, method, path, status, bytes);
        fclose(log);
    }
    sem_post(log_sem);
}