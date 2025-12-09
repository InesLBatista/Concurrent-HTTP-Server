#include "shared_mem.h"
#include "config.h"
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

/* Access global configuration for the timeout interval */
extern server_config_t config;

/*
 * Statistics Monitor Thread
 * Purpose: This thread runs in the background (typically in the Master process)
 * and periodically prints the server's performance metrics to the standard output.
 * It provides a real-time dashboard of the server's health.
 *
 * Parameters:
 * - arg: Unused (required by pthread_create signature).
 *
 * Logic:
 * 1. Sleeps for a configured interval (e.g., 30 seconds).
 * 2. Acquires the stats mutex to ensure it reads a consistent snapshot of the data.
 * (This prevents reading partially updated counters from workers).
 * 3. Calculates derived metrics (e.g., Average Response Time).
 * 4. Prints a formatted report.
 * 5. Releases the mutex.
 */
void *stats_monitor_thread(void *arg) {
    (void)arg; /* Mark unused parameter to avoid compiler warnings */
    
    while (1) {
        /* Wait for the next reporting interval defined in server.conf */
        sleep(config.timeout_seconds);

        /* Enter Critical Section: Lock stats to prevent modification during read */
        sem_wait(&stats->mutex);
        
        /* Calculate Average Response Time (avoid division by zero) */
        double avg_time = 0.0;
        if (stats->total_requests > 0) {
            avg_time = (double)stats->average_response_time / stats->total_requests;
        }

        /* Display Statistics Dashboard */
        printf("\n=== SERVER STATISTICS ===\n");
        printf("Active Connections: %d\n", stats->active_connections);
        printf("Total Requests:     %ld\n", stats->total_requests);
        printf("Bytes Transferred:  %ld\n", stats->bytes_transferred);
        printf("Avg Response Time:  %.2f ms\n", avg_time);
        printf("Status 200 (OK):    %ld\n", stats->status_200);
        printf("Status 404 (NF):    %ld\n", stats->status_404);
        printf("Status 500 (Err):   %ld\n", stats->status_500);
        printf("=========================\n\n");

        /* Exit Critical Section */
        sem_post(&stats->mutex);
    }
    return NULL;
}