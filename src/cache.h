#ifndef CACHE_H
#define CACHE_H

#include <pthread.h>
#include <stddef.h>
#include <time.h>

#define CACHE_MAX_ENTRIES 1000
#define CACHE_MAX_FILE_SIZE (1024 * 1024) // 1MB
#define HASH_TABLE_SIZE 1024

typedef struct cache_entry_t {
    char *key;                    // File path
    void *data;                   // File content
    size_t size;                  // Size in bytes
    time_t timestamp;             // Last access time
    struct cache_entry_t *prev;   // LRU doubly-linked list
    struct cache_entry_t *next;   // LRU doubly-linked list
    struct cache_entry_t *next_hash; // Hash collision chain
    int ref_count;                // Reference count for thread safety
} cache_entry_t;

typedef struct {
    cache_entry_t *head;          // Most recently used
    cache_entry_t *tail;          // Least recently used
    cache_entry_t **hash_table;   // Hash table for O(1) lookup
    size_t max_size;              // Maximum cache size in bytes
    size_t current_size;          // Current cache size in bytes
    int max_entries;              // Maximum number of entries
    int current_entries;          // Current number of entries
    pthread_mutex_t lock;         // Mutex for thread safety
    pthread_mutex_t eviction_lock;// Lock for eviction
} cache_t;

// Cache operations
cache_t *cache_create(size_t max_size_mb, int max_entries);
void cache_destroy(cache_t *cache);

cache_entry_t *cache_get(cache_t *cache, const char *key);
int cache_put(cache_t *cache, const char *key, const void *data, size_t size);
int cache_remove(cache_t *cache, const char *key);
void cache_invalidate(cache_t *cache);

// Statistics
void cache_print_stats(const cache_t *cache);
size_t cache_get_hit_count(void);
size_t cache_get_miss_count(void);
double cache_get_hit_ratio(void);

// Helper functions
unsigned int cache_hash(const char *key);
void cache_entry_release(cache_entry_t *entry);

#endif // CACHE_H