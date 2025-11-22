// config.c  
// Inês Batista, Maria Quinteiro

// Implementação do parser de configuração baseado no template fornecido.
// Expande o template para incluir todos os parâmetros necessários do projeto.

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Função do template fornecida - expandida para incluir todos os parâmetros
int load_config(const char* filename, server_config_t* config) {
    // Abre ficheiro de configuração em modo leitura
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        // Retorna erro se ficheiro não existir ou não puder ser aberto
        return -1;
    }
    
    // Buffers para processar cada linha do ficheiro
    char line[512];     // Buffer para linha completa
    char key[128];      // Buffer para chave (lado esquerdo do =)
    char value[256];    // Buffer para valor (lado direito do =)
    
    // Processa cada linha do ficheiro de configuração
    while (fgets(line, sizeof(line), fp)) {
        // Ignora linhas de comentário (começam com #) e linhas vazias
        if (line[0] == '#' || line[0] == '\n') {
            continue;  // Passa para próxima linha
        }
        
        // Divide a linha em chave=valor usando sscanf
        if (sscanf(line, "%[^=]=%s", key, value) == 2) {
            // Compara a chave com cada parâmetro possível e atribui o valor
            if (strcmp(key, "PORT") == 0) {
                config->port = atoi(value);  // Converte string para inteiro
            }
            else if (strcmp(key, "NUM_WORKERS") == 0) {
                config->num_workers = atoi(value);
            }
            else if (strcmp(key, "THREADS_PER_WORKER") == 0) {
                config->threads_per_worker = atoi(value);
            }
            else if (strcmp(key, "DOCUMENT_ROOT") == 0) {
                // Copia string para document_root com limite de tamanho
                strncpy(config->document_root, value, sizeof(config->document_root));
            }
            else if (strcmp(key, "MAX_QUEUE_SIZE") == 0) {
                config->max_queue_size = atoi(value);
            }
            else if (strcmp(key, "LOG_FILE") == 0) {
                strncpy(config->log_file, value, sizeof(config->log_file));
            }
            else if (strcmp(key, "CACHE_SIZE_MB") == 0) {
                config->cache_size_mb = atoi(value);
            }
            else if (strcmp(key, "TIMEOUT_SECONDS") == 0) {
                config->timeout_seconds = atoi(value);
            }
            // Podem ser adicionados mais parâmetros aqui se necessário
        }
    }
    
    // Fecha o ficheiro após leitura completa
    fclose(fp);
    
    // Retorna sucesso
    return 0;
}