

// Inês Batista, 124877
// Maria Quinteiro, 124996

#include "shared_memory.h"   // Acesso à SHM, semáforos e shared_data_t
#include "worker.h"          // Protótipos das funções Worker
#include "config.h"          // Estruturas de configuração
#include "stats.h"           // função update_stats
#include "logger.h"          // função log_request
#include "http.h"            // estrutura http_request_t

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>      // Funções de rede (send, close).
#include <string.h>          // memset, strlen.
#include <signal.h>          // Para o signal_handler.
#include <sys/mman.h>        // munmap (para detach da SHM).
#include <fcntl.h>           // shm_open flags.
#include <sys/stat.h>        // shm_open mode.


// flag de controlo para terminação
volatile sig_atomic_t worker_keep_running = 1;

// resposta de HTTP simpes 200 OK (APENAS PARA TESTES INICIAIS)
static const char* HTTP_200_RESPONSE =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Connection: close\r\n"
    "Content-Length: 17\r\n"
    "\r\n"
    "Worker Process OK";

    
void worker_signal_handler(int signum) {
    worker_keep_running = 0; // altera a flag para conseguir sair do loop principal
    printf("[Worker %d] Received signal %d. Shutting down...\n", getpid(), signum);
}

// simula o processamento de um pedido (ENVIA UM 200 OK SIMPLES)
void process_request(int client_fd) {
    size_t bytes_sent = strlen(HTTP_200_RESPONSE);
    int status_code = 200;
    
    // NOTE: Em código real, faríamos a leitura do socket aqui.
    // Vamos simular uma estrutura de pedido para o log.
    http_request_t mock_req = {0};
    strcpy(mock_req.method, "GET");
    strcpy(mock_req.path, "/test");
    strcpy(mock_req.version, "HTTP/1.1");
    // NOTE: O IP remoto seria obtido durante o accept no Master,
    // mas não temos essa informação no Worker só com o client_fd.
    const char *mock_ip = "127.0.0.1"; 


    // 1. Enviar resposta 200 OK
    send(client_fd, HTTP_200_RESPONSE, bytes_sent, 0);

    // 2. Atualizar estatísticas (USANDO A FUNÇÃO CENTRALIZADA)
    if (g_shared_data) {
        // Agora usamos a função que faz o sem_wait/post internamente e lida com toda a lógica.
        update_stats(&g_shared_data->stats, status_code, bytes_sent, &g_shared_data->mutex);
        
        // 3. REGISTAR O PEDIDO (LOG)
        // Usamos o mutex da SHM para proteger a escrita do log,
        // garantindo que não há race conditions no ficheiro de log.
        log_request(mock_ip, &mock_req, status_code, bytes_sent, &g_shared_data->mutex);
    }
    
    // 4. fechar conexão
    close(client_fd);
    printf("[Worker %d] Processed request (socket %d). Status: %d\n", getpid(), client_fd, status_code);
}



int worker_main(server_config_t* config) {
    // 1. configurar handlers de sinal
    // o worker deve responder ao SIGTERM enviado pelo Master
    signal(SIGINT, SIG_IGN);  //Ignora o Ctr+C para deixar o Master tratar do shutdown inicial
    signal(SIGTERM, worker_signal_handler); //terminação pelo Master

    // 2. ligar à memória partilhada (assume que o Master já a criou!!!)
    // O SHM_NAME é uma macro definida em shared_memory.h
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666); 
    if (shm_fd < 0) {
        perror("[Worker] shm_open failed");
        return -1;
    }
    // Mapeamento. Assume que o Master já definiu o tamanho.
    g_shared_data = (shared_data_t *)mmap(0, sizeof(shared_data_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd);

    if (g_shared_data == MAP_FAILED) {
        perror("[Worker] mmap failed");
        return -1;
    }

    printf("[Worker %d] Attached to shared memory. Starting work loop...\n", getpid());


    // 3. loop principal do consumidor
    while (worker_keep_running) {
        // A função dequeue_connection bloqueia o Worker se a fila estiver vazia (sem_wait full_slots).
        int client_fd = dequeue_connection();

        // Se o worker recebeu SIGTERM enquanto estava bloqueado em sem_wait,
        // o sem_wait retorna -1 com errno = EINTR. O dequeue_connection trata isso.
        
        if (client_fd > 0) {
            // Conexão recebida, processar.
            process_request(client_fd);
        } else if (client_fd == -1 && worker_keep_running == 0) {
            // Se dequeue falhou (client_fd == -1) E o Master já sinalizou o fim (worker_keep_running = 0).
            break; // Sair do loop.
        } else if (client_fd == -1 && worker_keep_running == 1) {
             // Se dequeue falhou mas ainda está a correr (erro de semáforo).
             fprintf(stderr, "[Worker %d] FATAL ERROR: dequeue failed but worker still running. Retrying in 1s...\n", getpid());
             sleep(1);
        }
    }


    // 4. limpeza final
    if (g_shared_data != NULL) {
        // O Worker APENAS desmapeia (detach). O Master é que desvincula (unlink).
        munmap(g_shared_data, sizeof(shared_data_t)); 
    }
    
    printf("[Worker %d] Terminated gracefully.\n", getpid());
    return 0;
}