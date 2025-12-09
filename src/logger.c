#include "logger.h"
#include "config.h" 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

/* Access global configuration for file paths */
extern server_config_t config;

/*
 * In-Memory Log Buffer
 * Purpose: Aggregates multiple log entries in memory to minimize disk I/O.
 * Synchronization: Access is protected by the 'log_sem' semaphore.
 */
static char log_buffer[LOG_BUFFER_SIZE];
static size_t buffer_offset = 0;

/*
 * Shutdown Flag
 * Purpose: Signals the logger thread to stop running.
 * Synchronization: Accessed via atomic built-ins to prevent data races.
 */
static volatile int logger_shutting_down = 0;

/*
 * Log Rotation Logic
 * Purpose: Checks if the current log file exceeds the maximum size limit.
 * If so, renames it to ".old" to archive it.
 * Note: Caller must hold the log_sem lock (since this modifies file state).
 */
void check_and_rotate_log()
{
    struct stat st;
    /* Check file status using the configured filename */
    if (stat(config.log_file, &st) == 0)
    {
        if (st.st_size >= MAX_LOG_FILE_SIZE)
        {
            /* Create a sufficiently large buffer for the new filename */
            char old_log_name[512]; 
            
            snprintf(old_log_name, sizeof(old_log_name), "%s.old", config.log_file);
            rename(config.log_file, old_log_name);
        }
    }
}

/*
 * Internal Buffer Flush (Unsafe)
 * Purpose: Writes the current buffer content to disk and resets the offset.
 * Warning: This function is NOT thread-safe. The caller MUST hold the 
 * 'log_sem' lock before calling this function.
 */
void flush_buffer_to_disk_internal()
{
    if (buffer_offset == 0) return; /* Nothing to write */

    check_and_rotate_log();

    FILE *fp = fopen(config.log_file, "a");
    if (fp)
    {
        fwrite(log_buffer, 1, buffer_offset, fp);
        fclose(fp);
    }

    /* Reset buffer pointer */
    buffer_offset = 0;
}

/*
 * Public Buffer Flush (Thread-Safe)
 * Purpose: Forces a write of the log buffer to disk safely.
 * Synchronization: Acquires the semaphore before flushing.
 */
void flush_logger(sem_t *log_sem)
{
    sem_wait(log_sem);
    flush_buffer_to_disk_internal();
    sem_post(log_sem);
}

/*
 * Log a Request
 * Purpose: Formats an HTTP request log entry (Apache Common Log Format) and 
 * appends it to the in-memory buffer.
 *
 * Parameters:
 * - log_sem: Semaphore for synchronization.
 * - client_ip: IP address string of the client.
 * - method: HTTP method (GET, HEAD, etc.).
 * - path: The requested resource path.
 * - status: The HTTP response status code.
 * - bytes: The size of the response body sent.
 *
 * Synchronization:
 * - Uses localtime_r for thread-safe time formatting.
 * - Acquires log_sem for the critical section (buffer write).
 * - Automatically flushes if the buffer is full.
 */
void log_request(sem_t *log_sem, const char *client_ip, const char *method,
                 const char *path, int status, size_t bytes)
{
    /* 1. Generate Timestamp */
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info); /* Thread-safe struct tm */
    
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%d/%b/%Y:%H:%M:%S %z", &tm_info);

    /* 2. Format Log Entry */
    char entry[512];
    int len = snprintf(entry, sizeof(entry), "%s - - [%s] \"%s %s HTTP/1.1\" %d %zu\n",
                       client_ip, timestamp, method, path, status, bytes);

    if (len < 0) return;

    /* 3. Critical Section: Append to Buffer */
    sem_wait(log_sem);

    /* Flush first if there isn't enough space */
    if (buffer_offset + len >= LOG_BUFFER_SIZE)
    {
        flush_buffer_to_disk_internal();
    }

    memcpy(log_buffer + buffer_offset, entry, len);
    buffer_offset += len;

    sem_post(log_sem);
}

/*
 * Flush Wrapper (Compatibility)
 * Purpose: Wrapper for flush_logger to match expected interface.
 */
void flush_buffer_to_disk(sem_t *log_sem)
{
    flush_logger(log_sem);
}

/*
 * Background Logger Thread
 * Purpose: Periodically flushes the log buffer to disk to ensure logs 
 * are persisted even if the buffer isn't full.
 *
 * Logic:
 * - Sleeps in 1-second intervals (looping 5 times) to allow for a 
 * responsive shutdown (max 1s delay on CTRL+C).
 * - Uses atomic load to check the shutdown flag safely.
 */
void *logger_flush_thread(void *arg)
{
    sem_t *log_sem = (sem_t *)arg;

    while (!__atomic_load_n(&logger_shutting_down, __ATOMIC_SEQ_CST))
    {
        /* Check shutdown status every second instead of blocking for 5s */
        for (int i = 0; i < 5; i++) {
             if (__atomic_load_n(&logger_shutting_down, __ATOMIC_SEQ_CST)) break;
             sleep(1);
        }

        flush_logger(log_sem);
    }

    /* Ensure any remaining logs are written before thread exit */
    flush_logger(log_sem);
    return NULL;
}

/*
 * Request Logger Shutdown
 * Purpose: Signals the flush thread to terminate.
 * Synchronization: Uses atomic store to prevent data races with the reading thread.
 */
void logger_request_shutdown()
{
    __atomic_store_n(&logger_shutting_down, 1, __ATOMIC_SEQ_CST);
}