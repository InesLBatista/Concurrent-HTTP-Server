#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

// Statistics
static size_t cache_hits = 0;
static size_t cache_misses = 0;

// Hash function
unsigned int cache_hash(const char *key) {
    unsigned int hash = 5381;
    int c;
    
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    
    return hash % HASH_TABLE_SIZE;
}

// Create a new cache entry
static cache_entry_t *cache_entry_create(const char *key, const void *data, size_t size) {
    cache_entry_t *entry = malloc(sizeof(cache_entry_t));
    if (!entry) {
        return NULL;
    }
    
    entry->key = strdup(key);
    if (!entry->key) {
        free(entry);
        return NULL;
    }
    
    entry->data = malloc(size);
    if (!entry->data) {
        free(entry->key);
        free(entry);
        return NULL;
    }
    
    memcpy(entry->data, data, size);
    entry->size = size;
    entry->timestamp = time(NULL);
    entry->prev = NULL;
    entry->next = NULL;
    entry->next_hash = NULL;
    entry->ref_count = 1;
    
    return entry;
}

// Destroy a cache entry
static void cache_entry_destroy(cache_entry_t *entry) {
    if (entry) {
        free(entry->key);
        free(entry->data);
        free(entry);
    }
}

// Release reference to cache entry
void cache_entry_release(cache_entry_t *entry) {
    if (entry && --entry->ref_count == 0) {
        cache_entry_destroy(entry);
    }
}

// Move entry to head of LRU list
static void cache_move_to_head(cache_t *cache, cache_entry_t *entry) {
    if (entry == cache->head) {
        entry->timestamp = time(NULL);
        return; // Already at head
    }
    
    // Remove from current position
    if (entry->prev) {
        entry->prev->next = entry->next;
    }
    if (entry->next) {
        entry->next->prev = entry->prev;
    }
    
    // If entry was tail, update tail
    if (entry == cache->tail) {
        cache->tail = entry->prev;
    }
    
    // Move to head
    entry->prev = NULL;
    entry->next = cache->head;
    
    if (cache->head) {
        cache->head->prev = entry;
    }
    
    cache->head = entry;
    
    // If list was empty, set tail
    if (!cache->tail) {
        cache->tail = entry;
    }
    
    entry->timestamp = time(NULL);
}

// Evict least recently used entry
static void cache_evict_lru(cache_t *cache) {
    if (!cache->tail) {
        return;
    }
    
    cache_entry_t *to_evict = cache->tail;
    
    // Remove from hash table
    unsigned int hash = cache_hash(to_evict->key);
    cache_entry_t **entry_ptr = &cache->hash_table[hash];
    
    while (*entry_ptr) {
        if (*entry_ptr == to_evict) {
            *entry_ptr = to_evict->next_hash;
            break;
        }
        entry_ptr = &(*entry_ptr)->next_hash;
    }
    
    // Remove from LRU list
    cache->tail = to_evict->prev;
    if (cache->tail) {
        cache->tail->next = NULL;
    } else {
        cache->head = NULL;
    }
    
    // Update cache statistics
    cache->current_size -= to_evict->size;
    cache->current_entries--;
    
    // Free the entry
    cache_entry_destroy(to_evict);
}

// Create a new cache
cache_t *cache_create(size_t max_size_mb, int max_entries) {
    cache_t *cache = malloc(sizeof(cache_t));
    if (!cache) {
        return NULL;
    }
    
    memset(cache, 0, sizeof(cache_t));
    
    cache->hash_table = calloc(HASH_TABLE_SIZE, sizeof(cache_entry_t *));
    if (!cache->hash_table) {
        free(cache);
        return NULL;
    }
    
    cache->max_size = max_size_mb * 1024 * 1024;
    cache->max_entries = max_entries;
    cache->current_size = 0;
    cache->current_entries = 0;
    cache->head = NULL;
    cache->tail = NULL;
    
    // Initialize mutexes
    if (pthread_mutex_init(&cache->lock, NULL) != 0) {
        free(cache->hash_table);
        free(cache);
        return NULL;
    }
    
    if (pthread_mutex_init(&cache->eviction_lock, NULL) != 0) {
        pthread_mutex_destroy(&cache->lock);
        free(cache->hash_table);
        free(cache);
        return NULL;
    }
    
    printf("Cache created: %zu MB max, %d entries max\n", 
           max_size_mb, max_entries);
    
    return cache;
}

// Destroy a cache
void cache_destroy(cache_t *cache) {
    if (!cache) {
        return;
    }
    
    // Invalidate all entries
    cache_invalidate(cache);
    
    // Destroy mutexes
    pthread_mutex_destroy(&cache->lock);
    pthread_mutex_destroy(&cache->eviction_lock);
    
    // Free hash table
    free(cache->hash_table);
    
    // Free cache structure
    free(cache);
    
    printf("Cache destroyed\n");
}

// Get an entry from cache
cache_entry_t *cache_get(cache_t *cache, const char *key) {
    if (!cache || !key) {
        cache_misses++;
        return NULL;
    }
    
    pthread_mutex_lock(&cache->lock);
    
    unsigned int hash = cache_hash(key);
    cache_entry_t *entry = cache->hash_table[hash];
    
    // Search in hash chain
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            // Found entry - move to head and update stats
            cache_move_to_head(cache, entry);
            entry->ref_count++;
            cache_hits++;
            
            pthread_mutex_unlock(&cache->lock);
            return entry;
        }
        entry = entry->next_hash;
    }
    
    pthread_mutex_unlock(&cache->lock);
    cache_misses++;
    return NULL;
}

// Put an entry into cache
int cache_put(cache_t *cache, const char *key, const void *data, size_t size) {
    if (!cache || !key || !data || size == 0 || size > CACHE_MAX_FILE_SIZE) {
        return -1;
    }
    
    pthread_mutex_lock(&cache->lock);
    
    // Check if entry already exists
    unsigned int hash = cache_hash(key);
    cache_entry_t *entry = cache->hash_table[hash];
    cache_entry_t *prev = NULL;
    
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            // Update existing entry
            if (entry->size != size) {
                void *new_data = realloc(entry->data, size);
                if (!new_data) {
                    pthread_mutex_unlock(&cache->lock);
                    return -1;
                }
                entry->data = new_data;
                cache->current_size -= entry->size;
                cache->current_size += size;
            }
            
            memcpy(entry->data, data, size);
            entry->size = size;
            cache_move_to_head(cache, entry);
            
            pthread_mutex_unlock(&cache->lock);
            return 0;
        }
        prev = entry;
        entry = entry->next_hash;
    }
    
    // Check if we need to evict entries
    while ((cache->current_size + size > cache->max_size) || 
           (cache->current_entries >= cache->max_entries)) {
        if (cache->current_entries == 0) {
            // No entries to evict, but still too large
            pthread_mutex_unlock(&cache->lock);
            return -1;
        }
        cache_evict_lru(cache);
    }
    
    // Create new entry
    entry = cache_entry_create(key, data, size);
    if (!entry) {
        pthread_mutex_unlock(&cache->lock);
        return -1;
    }
    
    // Add to hash table
    if (prev) {
        prev->next_hash = entry;
    } else {
        cache->hash_table[hash] = entry;
    }
    
    // Add to LRU list head
    entry->next = cache->head;
    if (cache->head) {
        cache->head->prev = entry;
    }
    cache->head = entry;
    
    if (!cache->tail) {
        cache->tail = entry;
    }
    
    // Update cache statistics
    cache->current_size += size;
    cache->current_entries++;
    
    pthread_mutex_unlock(&cache->lock);
    return 0;
}

// Remove an entry from cache
int cache_remove(cache_t *cache, const char *key) {
    if (!cache || !key) {
        return -1;
    }
    
    pthread_mutex_lock(&cache->lock);
    
    unsigned int hash = cache_hash(key);
    cache_entry_t **entry_ptr = &cache->hash_table[hash];
    cache_entry_t *entry = *entry_ptr;
    cache_entry_t *prev_hash = NULL;
    
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            // Remove from hash table
            if (prev_hash) {
                prev_hash->next_hash = entry->next_hash;
            } else {
                *entry_ptr = entry->next_hash;
            }
            
            // Remove from LRU list
            if (entry->prev) {
                entry->prev->next = entry->next;
            } else {
                cache->head = entry->next;
            }
            
            if (entry->next) {
                entry->next->prev = entry->prev;
            } else {
                cache->tail = entry->prev;
            }
            
            // Update cache statistics
            cache->current_size -= entry->size;
            cache->current_entries--;
            
            // Free entry if no references
            if (--entry->ref_count == 0) {
                cache_entry_destroy(entry);
            } else {
                // Still referenced elsewhere, just disconnect
                entry->next = NULL;
                entry->prev = NULL;
                entry->next_hash = NULL;
            }
            
            pthread_mutex_unlock(&cache->lock);
            return 0;
        }
        
        prev_hash = entry;
        entry = entry->next_hash;
    }
    
    pthread_mutex_unlock(&cache->lock);
    return -1;
}

// Invalidate all cache entries
void cache_invalidate(cache_t *cache) {
    if (!cache) {
        return;
    }
    
    pthread_mutex_lock(&cache->lock);
    
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        cache_entry_t *entry = cache->hash_table[i];
        while (entry) {
            cache_entry_t *next = entry->next_hash;
            cache_entry_destroy(entry);
            entry = next;
        }
        cache->hash_table[i] = NULL;
    }
    
    cache->head = NULL;
    cache->tail = NULL;
    cache->current_size = 0;
    cache->current_entries = 0;
    
    pthread_mutex_unlock(&cache->lock);
    
    printf("Cache invalidated\n");
}

// Print cache statistics
void cache_print_stats(const cache_t *cache) {
    if (!cache) {
        return;
    }
    
    pthread_mutex_lock(&cache->lock);
    
    printf("\n=== Cache Statistics ===\n");
    printf("Max Size: %.2f MB\n", cache->max_size / (1024.0 * 1024.0));
    printf("Current Size: %.2f MB\n", cache->current_size / (1024.0 * 1024.0));
    printf("Max Entries: %d\n", cache->max_entries);
    printf("Current Entries: %d\n", cache->current_entries);
    printf("Cache Hits: %zu\n", cache_hits);
    printf("Cache Misses: %zu\n", cache_misses);
    
    size_t total = cache_hits + cache_misses;
    if (total > 0) {
        printf("Hit Ratio: %.2f%%\n", (cache_hits * 100.0) / total);
    } else {
        printf("Hit Ratio: 0.00%%\n");
    }
        
    pthread_mutex_unlock(&cache->lock);
}

// Get cache hit count
size_t cache_get_hit_count(void) {
    return cache_hits;
}

// Get cache miss count
size_t cache_get_miss_count(void) {
    return cache_misses;
}

// Get cache hit ratio
double cache_get_hit_ratio(void) {
    size_t total = cache_hits + cache_misses;
    if (total == 0) {
        return 0.0;
    }
    return (cache_hits * 100.0) / total;
}