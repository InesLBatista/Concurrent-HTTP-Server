// main.c
// Inês Batista, Maria Quinteiro

// Ponto de entrada do servidor HTTP concorrente.
// Inicializa todos os componentes e inicia o master process.

#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "shared_memory.h"
#include "semaphores.h"
#include "master.h"

// Função principal do programa
int main(int argc, char *argv[]) {
    // Silencia unused parameters para evitar warnings com -Werror
    (void)argc;
    (void)argv;
    
    printf("=== Concurrent HTTP Server ===\n");
    printf("Inês Batista & Maria Quinteiro - P3G4\n\n");
    
    // 1. Carregar configuração
    server_config_t config;
    if (load_config("server.conf", &config) != 0) {
        fprintf(stderr, "ERROR: Failed to load configuration from server.conf\n");
        return EXIT_FAILURE;
    }
    printf("✓ Configuration loaded\n");
    
    // 2. Inicializar master process
    master_context_t* master = init_master(&config);
    if (master == NULL) {
        fprintf(stderr, "ERROR: Failed to initialize master process\n");
        return EXIT_FAILURE;
    }
    printf("✓ Master process initialized\n");
    
    // 3. Executar servidor
    run_master(master);
    
    // Cleanup é feito automaticamente em cleanup_master()
    return EXIT_SUCCESS;
}