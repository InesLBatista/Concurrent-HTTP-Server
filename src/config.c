#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>

// Helper function to trim whitespace
static void trim_whitespace(char *str) {
    if (!str) return;
    
    char *end;
    
    // Trim leading whitespace
    while (isspace((unsigned char)*str)) {
        str++;
    }
    
    if (*str == 0) {
        return;
    }
    
    // Trim trailing whitespace
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }
    
    *(end + 1) = 0;
}

// Helper function to trim quotes
static void trim_quotes(char *str) {
    if (!str || strlen(str) < 2) return;
    
    if (str[0] == '"' && str[strlen(str) - 1] == '"') {
        memmove(str, str + 1, strlen(str) - 2);
        str[strlen(str) - 2] = 0;
    } else if (str[0] == '\'' && str[strlen(str) - 1] == '\'') {
        memmove(str, str + 1, strlen(str) - 2);
        str[strlen(str) - 2] = 0;
    }
}

// Helper function to parse boolean values
static int parse_boolean(const char *value) {
    if (!value) return 0;
    
    if (strcasecmp(value, "true") == 0 ||
        strcasecmp(value, "yes") == 0 ||
        strcasecmp(value, "on") == 0 ||
        strcasecmp(value, "1") == 0 ||
        strcasecmp(value, "enabled") == 0) {
        return 1;
    }
    return 0;
}

// Helper function to parse integers with range checking
static int parse_integer(const char *value, int min, int max, int *result) {
    if (!value || !result) return -1;
    
    char *endptr;
    long val = strtol(value, &endptr, 10);
    
    if (*endptr != '\0' || errno == ERANGE) {
        return -1;
    }
    
    if (val < min || val > max) {
        return -1;
    }
    
    *result = (int)val;
    return 0;
}

// Convert log level to string
const char* log_level_to_string(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

// Convert string to log level
log_level_t string_to_log_level(const char *str) {
    if (!str) return LOG_LEVEL_INFO;
    
    if (strcasecmp(str, "DEBUG") == 0) return LOG_LEVEL_DEBUG;
    if (strcasecmp(str, "INFO") == 0) return LOG_LEVEL_INFO;
    if (strcasecmp(str, "WARN") == 0) return LOG_LEVEL_WARN;
    if (strcasecmp(str, "ERROR") == 0) return LOG_LEVEL_ERROR;
    
    return LOG_LEVEL_INFO; // Default
}

// Set default configuration values
void config_set_defaults(server_config_t *config) {
    if (!config) return;
    
    memset(config, 0, sizeof(server_config_t));
    
    // Network settings
    config->port = CONFIG_DEFAULT_PORT;
    config->timeout_seconds = CONFIG_DEFAULT_TIMEOUT;
    config->max_connections = CONFIG_DEFAULT_MAX_CONNECTIONS;
    config->keep_alive_timeout = 15;
    config->max_keep_alive_requests = 100;
    
    // File system settings
    strcpy(config->document_root, "./www");
    strcpy(config->default_charset, "utf-8");
    config->allow_directory_listing = 0;
    
    // Process architecture
    config->num_workers = CONFIG_DEFAULT_WORKERS;
    config->threads_per_worker = CONFIG_DEFAULT_THREADS;
    config->max_queue_size = CONFIG_DEFAULT_QUEUE_SIZE;
    
    // Caching
    config->cache_size_mb = CONFIG_DEFAULT_CACHE_SIZE_MB;
    config->enable_cache = 1;
    
    // Logging
    strcpy(config->log_file, "access.log");
    config->enable_logging = 1;
    config->log_level = LOG_LEVEL_INFO;
    
    // Server identification
    strcpy(config->server_name, "ConcurrentHTTP/1.0");
    
    // Additional flags
    config->daemon_mode = 0;
    config->verbose = 0;
}

// Parse configuration file
int config_parse_file(const char *filename, server_config_t *config) {
    if (!filename || !config) {
        return -1;
    }
    
    FILE *file = fopen(filename, "r");
    if (!file) {
        // If file doesn't exist, use defaults
        fprintf(stderr, "Config file '%s' not found, using defaults\n", filename);
        config_set_defaults(config);
        return 0;
    }
    
    char line[CONFIG_MAX_LINE_LENGTH];
    int line_number = 0;
    int errors = 0;
    
    while (fgets(line, sizeof(line), file)) {
        line_number++;
        
        // Remove trailing newline
        line[strcspn(line, "\n")] = 0;
        
        // Skip empty lines and comments
        trim_whitespace(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        
        // Find equals sign
        char *equals = strchr(line, '=');
        if (!equals) {
            fprintf(stderr, "Warning: Invalid config line %d (no '='): %s\n", 
                    line_number, line);
            errors++;
            continue;
        }
        
        // Split key and value
        *equals = '\0';
        char *key = line;
        char *value = equals + 1;
        
        trim_whitespace(key);
        trim_whitespace(value);
        trim_quotes(value);
        
        // Parse configuration parameters
        if (strcmp(key, "PORT") == 0) {
            int port;
            if (parse_integer(value, 1, 65535, &port) == 0) {
                config->port = port;
            } else {
                fprintf(stderr, "Warning: Invalid port '%s' on line %d\n", 
                        value, line_number);
                errors++;
            }
        } else if (strcmp(key, "DOCUMENT_ROOT") == 0) {
            strncpy(config->document_root, value, sizeof(config->document_root) - 1);
            config->document_root[sizeof(config->document_root) - 1] = '\0';
        } else if (strcmp(key, "NUM_WORKERS") == 0) {
            int workers;
            if (parse_integer(value, 1, 64, &workers) == 0) {
                config->num_workers = workers;
            } else {
                fprintf(stderr, "Warning: Invalid NUM_WORKERS '%s' on line %d\n", 
                        value, line_number);
                errors++;
            }
        } else if (strcmp(key, "THREADS_PER_WORKER") == 0) {
            int threads;
            if (parse_integer(value, 1, 256, &threads) == 0) {
                config->threads_per_worker = threads;
            } else {
                fprintf(stderr, "Warning: Invalid THREADS_PER_WORKER '%s' on line %d\n", 
                        value, line_number);
                errors++;
            }
        } else if (strcmp(key, "MAX_QUEUE_SIZE") == 0) {
            int size;
            if (parse_integer(value, 1, 10000, &size) == 0) {
                config->max_queue_size = size;
            } else {
                fprintf(stderr, "Warning: Invalid MAX_QUEUE_SIZE '%s' on line %d\n", 
                        value, line_number);
                errors++;
            }
        } else if (strcmp(key, "LOG_FILE") == 0) {
            strncpy(config->log_file, value, sizeof(config->log_file) - 1);
            config->log_file[sizeof(config->log_file) - 1] = '\0';
        } else if (strcmp(key, "CACHE_SIZE_MB") == 0) {
            int cache_size;
            if (parse_integer(value, 0, 1024, &cache_size) == 0) {
                config->cache_size_mb = cache_size;
            } else {
                fprintf(stderr, "Warning: Invalid CACHE_SIZE_MB '%s' on line %d\n", 
                        value, line_number);
                errors++;
            }
        } else if (strcmp(key, "TIMEOUT_SECONDS") == 0) {
            int timeout;
            if (parse_integer(value, 1, 3600, &timeout) == 0) {
                config->timeout_seconds = timeout;
            } else {
                fprintf(stderr, "Warning: Invalid TIMEOUT_SECONDS '%s' on line %d\n", 
                        value, line_number);
                errors++;
            }
        } else if (strcmp(key, "MAX_CONNECTIONS") == 0) {
            int max_conn;
            if (parse_integer(value, 1, 100000, &max_conn) == 0) {
                config->max_connections = max_conn;
            } else {
                fprintf(stderr, "Warning: Invalid MAX_CONNECTIONS '%s' on line %d\n", 
                        value, line_number);
                errors++;
            }
        } else if (strcmp(key, "SERVER_NAME") == 0) {
            strncpy(config->server_name, value, sizeof(config->server_name) - 1);
            config->server_name[sizeof(config->server_name) - 1] = '\0';
        } else if (strcmp(key, "ENABLE_CACHE") == 0) {
            config->enable_cache = parse_boolean(value);
        } else if (strcmp(key, "ENABLE_LOGGING") == 0) {
            config->enable_logging = parse_boolean(value);
        } else if (strcmp(key, "LOG_LEVEL") == 0) {
            config->log_level = string_to_log_level(value);
        } else if (strcmp(key, "ALLOW_DIRECTORY_LISTING") == 0) {
            config->allow_directory_listing = parse_boolean(value);
        } else if (strcmp(key, "DEFAULT_CHARSET") == 0) {
            strncpy(config->default_charset, value, sizeof(config->default_charset) - 1);
            config->default_charset[sizeof(config->default_charset) - 1] = '\0';
        } else if (strcmp(key, "KEEP_ALIVE_TIMEOUT") == 0) {
            int timeout;
            if (parse_integer(value, 0, 300, &timeout) == 0) {
                config->keep_alive_timeout = timeout;
            } else {
                fprintf(stderr, "Warning: Invalid KEEP_ALIVE_TIMEOUT '%s' on line %d\n", 
                        value, line_number);
                errors++;
            }
        } else if (strcmp(key, "MAX_KEEP_ALIVE_REQUESTS") == 0) {
            int max_req;
            if (parse_integer(value, 0, 1000, &max_req) == 0) {
                config->max_keep_alive_requests = max_req;
            } else {
                fprintf(stderr, "Warning: Invalid MAX_KEEP_ALIVE_REQUESTS '%s' on line %d\n", 
                        value, line_number);
                errors++;
            }
        } else if (strcmp(key, "DAEMON_MODE") == 0) {
            config->daemon_mode = parse_boolean(value);
        } else if (strcmp(key, "VERBOSE") == 0) {
            config->verbose = parse_boolean(value);
        } else {
            fprintf(stderr, "Warning: Unknown config key '%s' on line %d\n", 
                    key, line_number);
            errors++;
        }
    }
    
    fclose(file);
    
    if (errors > 0) {
        fprintf(stderr, "Found %d error(s) in config file '%s'\n", errors, filename);
    }
    
    return errors == 0 ? 0 : -1;
}

// Print configuration
void config_print(const server_config_t *config) {
    if (!config) {
        printf("Configuration is NULL\n");
        return;
    }
    
    printf("\n=== Server Configuration ===\n");
    
    // Network settings
    printf("Network Settings:\n");
    printf("  Port: %d\n", config->port);
    printf("  Timeout: %d seconds\n", config->timeout_seconds);
    printf("  Max Connections: %d\n", config->max_connections);
    printf("  Keep-Alive Timeout: %d\n", config->keep_alive_timeout);
    printf("  Max Keep-Alive Requests: %d\n", config->max_keep_alive_requests);
    
    // File system settings
    printf("\nFile System Settings:\n");
    printf("  Document Root: %s\n", config->document_root);
    printf("  Default Charset: %s\n", config->default_charset);
    printf("  Directory Listing: %s\n", 
           config->allow_directory_listing ? "Allowed" : "Denied");
    
    // Process architecture
    printf("\nProcess Architecture:\n");
    printf("  Workers: %d\n", config->num_workers);
    printf("  Threads per Worker: %d\n", config->threads_per_worker);
    printf("  Max Queue Size: %d\n", config->max_queue_size);
    
    // Caching
    printf("\nCaching:\n");
    printf("  Enabled: %s\n", config->enable_cache ? "Yes" : "No");
    printf("  Cache Size: %d MB\n", config->cache_size_mb);
    
    // Logging
    printf("\nLogging:\n");
    printf("  Enabled: %s\n", config->enable_logging ? "Yes" : "No");
    printf("  Log File: %s\n", config->log_file);
    printf("  Log Level: %s\n", log_level_to_string(config->log_level));
    
    // Server identification
    printf("\nServer Identification:\n");
    printf("  Server Name: %s\n", config->server_name);
    
    // Additional flags
    printf("\nAdditional Flags:\n");
    printf("  Daemon Mode: %s\n", config->daemon_mode ? "Yes" : "No");
    printf("  Verbose: %s\n", config->verbose ? "Yes" : "No");
    
    printf("============================\n");
}

// Validate configuration
int config_validate(const server_config_t *config) {
    if (!config) {
        return -1;
    }
    
    int errors = 0;
    
    // Check port range
    if (config->port < 1 || config->port > 65535) {
        fprintf(stderr, "Error: Port %d out of range (1-65535)\n", config->port);
        errors++;
    }
    
    // Check workers
    if (config->num_workers < 1) {
        fprintf(stderr, "Error: NUM_WORKERS must be at least 1\n");
        errors++;
    }
    
    // Check threads
    if (config->threads_per_worker < 1) {
        fprintf(stderr, "Error: THREADS_PER_WORKER must be at least 1\n");
        errors++;
    }
    
    // Check queue size
    if (config->max_queue_size < 1) {
        fprintf(stderr, "Error: MAX_QUEUE_SIZE must be at least 1\n");
        errors++;
    }
    
    // Check timeout
    if (config->timeout_seconds < 1) {
        fprintf(stderr, "Error: TIMEOUT_SECONDS must be at least 1\n");
        errors++;
    }
    
    // Check cache size
    if (config->cache_size_mb < 0) {
        fprintf(stderr, "Error: CACHE_SIZE_MB cannot be negative\n");
        errors++;
    }
    
    // Check document root accessibility
    if (access(config->document_root, F_OK) != 0) {
        fprintf(stderr, "Warning: Document root '%s' does not exist\n", 
                config->document_root);
        // Not a fatal error, just a warning
    }
    
    return errors == 0 ? 0 : -1;
}