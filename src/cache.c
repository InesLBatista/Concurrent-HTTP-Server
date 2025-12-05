#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

// Statistics
static size_t cache_hits = 0;
static size_t cache_misses = 0;

// Hash function (mantém igual)
unsigned int cache_hash(const char *key) {
    unsigned int hash = 5381;
    int c;
    
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    
    return hash % HASH_TABLE_SIZE;
}

// Create a new cache entry (mantém igual)
static cache_entry_t *cache_entry_create(const char *key, const void *data, size_t size) {
    // ... (código existente mantido)
}

// Destroy a cache entry (mantém igual)
static void cache_entry_destroy(cache_entry_t *entry) {
    // ... (código existente mantido)
}

// Release reference to cache entry (mantém igual)
void cache_entry_release(cache_entry_t *entry) {
    // ... (código existente mantido)
}

// Move entry to head of LRU list (mantém igual, mas atualiza lock)
static void cache_move_to_head(cache_t *cache, cache_entry_t *entry) {
    // ... (código existente mantido)
}

// Evict least recently used entry (mantém igual, mas atualiza lock)
static void cache_evict_lru(cache_t *cache) {
    // ... (código existente mantido)
}

// Create a new cache (MODIFICADO para usar rwlock)
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
    
    // MODIFICAÇÃO: Inicializar reader-writer lock em vez de mutex simples
    if (pthread_rwlock_init(&cache->rwlock, NULL) != 0) {
        free(cache->hash_table);
        free(cache);
        return NULL;
    }
    
    if (pthread_mutex_init(&cache->eviction_lock, NULL) != 0) {
        pthread_rwlock_destroy(&cache->rwlock);
        free(cache->hash_table);
        free(cache);
        return NULL;
    }
    
    printf("Cache created: %zu MB max, %d entries max (using reader-writer locks)\n", 
           max_size_mb, max_entries);
    
    return cache;
}

// Destroy a cache (MODIFICADO para usar rwlock)
void cache_destroy(cache_t *cache) {
    if (!cache) {
        return;
    }
    
    // Invalidate all entries
    cache_invalidate(cache);
    
    // Destroy locks
    pthread_rwlock_destroy(&cache->rwlock);
    pthread_mutex_destroy(&cache->eviction_lock);
    
    // Free hash table
    free(cache->hash_table);
    
    // Free cache structure
    free(cache);
    
    printf("Cache destroyed\n");
}

// NOVA FUNÇÃO: Get an entry from cache with READER lock (múltiplos leitores)
cache_entry_t *cache_get_read(cache_t *cache, const char *key) {
    if (!cache || !key) {
        cache_misses++;
        return NULL;
    }
    
    // MODIFICAÇÃO: Usar READ lock (múltiplas threads podem ler simultaneamente)
    pthread_rwlock_rdlock(&cache->rwlock);
    
    unsigned int hash = cache_hash(key);
    cache_entry_t *entry = cache->hash_table[hash];
    
    // Search in hash chain
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            // Found entry - move to head and update stats
            cache_move_to_head(cache, entry);
            entry->ref_count++;
            cache_hits++;
            
            // Para mover para a frente, precisamos de WRITE lock
            pthread_rwlock_unlock(&cache->rwlock);
            
            // Obter WRITE lock para atualizar posição LRU
            pthread_rwlock_wrlock(&cache->rwlock);
            cache_move_to_head(cache, entry);
            pthread_rwlock_unlock(&cache->rwlock);
            
            return entry;
        }
        entry = entry->next_hash;
    }
    
    pthread_rwlock_unlock(&cache->rwlock);
    cache_misses++;
    return NULL;
}

// NOVA FUNÇÃO: Get an entry from cache with WRITER lock (para modificações)
cache_entry_t *cache_get_write(cache_t *cache, const char *key) {
    if (!cache || !key) {
        cache_misses++;
        return NULL;
    }
    
    // MODIFICAÇÃO: Usar WRITE lock (acesso exclusivo)
    pthread_rwlock_wrlock(&cache->rwlock);
    
    unsigned int hash = cache_hash(key);
    cache_entry_t *entry = cache->hash_table[hash];
    
    // Search in hash chain
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            // Found entry - move to head and update stats
            cache_move_to_head(cache, entry);
            entry->ref_count++;
            cache_hits++;
            
            pthread_rwlock_unlock(&cache->rwlock);
            return entry;
        }
        entry = entry->next_hash;
    }
    
    pthread_rwlock_unlock(&cache->rwlock);
    cache_misses++;
    return NULL;
}

// Put an entry into cache (MODIFICADO para usar rwlock)
int cache_put(cache_t *cache, const char *key, const void *data, size_t size) {
    if (!cache || !key || !data || size == 0 || size > CACHE_MAX_FILE_SIZE) {
        return -1;
    }
    
    // MODIFICAÇÃO: Usar WRITE lock (modificação do cache)
    pthread_rwlock_wrlock(&cache->rwlock);
    
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
                    pthread_rwlock_unlock(&cache->rwlock);
                    return -1;
                }
                entry->data = new_data;
                cache->current_size -= entry->size;
                cache->current_size += size;
            }
            
            memcpy(entry->data, data, size);
            entry->size = size;
            cache_move_to_head(cache, entry);
            
            pthread_rwlock_unlock(&cache->rwlock);
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
            pthread_rwlock_unlock(&cache->rwlock);
            return -1;
        }
        cache_evict_lru(cache);
    }
    
    // Create new entry
    entry = cache_entry_create(key, data, size);
    if (!entry) {
        pthread_rwlock_unlock(&cache->rwlock);
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
    
    pthread_rwlock_unlock(&cache->rwlock);
    return 0;
}

// Remove an entry from cache (MODIFICADO para usar rwlock)
int cache_remove(cache_t *cache, const char *key) {
    if (!cache || !key) {
        return -1;
    }
    
    // MODIFICAÇÃO: Usar WRITE lock
    pthread_rwlock_wrlock(&cache->rwlock);
    
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
            
            pthread_rwlock_unlock(&cache->rwlock);
            return 0;
        }
        
        prev_hash = entry;
        entry = entry->next_hash;
    }
    
    pthread_rwlock_unlock(&cache->rwlock);
    return -1;
}

// Invalidate all cache entries (MODIFICADO para usar rwlock)
void cache_invalidate(cache_t *cache) {
    if (!cache) {
        return;
    }
    
    // MODIFICAÇÃO: Usar WRITE lock
    pthread_rwlock_wrlock(&cache->rwlock);
    
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
    
    pthread_rwlock_unlock(&cache->rwlock);
    
    printf("Cache invalidated\n");
}

// Print cache statistics (MODIFICADO para usar READ lock)
void cache_print_stats(const cache_t *cache) {
    if (!cache) {
        return;
    }
    
    // MODIFICAÇÃO: Usar READ lock (apenas leitura)
    pthread_rwlock_rdlock(&cache->rwlock);
    
    printf("\n=== Cache Statistics (Thread-Safe) ===\n");
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
        
    pthread_rwlock_unlock(&cache->rwlock);
}