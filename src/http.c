// http.c - ATUALIZADO para usar cache
#include "http.h"
#include "cache.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/sendfile.h>

// Cache global (uma por worker process)
static lru_cache_t* file_cache = NULL;

// Função para obter a cache (cria se não existir)
lru_cache_t* get_file_cache() {
    if (!file_cache) {
        file_cache = cache_create(MAX_CACHE_SIZE);
    }
    return file_cache;
}

// Versão atualizada de send_file_response que usa cache
void send_file_response(int fd, const char* file_path) {
    lru_cache_t* cache = get_file_cache();
    char* file_content = NULL;
    size_t file_size = 0;
    
    // Tenta obter da cache primeiro
    if (cache_get(cache, file_path, &file_content, &file_size)) {
        // Cache hit - serve da memória
        const char* mime_type = get_mime_type(file_path);
        send_http_response(fd, 200, "OK", mime_type, file_content, file_size);
        free(file_content);  // Liberta cópia do conteúdo
        return;
    }
    
    // Cache miss - lê do disco
    FILE* file = fopen(file_path, "rb");
    if (!file) {
        send_error_response(fd, 404);
        return;
    }
    
    // Obtém tamanho do ficheiro
    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Lê conteúdo do ficheiro
    file_content = malloc(file_size);
    if (!file_content) {
        fclose(file);
        send_error_response(fd, 500);
        return;
    }
    
    fread(file_content, 1, file_size, file);
    fclose(file);
    
    // Adiciona à cache (se for pequeno o suficiente)
    if (file_size <= MAX_FILE_SIZE) {
        cache_put(cache, file_path, file_content, file_size);
    }
    
    // Serve o ficheiro
    const char* mime_type = get_mime_type(file_path);
    send_http_response(fd, 200, "OK", mime_type, file_content, file_size);
    
    free(file_content);
}