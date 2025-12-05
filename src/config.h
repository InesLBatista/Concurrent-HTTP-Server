#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

// Configuration constants
#define CONFIG_MAX_LINE_LENGTH 512
#define CONFIG_DEFAULT_PORT 8080
#define CONFIG_DEFAULT_WORKERS 4
#define CONFIG_DEFAULT_THREADS 10
#define CONFIG_DEFAULT_QUEUE_SIZE 100
#define CONFIG_DEFAULT_CACHE_SIZE_MB 10
#define CONFIG_DEFAULT_TIMEOUT 30
#define CONFIG_DEFAULT_MAX_CONNECTIONS 1000

// Log levels
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3
} log_level_t;

// Server configuration structure
typedef struct {
    // Network settings
    int port;
    int timeout_seconds;
    int max_connections;
    int keep_alive_timeout;
    int max_keep_alive_requests;
    
    // File system settings
    char document_root[256];
    char default_charset[32];
    int allow_directory_listing;
    
    // Process architecture
    int num_workers;
    int threads_per_worker;
    int max_queue_size;
    
    // Caching
    int cache_size_mb;
    int enable_cache;
    
    // Logging
    char log_file[256];
    int enable_logging;
    log_level_t log_level;
    
    // Server identification
    char server_name[256];
    
    // Additional flags
    int daemon_mode;
    int verbose;
} server_config_t;

// Function prototypes
void config_set_defaults(server_config_t *config);
int config_parse_file(const char *filename, server_config_t *config);
void config_print(const server_config_t *config);

// Helper functions
const char* log_level_to_string(log_level_t level);
log_level_t string_to_log_level(const char *str);

#endif // CONFIG_H