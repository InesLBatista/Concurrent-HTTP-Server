// cache.c
// Inês Batista, Maria Quinteiro

// Implementa o sistema de cache LRU com reader-writer locks.
// Permite múltiplas leituras concorrentes enquanto garante exclusão mútua para escritas.

#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

// Cria e inicializa uma nova cache LRU
// Max_size: tamanho máximo da cache em bytes
// Retorna: ponteiro para cache ou NULL em erro
lru_cache_t* cache_create(size_t max_size) {
    lru_cache_t* cache = malloc(sizeof(lru_cache_t));
    if (!cache) {
        perror("Failed to allocate cache");
        return NULL;
    }
    
    // Inicializa campos
    cache->head = NULL;
    cache->tail = NULL;
    cache->max_size = max_size;
    cache->current_size = 0;
    cache->hits = 0;
    cache->misses = 0;
    
    // Inicializa reader-writer lock
    if (pthread_rwlock_init(&cache->rwlock, NULL) != 0) {
        perror("Failed to initialize cache rwlock");
        free(cache);
        return NULL;
    }
    
    printf("Created LRU cache with max size: %zu bytes\n", max_size);
    return cache;
}

// Destrói a cache e liberta todos os recursos
// Cache: cache a destruir
void cache_destroy(lru_cache_t* cache) {
    if (!cache) return;
    
    // Limpa todas as entradas
    cache_clear(cache);
    
    // Destrói o lock
    pthread_rwlock_destroy(&cache->rwlock);
    
    free(cache);
    printf("Cache destroyed\n");
}

// Obtém um ficheiro da cache (operação de leitura)
// Cache: cache onde procurar
// File_path: caminho do ficheiro a obter
// Content: ponteiro para guardar o conteúdo (alocado dinamicamente)
// Size: ponteiro para guardar o tamanho
// Retorna: 1 se encontrado (hit), 0 se não encontrado (miss)
int cache_get(lru_cache_t* cache, const char* file_path, char** content, size_t* size) {
    if (!cache || !file_path || !content || !size) return 0;
    
    // Adquire read lock (múltiplas threads podem ler simultaneamente)
    pthread_rwlock_rdlock(&cache->rwlock);
    
    // Procura a entrada na cache
    cache_entry_t* current = cache->head;
    while (current != NULL) {
        if (strcmp(current->file_path, file_path) == 0) {
            // Cache hit - encontrou o ficheiro
            *content = malloc(current->file_size);
            if (*content) {
                memcpy(*content, current->file_content, current->file_size);
                *size = current->file_size;
                current->last_access = time(NULL);  // Atualiza timestamp de acesso
                
                // Move para head (mais recente) - precisa de write lock
                pthread_rwlock_unlock(&cache->rwlock);
                pthread_rwlock_wrlock(&cache->rwlock);
                cache_move_to_head(cache, current);
                pthread_rwlock_unlock(&cache->rwlock);
                
                cache->hits++;
                printf("Cache HIT: %s (%zu bytes)\n", file_path, *size);
                return 1;
            }
            break;
        }
        current = current->next;
    }
    
    // Cache miss - não encontrado
    pthread_rwlock_unlock(&cache->rwlock);
    cache->misses++;
    printf("Cache MISS: %s\n", file_path);
    return 0;
}

// Adiciona um ficheiro à cache (operações de escrita)
// Cache: cache onde adicionar
// File_path: caminho do ficheiro
// Content: conteúdo do ficheiro
// Size: tamanho do conteúdo
// Retorna: 1 em sucesso, 0 em erro
int cache_put(lru_cache_t* cache, const char* file_path, const char* content, size_t size) {
    if (!cache || !file_path || !content || size == 0) return 0;
    
    // Verifica se o ficheiro é muito grande para cache
    if (size > MAX_FILE_SIZE) {
        printf("File too large for cache: %s (%zu bytes)\n", file_path, size);
        return 0;
    }
    
    // Adquire write lock (exclusivo para escritas)
    pthread_rwlock_wrlock(&cache->rwlock);
    
    // Verifica se já existe na cache (atualiza se existir)
    cache_entry_t* current = cache->head;
    while (current != NULL) {
        if (strcmp(current->file_path, file_path) == 0) {
            // Já existe - atualiza conteúdo
            if (current->file_size != size) {
                // Tamanho diferente - realoca memória
                char* new_content = realloc(current->file_content, size);
                if (!new_content) {
                    pthread_rwlock_unlock(&cache->rwlock);
                    return 0;
                }
                current->file_content = new_content;
                cache->current_size = cache->current_size - current->file_size + size;
            }
            
            memcpy(current->file_content, content, size);
            current->file_size = size;
            current->last_access = time(NULL);
            cache_move_to_head(cache, current);
            
            pthread_rwlock_unlock(&cache->rwlock);
            printf("Cache UPDATED: %s (%zu bytes)\n", file_path, size);
            return 1;
        }
        current = current->next;
    }
    
    // Cria nova entrada
    cache_entry_t* new_entry = malloc(sizeof(cache_entry_t));
    if (!new_entry) {
        pthread_rwlock_unlock(&cache->rwlock);
        return 0;
    }
    
    new_entry->file_path = strdup(file_path);
    new_entry->file_content = malloc(size);
    new_entry->file_size = size;
    new_entry->last_access = time(NULL);
    new_entry->prev = NULL;
    new_entry->next = NULL;
    
    if (!new_entry->file_path || !new_entry->file_content) {
        free(new_entry->file_path);
        free(new_entry->file_content);
        free(new_entry);
        pthread_rwlock_unlock(&cache->rwlock);
        return 0;
    }
    
    memcpy(new_entry->file_content, content, size);
    
    // Verifica se precisa de fazer espaço (LRU eviction)
    while (cache->current_size + size > cache->max_size && cache->tail != NULL) {
        cache_remove_lru(cache);
    }
    
    // Adiciona nova entrada ao início
    new_entry->next = cache->head;
    if (cache->head != NULL) {
        cache->head->prev = new_entry;
    }
    cache->head = new_entry;
    
    if (cache->tail == NULL) {
        cache->tail = new_entry;  // Primeira entrada
    }
    
    cache->current_size += size;
    
    pthread_rwlock_unlock(&cache->rwlock);
    printf("Cache ADDED: %s (%zu bytes), cache size: %zu/%zu bytes\n", 
           file_path, size, cache->current_size, cache->max_size);
    return 1;
}

// Remove uma entrada específica da cache
// Cache: cache de onde remover
// File_path: caminho do ficheiro a remover
void cache_remove(lru_cache_t* cache, const char* file_path) {
    if (!cache || !file_path) return;
    
    pthread_rwlock_wrlock(&cache->rwlock);
    
    cache_entry_t* current = cache->head;
    while (current != NULL) {
        if (strcmp(current->file_path, file_path) == 0) {
            // Remove da lista ligada
            if (current->prev) {
                current->prev->next = current->next;
            } else {
                cache->head = current->next;
            }
            
            if (current->next) {
                current->next->prev = current->prev;
            } else {
                cache->tail = current->prev;
            }
            
            // Liberta memória
            cache->current_size -= current->file_size;
            free(current->file_path);
            free(current->file_content);
            free(current);
            
            printf("Cache REMOVED: %s\n", file_path);
            break;
        }
        current = current->next;
    }
    
    pthread_rwlock_unlock(&cache->rwlock);
}

// Remove a entrada menos recentemente usada (LRU)
// Cache: cache de onde remover
void cache_remove_lru(lru_cache_t* cache) {
    if (!cache || !cache->tail) return;
    
    cache_entry_t* lru = cache->tail;
    
    // Atualiza tail
    cache->tail = lru->prev;
    if (cache->tail) {
        cache->tail->next = NULL;
    } else {
        cache->head = NULL;  // Cache ficou vazia
    }
    
    // Liberta memória
    cache->current_size -= lru->file_size;
    printf("Cache EVICTED: %s (%zu bytes) - LRU policy\n", lru->file_path, lru->file_size);
    
    free(lru->file_path);
    free(lru->file_content);
    free(lru);
}

// Move uma entrada para o head (mais recente)
// Cache: cache onde mover
// Entry: entrada a mover
void cache_move_to_head(lru_cache_t* cache, cache_entry_t* entry) {
    if (!cache || !entry || entry == cache->head) return;
    
    // Remove da posição atual
    if (entry->prev) {
        entry->prev->next = entry->next;
    }
    if (entry->next) {
        entry->next->prev = entry->prev;
    }
    
    // Atualiza tail se necessário
    if (entry == cache->tail) {
        cache->tail = entry->prev;
    }
    
    // Move para head
    entry->prev = NULL;
    entry->next = cache->head;
    
    if (cache->head) {
        cache->head->prev = entry;
    }
    cache->head = entry;
    
    if (!cache->tail) {
        cache->tail = entry;
    }
}

// Limpa completamente a cache
// Cache: cache a limpar
void cache_clear(lru_cache_t* cache) {
    if (!cache) return;
    
    pthread_rwlock_wrlock(&cache->rwlock);
    
    cache_entry_t* current = cache->head;
    while (current != NULL) {
        cache_entry_t* next = current->next;
        free(current->file_path);
        free(current->file_content);
        free(current);
        current = next;
    }
    
    cache->head = NULL;
    cache->tail = NULL;
    cache->current_size = 0;
    
    pthread_rwlock_unlock(&cache->rwlock);
    printf("Cache cleared\n");
}

// Mostra estatísticas da cache
// Cache: cache a mostrar estatísticas
void cache_print_stats(lru_cache_t* cache) {
    if (!cache) return;
    
    pthread_rwlock_rdlock(&cache->rwlock);
    
    printf("\n=== CACHE STATISTICS ===\n");
    printf("Current size: %zu/%zu bytes\n", cache->current_size, cache->max_size);
    printf("Entries: ");
    
    int count = 0;
    cache_entry_t* current = cache->head;
    while (current != NULL) {
        count++;
        current = current->next;
    }
    printf("%d\n", count);
    
    printf("Hits: %lu\n", cache->hits);
    printf("Misses: %lu\n", cache->misses);
    
    if (cache->hits + cache->misses > 0) {
        double hit_rate = (double)cache->hits / (cache->hits + cache->misses) * 100;
        printf("Hit rate: %.2f%%\n", hit_rate);
    }
    
    printf("=======================\n");
    
    pthread_rwlock_unlock(&cache->rwlock);
}