
#ifndef CONFIG_H
#define CONFIG_H

#define MAX_PATH_LEN 256

typedef struct
{
    int port;
    int num_workers;
    int threads_per_worker;
    int max_queue_size;
    char document_root[MAX_PATH_LEN];
    char log_file[MAX_PATH_LEN];
    int cache_size_mb;
    int timeout_seconds;
    int keep_alive_timeout;
} server_config_t;

int load_config(const char *filename, server_config_t *config);

#endif
