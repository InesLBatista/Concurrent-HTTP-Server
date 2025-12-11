#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * Load Server Configuration
 * Purpose: Parses a configuration file (key=value format) and populates the
 * server_config_t structure. Ignores comments starting with '#' and empty lines.
 *
 * Parameters:
 * - filename: Path to the configuration file (e.g., "server.conf").
 * - config: Pointer to the server_config_t structure to populate.
 *
 * Return:
 * - 0 on success.
 * - -1 if the file cannot be opened.
 */
int load_config(const char *filename, server_config_t *config)
{
    FILE *fp = fopen(filename, "r");
    if (!fp)
        return -1;

    char line[512], key[128], value[256];
    
    /* Iterate through the file line by line */
    while (fgets(line, sizeof(line), fp))
    {
        /* Skip comments and empty lines to prevent parsing errors */
        if (line[0] == '#' || line[0] == '\n')
            continue;

        /* * Parse line using sscanf format "%[^=]=%s"
         * - %[^=]: Read everything up to the equal sign into 'key'.
         * - =: Match the separator literal.
         * - %s: Read the remaining string into 'value' (stops at whitespace).
         */
        if (sscanf(line, "%[^=]=%s", key, value) == 2)
        {
            /* Map string keys to specific struct fields */
            if (strcmp(key, "PORT") == 0)
                config->port = atoi(value);
            else if (strcmp(key, "NUM_WORKERS") == 0)
                config->num_workers = atoi(value);
            else if (strcmp(key, "THREADS_PER_WORKER") == 0)
                config->threads_per_worker = atoi(value);
            else if (strcmp(key, "DOCUMENT_ROOT") == 0)
                /* Use strncpy to prevent buffer overflows if value is too long */
                strncpy(config->document_root, value, sizeof(config->document_root));
            else if (strcmp(key, "MAX_QUEUE_SIZE") == 0)
                config->max_queue_size = atoi(value);
            else if (strcmp(key, "LOG_FILE") == 0)
                strncpy(config->log_file, value, sizeof(config->log_file));
            else if (strcmp(key, "CACHE_SIZE_MB") == 0)
                config->cache_size_mb = atoi(value);
            else if (strcmp(key, "TIMEOUT_SECONDS") == 0)
                config->timeout_seconds = atoi(value);
            else if (strcmp(key, "KEEP_ALIVE_TIMEOUT") == 0)
                config->keep_alive_timeout = atoi(value);
        }
    }
    fclose(fp);
    return 0;
}

void parse_env_vars(server_config_t *config) {
    char *val;

    if ((val = getenv("HTTP_PORT"))) config->port = atoi(val);
    if ((val = getenv("HTTP_WORKERS"))) config->num_workers = atoi(val);
    if ((val = getenv("HTTP_THREADS"))) config->threads_per_worker = atoi(val);
    if ((val = getenv("HTTP_ROOT"))) {
        strncpy(config->document_root, val, sizeof(config->document_root) - 1);
        config->document_root[sizeof(config->document_root) - 1] = '\0';
    }
    if ((val = getenv("HTTP_QUEUE"))) config->max_queue_size = atoi(val);
    if ((val = getenv("HTTP_CACHE"))) config->cache_size_mb = atoi(val);
    if ((val = getenv("HTTP_LOG"))) {
        strncpy(config->log_file, val, sizeof(config->log_file) - 1);
        config->log_file[sizeof(config->log_file) - 1] = '\0';
    }
    if ((val = getenv("HTTP_TIMEOUT"))) config->timeout_seconds = atoi(val);
}