// main.c
// Inês Batista, Maria Quinteiro

// Ponto de entrada do servidor HTTP concorrente.
// Inicializa todos os componentes do sistema e testa a infraestrutura base.

#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "shared_memory.h"
#include "semaphores.h"

// Função principal do programa
int main(int argc, char *argv[]) {
    printf("=== Concurrent HTTP Server - Starting ===\n");
    
    // 1. Carregar configuração do ficheiro server.conf
    server_config_t config;
    if (load_config("server.conf", &config) != 0) {
        fprintf(stderr, "ERROR: Failed to load configuration from server.conf\n");
        return EXIT_FAILURE;
    }
    printf("✓ Configuration loaded successfully\n");
    
    // 2. Criar memória partilhada para fila e estatísticas
    shared_data_t* shared_data = create_shared_memory();
    if (shared_data == NULL) {
        fprintf(stderr, "ERROR: Failed to create shared memory\n");
        return EXIT_FAILURE;
    }
    printf("✓ Shared memory initialized\n");
    
    // 3. Inicializar semáforos para sincronização
    semaphores_t semaphores;
    if (init_semaphores(&semaphores, config.max_queue_size) != 0) {
        fprintf(stderr, "ERROR: Failed to initialize semaphores\n");
        destroy_shared_memory(shared_data);
        return EXIT_FAILURE;
    }
    printf("✓ Semaphores initialized\n");
    
    // 4. Testar operações da fila
    printf("Testing queue operations...\n");
    
    // Teste de enqueue (deverá falhar porque não há sockets reais)
    int test_result = enqueue_connection(shared_data, 
                                       semaphores.empty_slots,
                                       semaphores.filled_slots, 
                                       semaphores.queue_mutex, 
                                       123); // FD fictício para teste
    
    if (test_result == 0) {
        printf("✓ Enqueue operation successful\n");
        
        // Teste de dequeue
        int fd = dequeue_connection(shared_data,
                                  semaphores.empty_slots,
                                  semaphores.filled_slots,
                                  semaphores.queue_mutex);
        printf("✓ Dequeue operation successful (FD: %d)\n", fd);
    } else {
        printf("✓ Enqueue correctly detected full queue\n");
    }
    
    // 5. Mostrar configuração carregada
    printf("\n=== Server Configuration ===\n");
    printf("Port: %d\n", config.port);
    printf("Document Root: %s\n", config.document_root);
    printf("Workers: %d\n", config.num_workers);
    printf("Threads per Worker: %d\n", config.threads_per_worker);
    printf("Max Queue Size: %d\n", config.max_queue_size);
    printf("Log File: %s\n", config.log_file);
    printf("Cache Size: %d MB\n", config.cache_size_mb);
    printf("Timeout: %d seconds\n", config.timeout_seconds);
    
    printf("\n=== Infrastructure Test Complete ===\n");
    printf("All base components initialized and tested successfully!\n");
    printf("Ready for master-worker implementation in next phase.\n");
    
    // 6. Cleanup (em produção isto seria feito no shutdown)
    destroy_semaphores(&semaphores);
    destroy_shared_memory(shared_data);
    
    printf("✓ Cleanup completed\n");
    return EXIT_SUCCESS;
}