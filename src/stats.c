#include "stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

static stats_t global_stats;
static pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

void stats_init(stats_t *stats) {
    if (!stats) {
        return;
    }
    
    memset(stats, 0, sizeof(stats_t));
    stats->start_time = time(NULL);
    
    // Initialize status code counters
    for (int i = 0; i < MAX_STATUS_CODE; i++) {
        stats->status_counts[i] = 0;
    }
    
    // Initialize response time histogram
    for (int i = 0; i < MAX_RESPONSE_TIME_BUCKET; i++) {
        stats->response_time_buckets[i] = 0;
    }
}

void stats_update_request(stats_t *stats, int status_code, 
                         size_t bytes_sent, double response_time_ms) {
    if (!stats) {
        return;
    }
    
    pthread_mutex_lock(&stats_mutex);
    
    stats->total_requests++;
    stats->bytes_transferred += bytes_sent;
    
    // Update status code counter
    if (status_code >= 0 && status_code < MAX_STATUS_CODE) {
        stats->status_counts[status_code]++;
    }
    
    // Categorize by status class
    if (status_code >= 200 && status_code < 300) {
        stats->status_2xx++;
    } else if (status_code >= 300 && status_code < 400) {
        stats->status_3xx++;
    } else if (status_code >= 400 && status_code < 500) {
        stats->status_4xx++;
    } else if (status_code >= 500) {
        stats->status_5xx++;
    }
    
    // Update response time statistics
    stats->total_response_time_ms += response_time_ms;
    
    if (stats->total_requests == 1) {
        stats->min_response_time_ms = response_time_ms;
        stats->max_response_time_ms = response_time_ms;
    } else {
        if (response_time_ms < stats->min_response_time_ms) {
            stats->min_response_time_ms = response_time_ms;
        }
        if (response_time_ms > stats->max_response_time_ms) {
            stats->max_response_time_ms = response_time_ms;
        }
    }
    
    // Update response time histogram
    int bucket = (int)(response_time_ms / 100.0); // 100ms buckets
    if (bucket >= MAX_RESPONSE_TIME_BUCKET) {
        bucket = MAX_RESPONSE_TIME_BUCKET - 1;
    }
    stats->response_time_buckets[bucket]++;
    
    // Update active connections
    if (status_code > 0) {
        // Request completed
        if (stats->active_connections > 0) {
            stats->active_connections--;
        }
    } else {
        // New connection
        stats->active_connections++;
        if (stats->active_connections > stats->peak_connections) {
            stats->peak_connections = stats->active_connections;
        }
    }
    
    // Update throughput calculation
    struct timeval now;
    gettimeofday(&now, NULL);
    stats->last_update_time = now.tv_sec;
    
    pthread_mutex_unlock(&stats_mutex);
}

void stats_update_cache(stats_t *stats, int cache_hit) {
    if (!stats) {
        return;
    }
    
    pthread_mutex_lock(&stats_mutex);
    
    if (cache_hit) {
        stats->cache_hits++;
    } else {
        stats->cache_misses++;
    }
    
    pthread_mutex_unlock(&stats_mutex);
}

void stats_update_error(stats_t *stats) {
    if (!stats) {
        return;
    }
    
    pthread_mutex_lock(&stats_mutex);
    stats->total_errors++;
    pthread_mutex_unlock(&stats_mutex);
}

double stats_get_average_response_time(const stats_t *stats) {
    if (!stats || stats->total_requests == 0) {
        return 0.0;
    }
    return stats->total_response_time_ms / stats->total_requests;
}

double stats_get_requests_per_second(const stats_t *stats) {
    if (!stats) {
        return 0.0;
    }
    
    time_t now = time(NULL);
    double uptime = difftime(now, stats->start_time);
    
    if (uptime <= 0) {
        return 0.0;
    }
    
    return stats->total_requests / uptime;
}

double stats_get_throughput_kbps(const stats_t *stats) {
    if (!stats) {
        return 0.0;
    }
    
    time_t now = time(NULL);
    double uptime = difftime(now, stats->start_time);
    
    if (uptime <= 0) {
        return 0.0;
    }
    
    double bytes_per_second = stats->bytes_transferred / uptime;
    return (bytes_per_second * 8) / 1024.0; // Convert to kbps
}

double stats_get_cache_hit_ratio(const stats_t *stats) {
    if (!stats) {
        return 0.0;
    }
    
    long total_cache_access = stats->cache_hits + stats->cache_misses;
    if (total_cache_access == 0) {
        return 0.0;
    }
    
    return (stats->cache_hits * 100.0) / total_cache_access;
}

void stats_print(const stats_t *stats) {
    if (!stats) {
        return;
    }
    
    printf("\n=== Server Statistics ===\n");
    printf("Uptime: %.0f seconds\n", difftime(time(NULL), stats->start_time));
    printf("Total Requests: %ld\n", stats->total_requests);
    printf("Bytes Transferred: %ld (%.2f MB)\n", 
           stats->bytes_transferred, 
           stats->bytes_transferred / (1024.0 * 1024.0));
    printf("\nStatus Code Distribution:\n");
    printf("  2xx (Success): %ld\n", stats->status_2xx);
    printf("  3xx (Redirect): %ld\n", stats->status_3xx);
    printf("  4xx (Client Error): %ld\n", stats->status_4xx);
    printf("  5xx (Server Error): %ld\n", stats->status_5xx);
    printf("\nResponse Time (ms):\n");
    printf("  Min: %.2f\n", stats->min_response_time_ms);
    printf("  Max: %.2f\n", stats->max_response_time_ms);
    printf("  Avg: %.2f\n", stats_get_average_response_time(stats));
    printf("\nPerformance:\n");
    printf("  Requests/sec: %.2f\n", stats_get_requests_per_second(stats));
    printf("  Throughput: %.2f kbps\n", stats_get_throughput_kbps(stats));
    printf("\nCurrent Status:\n");
    printf("  Active Connections: %d\n", stats->active_connections);
    printf("  Peak Connections: %d\n", stats->peak_connections);
    printf("  Total Errors: %ld\n", stats->total_errors);
    printf("\nCache Performance:\n");
    printf("  Cache Hits: %ld\n", stats->cache_hits);
    printf("  Cache Misses: %ld\n", stats->cache_misses);
    printf("  Hit Ratio: %.2f%%\n", stats_get_cache_hit_ratio(stats));
    printf("==========================\n");
}

void stats_print_histogram(const stats_t *stats) {
    if (!stats) {
        return;
    }
    
    printf("\n=== Response Time Histogram ===\n");
    printf("Time (ms)  | Count     | Bar\n");
    printf("-----------+-----------+------------------------\n");
    
    for (int i = 0; i < MAX_RESPONSE_TIME_BUCKET; i++) {
        long count = stats->response_time_buckets[i];
        if (count > 0) {
            int bar_length = (int)((count * 50) / stats->total_requests);
            if (bar_length < 1 && count > 0) bar_length = 1;
            
            printf("%4d-%-4d | %9ld | ", 
                   i * 100, (i + 1) * 100 - 1, count);
            
            for (int j = 0; j < bar_length; j++) {
                printf("█");
            }
            printf("\n");
        }
    }
    printf("===============================\n");
}

void stats_reset(stats_t *stats) {
    if (!stats) {
        return;
    }
    
    pthread_mutex_lock(&stats_mutex);
    
    // Save start time and reset everything else
    time_t old_start_time = stats->start_time;
    memset(stats, 0, sizeof(stats_t));
    stats->start_time = old_start_time;
    
    pthread_mutex_unlock(&stats_mutex);
}

void stats_export_json(const stats_t *stats, const char *filename) {
    if (!stats || !filename) {
        return;
    }
    
    FILE *file = fopen(filename, "w");
    if (!file) {
        return;
    }
    
    fprintf(file, "{\n");
    fprintf(file, "  \"uptime_seconds\": %.0f,\n", 
            difftime(time(NULL), stats->start_time));
    fprintf(file, "  \"total_requests\": %ld,\n", stats->total_requests);
    fprintf(file, "  \"bytes_transferred\": %ld,\n", stats->bytes_transferred);
    fprintf(file, "  \"requests_per_second\": %.2f,\n", 
            stats_get_requests_per_second(stats));
    fprintf(file, "  \"throughput_kbps\": %.2f,\n", 
            stats_get_throughput_kbps(stats));
    fprintf(file, "  \"status_2xx\": %ld,\n", stats->status_2xx);
    fprintf(file, "  \"status_3xx\": %ld,\n", stats->status_3xx);
    fprintf(file, "  \"status_4xx\": %ld,\n", stats->status_4xx);
    fprintf(file, "  \"status_5xx\": %ld,\n", stats->status_5xx);
    fprintf(file, "  \"active_connections\": %d,\n", stats->active_connections);
    fprintf(file, "  \"peak_connections\": %d,\n", stats->peak_connections);
    fprintf(file, "  \"total_errors\": %ld,\n", stats->total_errors);
    fprintf(file, "  \"cache_hits\": %ld,\n", stats->cache_hits);
    fprintf(file, "  \"cache_misses\": %ld,\n", stats->cache_misses);
    fprintf(file, "  \"cache_hit_ratio\": %.2f,\n", 
            stats_get_cache_hit_ratio(stats));
    fprintf(file, "  \"response_time_min_ms\": %.2f,\n", 
            stats->min_response_time_ms);
    fprintf(file, "  \"response_time_max_ms\": %.2f,\n", 
            stats->max_response_time_ms);
    fprintf(file, "  \"response_time_avg_ms\": %.2f\n", 
            stats_get_average_response_time(stats));
    fprintf(file, "}\n");
    
    fclose(file);
}

stats_t *stats_get_global() {
    return &global_stats;
}