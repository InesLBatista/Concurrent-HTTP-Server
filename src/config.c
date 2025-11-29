// Inês Batista, 124877
// Maria Quinteiro, 124996

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Função auxiliar para remover espaços desnecessários para o bom funcionamento do server
static void trim_string(char* str) {
    if (!str) return;
    
    char* start = str;
    char* end = str + strlen(str) - 1;
    
    // Avançar até encontrar primeiro caractere que não seja espaço
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n')) {
        start++;
    }
    
    // Recuar até encontrar último caractere que não seja espaço  
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\n')) {
        end--;
    }
    
    // Mover a parte importante para o início da string
    size_t len = end - start + 1;
    if (start != str) {
        memmove(str, start, len);
    }
    str[len] = '\0';
}

int load_config(const char* filename, server_config_t* config) {
    // Primeiro definimos valores por defeito
    // Estes são usados se o ficheiro não existir ou faltar alguma configuração
    config->port = 8080;
    strcpy(config->document_root, "./www");
    config->num_workers = 4;
    config->threads_per_worker = 10;
    config->max_queue_size = 100;
    strcpy(config->log_file, "access.log");
    config->cache_size_mb = 10;
    config->timeout_seconds = 30;

    // Tentar abrir o ficheiro de configuração
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        // Se o ficheiro não existir, não é erro grave
        // Usamos os valores por defeito e continuamos
        printf("Config file not found, using defaults\n");
        return 0;
    }
    
    char line[512];
    int loaded_settings = 0;

    // Ler o ficheiro linha por linha
    while (fgets(line, sizeof(line), fp)) {
        // Ignorar linhas que são comentários (começam com #)
        if (line[0] == '#' || line[0] == '\n') continue;
        
        char key[128], value[256];
        // Dividir cada linha em duas partes: chave=valor
        if (sscanf(line, "%[^=]=%s", key, value) == 2) {
            // Limpar espaços em branco de ambas as partes
            trim_string(key);
            trim_string(value);
            
            // Verificar qual é a chave e guardar o valor correspondente
            if (strcmp(key, "PORT") == 0) {
                config->port = atoi(value);  // Converter string para número
                loaded_settings++;
            }
            else if (strcmp(key, "DOCUMENT_ROOT") == 0) {
                strncpy(config->document_root, value, sizeof(config->document_root));
                loaded_settings++;
            }
            else if (strcmp(key, "NUM_WORKERS") == 0) {
                config->num_workers = atoi(value);
                loaded_settings++;
            }
            else if (strcmp(key, "THREADS_PER_WORKER") == 0) {
                config->threads_per_worker = atoi(value);
                loaded_settings++;
            }
            else if (strcmp(key, "MAX_QUEUE_SIZE") == 0) {
                config->max_queue_size = atoi(value);
                loaded_settings++;
            }
            else if (strcmp(key, "LOG_FILE") == 0) {
                strncpy(config->log_file, value, sizeof(config->log_file));
                loaded_settings++;
            }
            else if (strcmp(key, "CACHE_SIZE_MB") == 0) {
                config->cache_size_mb = atoi(value);
                loaded_settings++;
            }
            else if (strcmp(key, "TIMEOUT_SECONDS") == 0) {
                config->timeout_seconds = atoi(value);
                loaded_settings++;
            }
            // Se chegar aqui, a chave não é reconhecida (mas ignoramos)
        }
    }
    
    fclose(fp);
    printf("Loaded %d settings from config file\n", loaded_settings);
    return 0;
}