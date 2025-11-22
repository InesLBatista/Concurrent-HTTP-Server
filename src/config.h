// config.h
// Inês Batista, Maria Quinteiro

// Este módulo define a estrutura de configuração e função para carregar 
// parâmetros do ficheiro server.conf. É fundamental para personalizar o 
// comportamento do servidor sem recompilar.

#ifndef CONFIG_H
#define CONFIG_H

// Estrutura de configuração do servidor - conforme template 
typedef struct {
    int port;                       // Porta onde o servidor escuta (ex: 8080)
    char document_root[256];        // Diretório base dos ficheiros web
    int num_workers;                // Número de processos worker
    int threads_per_worker;         // Threads por processo worker  
    int max_queue_size;             // Tamanho máximo da fila de conexões
    char log_file[256];             // Ficheiro de log de acesso
    int cache_size_mb;              // Tamanho máximo da cache em MB
    int timeout_seconds;            // Timeout de conexão em segundos
} server_config_t;

// Função para carregar configurações do ficheiro - conforme template
int load_config(const char* filename, server_config_t* config);

#endif