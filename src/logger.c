#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

void log_request(semaphores_t* sems, const char* client_ip, 
                 const char* method, const char* path, 
                 int status, size_t bytes) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%d/%b/%Y:%H:%M:%S %z", tm_info);
    
    // Acquire log semaphore
    sem_wait(sems->log_mutex);
    
    // Open log file
    FILE* log = fopen("access.log", "a");
    if (log) {
        fprintf(log, "%s - - [%s] \"%s %s HTTP/1.1\" %d %zu\n",
                client_ip, timestamp, method, path, status, bytes);
        fclose(log);
        
        // Check log rotation (simplified)
        struct stat st;
        if (stat("access.log", &st) == 0 && st.st_size > 10 * 1024 * 1024) {
            rename("access.log", "access.log.old");
        }
    }
    
    // Release log semaphore
    sem_post(sems->log_mutex);
}