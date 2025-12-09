#define _XOPEN_SOURCE 700
#include "cache.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <bits/pthreadtypes.h>

/* * Global Cache State
 * Protected by cache_lock for thread safety.
 * Implements an LRU (Least Recently Used) policy using a doubly-linked list
 * combined with a hash table for O(1) lookups.
 */
static cache_node_t **htable = NULL;    /* Hash table buckets */
static size_t hsize = 0;                /* Number of buckets */
static cache_node_t *head = NULL;       /* MRU (Most Recently Used) end of list */
static cache_node_t *tail = NULL;       /* LRU (Least Recently Used) end of list */
static size_t current_size = 0;         /* Current total size of cached data in bytes */
static size_t max_size = 0;             /* Max allowed cache size in bytes */
static pthread_rwlock_t cache_lock = PTHREAD_RWLOCK_INITIALIZER;

/*
 * Hash function (djb2 algorithm)
 * Purpose: Generates a hash for a string path to map it to a table index.
 * Parameters:
 * - s: The null-terminated string to hash.
 * Return: The calculated hash value.
 */
static unsigned long hash_str(const char *s)
{
    unsigned long h = 5381;
    int c;
    while ((c = *s++)) h = ((h << 5) + h) + (unsigned long)c;
    return h;
}

/*
 * Helper to allocate the internal hash table.
 * Purpose: Ensures the hash table memory is allocated if not already present.
 * Parameters:
 * - size: Number of buckets to allocate.
 * Return: 0 on success, -1 on allocation failure.
 */
static int ensure_table(size_t size)
{
    if (htable) return 0;
    hsize = size;
    htable = calloc(hsize, sizeof(cache_node_t *));
    return htable ? 0 : -1;
}

/*
 * Initialize the cache system.
 * Purpose: Sets up the hash table, locks, and size limits.
 * Parameters:
 * - max_size_bytes: The maximum total size (in bytes) the cache can hold.
 * Return: 0 on success, -1 on failure.
 */
int cache_init(size_t max_size_bytes)
{
    max_size = max_size_bytes;
    current_size = 0;
    head = tail = NULL;
    if (pthread_rwlock_init(&cache_lock, NULL) != 0) return -1;
    return ensure_table(4096);
}

/*
 * Destroy the cache system.
 * Purpose: Frees all memory associated with cache nodes, data buffers, 
 * and the hash table itself. Destroys the lock.
 * Synchronization: Acquires write lock to ensure no other thread accesses memory while freeing.
 */
void cache_destroy()
{
    if (!htable) return;
    pthread_rwlock_wrlock(&cache_lock);
    for (size_t i = 0; i < hsize; i++) {
        cache_node_t *n = htable[i];
        while (n) {
            cache_node_t *next = n->hnext;
            free(n->path);
            free(n->data);
            free(n);
            n = next;
        }
        htable[i] = NULL;
    }
    free(htable);
    htable = NULL;
    head = tail = NULL;
    current_size = 0;
    pthread_rwlock_unlock(&cache_lock);
    pthread_rwlock_destroy(&cache_lock);
}

/*
 * Internal list helper.
 * Purpose: Unlinks a node from the doubly-linked LRU list.
 * Note: Caller must hold the write lock.
 */
static void remove_from_list(cache_node_t *n)
{
    if (!n) return;
    if (n->prev) n->prev->next = n->next; else head = n->next;
    if (n->next) n->next->prev = n->prev; else tail = n->prev;
    n->prev = n->next = NULL;
}

/*
 * Internal list helper.
 * Purpose: Inserts a node at the front (Head) of the list, making it the MRU node.
 * Note: Caller must hold the write lock.
 */
static void insert_at_head(cache_node_t *n)
{
    n->prev = NULL;
    n->next = head;
    if (head) head->prev = n;
    head = n;
    if (!tail) tail = n;
}

/*
 * LRU Eviction Logic.
 * Purpose: Removes nodes from the tail (Least Recently Used) until the total cache size 
 * is within limits.
 * Note: Caller must hold the write lock.
 */
static void evict_if_needed()
{
    while (current_size > max_size && tail) {
        cache_node_t *n = tail;
        
        /* Find and remove from hash chain */
        unsigned long h = hash_str(n->path) % hsize;
        cache_node_t *prev = NULL;
        cache_node_t *iter = htable[h];
        while (iter) {
            if (iter == n) {
                if (prev) prev->hnext = iter->hnext; else htable[h] = iter->hnext;
                break;
            }
            prev = iter; iter = iter->hnext;
        }

        remove_from_list(n);
        current_size -= n->len;
        
        free(n->path);
        free(n->data);
        free(n);
    }
}

/*
 * Retrieve data from the cache.
 * Purpose: Looks up a file by path. If found, returns a copy of the data and promotes the 
 * node to the head of the list (MRU).
 * Parameters:
 * - path: The file path key.
 * - out_buf: Pointer to store the address of the allocated data copy.
 * - out_len: Pointer to store the size of the data.
 * Return: 0 on success (hit), -1 on failure (miss).
 * Synchronization: 
 * 1. Acquires Read Lock to check existence.
 * 2. If found, upgrades to Write Lock to update LRU position (move to head).
 */
int cache_get(const char *path, char **out_buf, size_t *out_len)
{
    if (!htable) return -1;
    unsigned long h = hash_str(path) % hsize;

    /* Optimistic read: acquire read lock first */
    if (pthread_rwlock_rdlock(&cache_lock) != 0) return -1;
    cache_node_t *n = htable[h];
    while (n) {
        if (strcmp(n->path, path) == 0) break;
        n = n->hnext;
    }
    if (!n) {
        pthread_rwlock_unlock(&cache_lock);
        return -1; /* Cache miss */
    }

    /* * Cache hit: We need to modify the list order (promote to head).
     * We must release the read lock and acquire the write lock.
     */
    pthread_rwlock_unlock(&cache_lock);

    if (pthread_rwlock_wrlock(&cache_lock) != 0) return -1;

    /* Re-verify availability after re-locking (race condition check) */
    cache_node_t *n2 = htable[h];
    while (n2) {
        if (strcmp(n2->path, path) == 0) break;
        n2 = n2->hnext;
    }
    if (!n2) {
        pthread_rwlock_unlock(&cache_lock);
        return -1;
    }

    /* Move to MRU position */
    remove_from_list(n2);
    insert_at_head(n2);

    /* Return a deep copy so the caller owns the memory */
    char *buf = malloc(n2->len);
    if (!buf) {
        pthread_rwlock_unlock(&cache_lock);
        return -1;
    }
    memcpy(buf, n2->data, n2->len);
    *out_buf = buf;
    *out_len = n2->len;
    
    pthread_rwlock_unlock(&cache_lock);
    return 0;
}

/*
 * Insert or update data in the cache.
 * Purpose: Adds new data or updates existing data for a path. Handles LRU eviction
 * if the cache exceeds the size limit.
 * Parameters:
 * - path: The file path key.
 * - buf: Data to cache.
 * - len: Length of the data.
 * Return: 0 on success, -1 on failure.
 * Synchronization: Acquires Write Lock immediately since this modifies the structure.
 */
int cache_put(const char *path, const char *buf, size_t len)
{
    if (!htable) return -1;
    if (len == 0 || !buf) return -1;
    
    /* Enforce hard limit for single file size (1MB) */
    if (len > (1 * 1024 * 1024)) return -1;

    if (pthread_rwlock_wrlock(&cache_lock) != 0) return -1;
    unsigned long h = hash_str(path) % hsize;

    /* Check if node already exists */
    cache_node_t *n = htable[h];
    while (n) {
        if (strcmp(n->path, path) == 0) break;
        n = n->hnext;
    }

    if (n) {
        /* Node exists: Update data and promote to head */
        current_size -= n->len;
        free(n->data);
        n->data = malloc(len);
        if (!n->data) {
            pthread_rwlock_unlock(&cache_lock);
            return -1;
        }
        memcpy(n->data, buf, len);
        n->len = len;
        current_size += len;
        
        remove_from_list(n);
        insert_at_head(n);
        evict_if_needed();
        
        pthread_rwlock_unlock(&cache_lock);
        return 0;
    }

    /* Create new node */
    cache_node_t *node = malloc(sizeof(cache_node_t));
    if (!node) { pthread_rwlock_unlock(&cache_lock); return -1; }
    
    node->path = strdup(path);
    node->data = malloc(len);
    if (!node->path || !node->data) {
        free(node->path); free(node->data); free(node);
        pthread_rwlock_unlock(&cache_lock);
        return -1;
    }
    
    memcpy(node->data, buf, len);
    node->len = len;
    
    /* Setup links */
    node->prev = node->next = NULL;
    node->hnext = htable[h];
    htable[h] = node;
    
    insert_at_head(node);
    current_size += len;
    evict_if_needed();
    
    pthread_rwlock_unlock(&cache_lock);
    return 0;
}