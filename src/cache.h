// cache.h
// Inês Batista, Maria Quinteiro

// Define o sistema de cache LRU (Least Recently Used) para ficheiros.
// Melhora performance ao manter ficheiros frequentemente acedidos em memória.
// Usa reader-writer locks para permitir acesso concorrente de leitura.

#ifndef CACHE_H
#define CACHE_H

#include <pthread.h>
#include <sys/stat.h>

// Tamanho máximo da cache por worker (10MB conforme especificação)
#define MAX_CACHE_SIZE (10 * 1024 * 1024)  // 10MB
#define MAX_FILE_SIZE (1 * 1024 * 1024)    // 1MB - ficheiros maiores não são cacheados

// Estrutura para uma entrada na cache
typedef struct cache_entry {
    char* file_path;           // Caminho do ficheiro (chave)
    char* file_content;        // Conteúdo do ficheiro em memória
    size_t file_size;          // Tamanho do ficheiro em bytes
    time_t last_access;        // Timestamp do último acesso (para LRU)
    struct cache_entry* prev;  // Ponteiro para entrada anterior (lista duplamente ligada)
    struct cache_entry* next;  // Ponteiro para próxima entrada (lista duplamente ligada)
} cache_entry_t;

// Estrutura principal da cache LRU
typedef struct {
    cache_entry_t* head;           // Entrada mais recentemente usada
    cache_entry_t* tail;           // Entrada menos recentemente usada
    pthread_rwlock_t rwlock;       // Reader-writer lock para acesso concorrente
    size_t max_size;               // Tamanho máximo da cache em bytes
    size_t current_size;           // Tamanho atual da cache em bytes
    unsigned long hits;            // Estatística: cache hits
    unsigned long misses;          // Estatística: cache misses
} lru_cache_t;

// Funções da cache
lru_cache_t* cache_create(size_t max_size);
void cache_destroy(lru_cache_t* cache);
int cache_get(lru_cache_t* cache, const char* file_path, char** content, size_t* size);
int cache_put(lru_cache_t* cache, const char* file_path, const char* content, size_t size);
void cache_remove(lru_cache_t* cache, const char* file_path);
void cache_clear(lru_cache_t* cache);
void cache_print_stats(lru_cache_t* cache);

// Funções internas (não expostas no header normalmente)
void cache_remove_lru(lru_cache_t* cache);
void cache_move_to_head(lru_cache_t* cache, cache_entry_t* entry);

#endif