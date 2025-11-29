// Inês Batista, 124877
// Maria Quinteiro, 124996

#ifndef CONFIG_H
#define CONFIG_H

// Esta estrutura guarda todas as configurações do servidor
// São os valores que mudam consoante o ficheiro server.conf
typedef struct {
    int port;                       // Porto onde o servidor vai escutar (ex: 8080)
    char document_root[256];       // Pasta onde estão os ficheiros HTML, CSS, etc.
    int num_workers;                // Quantos processos trabalhadores vamos criar
    int threads_per_worker;    // Quantas threads cada worker vai ter
    int max_queue_size;             // Quantas ligações podem ficar em espera
    char log_file[256];      // Onde guardar os registos do servidor
    int cache_size_mb;            // Quanto espaço usar para cache de ficheiros
    int timeout_seconds;       // Quanto tempo esperar por cada cliente
} server_config_t;

// Esta função lê o ficheiro server.conf e preenche a estrutura acima
int load_config(const char* filename, server_config_t* config);

#endif