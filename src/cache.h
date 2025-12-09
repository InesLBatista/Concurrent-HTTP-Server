#ifndef CACHE_H
#define CACHE_H

#include <stddef.h>

typedef struct cache_node {
    char *path;
    char *data;
    size_t len;
    struct cache_node *prev, *next;
    struct cache_node *hnext; 
} cache_node_t;

int cache_init(size_t max_size_bytes);
void cache_destroy();

int cache_get(const char *path, char **out_buf, size_t *out_len);

int cache_put(const char *path, const char *buf, size_t len);

#endif
