#ifndef STATS_H
#define STATS_H

#include <pthread.h>
#include <time.h>
#include <stddef.h>

#define MAX_STATUS_CODE 600
#define MAX_RESPONSE_TIME_BUCKET 50  // 0-5000ms in 100ms buckets

typedef struct {
    // Basic counters
    long total_requests;
    long bytes_transferred;
    
    // Status code distribution
    long status_2xx;
    long status_3xx;
    long status_4xx;
    long status_5xx;
    long status_counts[MAX_STATUS_CODE];
    
    // Response time statistics
    double total_response_time_ms;
    double min_response_time_ms;
    double max_response_time_ms;
    long response_time_buckets[MAX_RESPONSE_TIME_BUCKET];
    
    // Connection statistics
    int active_connections;
    int peak_connections;
    
    // Error tracking
    long total_errors;
    
    // Cache statistics
    long cache_hits;
    long cache_misses;
    
    // Timing
    time_t start_time;
    time_t last_update_time;
} stats_t;

// Initialization
void stats_init(stats_t *stats);

// Update functions
void stats_update_request(stats_t *stats, int status_code, 
                         size_t bytes_sent, double response_time_ms);
void stats_update_cache(stats_t *stats, int cache_hit);
void stats_update_error(stats_t *stats);

// Calculation functions
double stats_get_average_response_time(const stats_t *stats);
double stats_get_requests_per_second(const stats_t *stats);
double stats_get_throughput_kbps(const stats_t *stats);
double stats_get_cache_hit_ratio(const stats_t *stats);

// Display functions
void stats_print(const stats_t *stats);
void stats_print_histogram(const stats_t *stats);

// Management
void stats_reset(stats_t *stats);
void stats_export_json(const stats_t *stats, const char *filename);

// Global statistics (for convenience)
stats_t *stats_get_global();

#endif // STATS_H