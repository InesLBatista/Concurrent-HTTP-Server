#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "config.h"
#include "master.h"
#include "worker.h"
#include "shared_mem.h"
#include "semaphores.h"

server_config_t config;
shared_data_t* shared_data = NULL;
semaphores_t semaphores;
pid_t* worker_pids = NULL;

void cleanup() {
    printf("\nPerforming cleanup...\n");
    
    // Kill worker processes
    if (worker_pids) {
        for (int i = 0; i < config.num_workers; i++) {
            if (worker_pids[i] > 0) {
                kill(worker_pids[i], SIGTERM);
            }
        }
        free(worker_pids);
    }
    
    // Cleanup semaphores
    destroy_semaphores(&semaphores);
    
    // Cleanup shared memory
    if (shared_data) {
        destroy_shared_memory(shared_data);
    }
    
    printf("Cleanup completed.\n");
}

void signal_handler(int signum) {
    printf("\nReceived signal %d, shutting down...\n", signum);
    cleanup();
    exit(0);
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Load configuration
    if (load_config("server.conf", &config) != 0) {
        fprintf(stderr, "Failed to load configuration\n");
        return 1;
    }
    
    printf("=== Concurrent HTTP Server ===\n");
    printf("Port: %d\n", config.port);
    printf("Document Root: %s\n", config.document_root);
    printf("Workers: %d\n", config.num_workers);
    printf("Threads per Worker: %d\n", config.threads_per_worker);
    printf("==============================\n\n");
    
    // Create shared memory
    shared_data = create_shared_memory();
    if (!shared_data) {
        fprintf(stderr, "Failed to create shared memory\n");
        return 1;
    }
    
    // Initialize semaphores
    if (init_semaphores(&semaphores, config.max_queue_size) != 0) {
        fprintf(stderr, "Failed to initialize semaphores\n");
        destroy_shared_memory(shared_data);
        return 1;
// Inês Batista, 124877
// Maria Quinteiro, 124996

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

// Headers do projeto para configuração e protocolo HTTP
#include "config.h"       
#include "http.h"         

#define BACKLOG 128 // Máximo de conexões na fila de espera do listen()

// Variável global para armazenar as configurações lidas do server.conf
server_config_t g_config;



// Cria, configura e coloca o socket principal em modo de escuta (listen).
int create_server_socket(int port) {
    int listen_fd;
    struct sockaddr_in server_addr;
    int optval = 1; 

    // 1. Criar o socket (IPv4 e TCP)
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("ERRO: Falha ao criar o socket");
        return -1;
    }

    // 2. Configurar SO_REUSEADDR para reuso imediato da porta após o encerramento
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, 
                   (const void *)&optval, sizeof(int)) < 0) {
        perror("ERRO: Falha ao configurar SO_REUSEADDR");
        close(listen_fd);
        return -1;
    }

    // 3. Configurar o endereço: Ouve em todas as interfaces (INADDR_ANY)
    memset(&server_addr, 0, sizeof(server_addr)); 
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); 
    server_addr.sin_port = htons((uint16_t)port);    

    // 4. Ligar (bind) o socket ao endereço e porta
    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("ERRO: Falha ao ligar (bind) o socket");
        close(listen_fd);
        return -1;
    }

    // 5. Colocar o socket em modo de escuta (listen)
    if (listen(listen_fd, BACKLOG) < 0) {
        perror("ERRO: Falha ao escutar (listen) no socket");
        close(listen_fd);
        return -1;
    }

    printf("Servidor a escutar na porta %d...\n", port);
    return listen_fd;
}



// Processa um único pedido HTTP: lê, analisa, serve o ficheiro ou envia o erro.
void handle_client(int client_fd) {
    char buffer[2048]; 
    http_request_t req;
    ssize_t bytes_read;
    
    // 1. Ler o pedido HTTP do cliente
    bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) {
        goto cleanup; // Conexão fechada ou erro
    }
    buffer[bytes_read] = '\0'; 

    // 2. Analisar a primeira linha do pedido (Método, Caminho, Versão)
    if (parse_http_request(buffer, &req) != 0) {
        send_500_response(client_fd, g_config.document_root);
        goto cleanup;
    }

    // 3. Validar o Método HTTP (apenas GET e HEAD suportados)
    if (strcmp(req.method, "GET") != 0 && strcmp(req.method, "HEAD") != 0) {
        send_500_response(client_fd, g_config.document_root);
        goto cleanup;
    }
    
    // 4. Construir o caminho completo do ficheiro (tratar "/" para index.html)
    char filepath[512];
    const char* req_path = strcmp(req.path, "/") == 0 ? "/index.html" : req.path;
    snprintf(filepath, sizeof(filepath), "%s%s", g_config.document_root, req_path);

    // 5. Verificar o ficheiro (existência e permissões)
    struct stat file_info;
    
    // stat() verifica se o ficheiro existe
    if (stat(filepath, &file_info) < 0) {
        send_404_response(client_fd, g_config.document_root);
        goto cleanup;
    }
    
    // S_ISREG verifica se é um ficheiro regular e access(R_OK) verifica a permissão de leitura
    if (!S_ISREG(file_info.st_mode) || access(filepath, R_OK) != 0) {
        send_403_response(client_fd, g_config.document_root);
        goto cleanup;
    }
    
    // 6. Servir o ficheiro (Status 200 OK)
    FILE* file = fopen(filepath, "rb"); 
    if (!file) {
        send_500_response(client_fd, g_config.document_root);
        goto cleanup;
    }

    size_t file_size = file_info.st_size;
    
    // Initialize shared data
    shared_data->queue.front = 0;
    shared_data->queue.rear = 0;
    shared_data->queue.size = 0;  // Corrigido: 'size' em vez de 'count'
    
    // Inicializar estatísticas usando stats_init() se disponível
    // ou inicializar manualmente conforme sua estrutura
    stats_init(&shared_data->stats);  // Se stats_init() estiver disponível
    

    
    // Inicializar o mutex das estatísticas
    pthread_mutex_init(&shared_data->stats_mutex, NULL);
    
    // Create worker processes
    worker_pids = malloc(sizeof(pid_t) * config.num_workers);
    
    for (int i = 0; i < config.num_workers; i++) {
        pid_t pid = fork();
        
        if (pid == 0) {
            // Child process - worker
            worker_main(i, shared_data, &semaphores, &config);
            exit(0);
        } else if (pid > 0) {
            // Parent process
            worker_pids[i] = pid;
            printf("Started worker process %d (PID: %d)\n", i, pid);
        } else {
            fprintf(stderr, "Failed to fork worker process\n");
            cleanup();
            return 1;
        }
        
        sleep(1); // Stagger worker startup
    }
    
    // Run master process
    printf("\nMaster process starting...\n");
    master_main(shared_data, &semaphores, &config);
    
    // Cleanup (should not reach here unless error)
    cleanup();
    // Alocar e ler o conteúdo completo do ficheiro para a memória (simplificação Dia 3)
    char* content = malloc(file_size + 1);
    
    if (content == NULL || fread(content, 1, file_size, file) != file_size) {
        if (content) free(content);
        fclose(file);
        send_500_response(client_fd, g_config.document_root);
        goto cleanup;
    }
    content[file_size] = '\0'; 
    fclose(file);
    
    // Determinar o MIME Type e o corpo da resposta (corpo NULL para HEAD)
    const char* mime_type = get_mime_type(req_path);
    const char* body_ptr = (strcmp(req.method, "HEAD") == 0) ? NULL : content;

    // Enviar a resposta HTTP
    send_http_response(client_fd, 200, "OK", mime_type, body_ptr, file_size);

    free(content); // Libertar a memória alocada


cleanup:
    // 7. Fechar a conexão
    close(client_fd);
}



// Ponto de entrada do programa: inicializa e executa o loop de aceitação.
int main(int argc, char *argv[]) {
    int listen_fd;
    int client_fd;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // 1. Carregar configuração a partir do ficheiro server.conf
    if (load_config("./server.conf", &g_config) != 0) {
        fprintf(stderr, "ERRO fatal: Falha ao carregar a configuração.\n");
        return 1;
    }
    printf("Document Root configurado para: %s\n", g_config.document_root);

    // 2. Inicializar o socket de escuta
    listen_fd = create_server_socket(g_config.port);
    if (listen_fd < 0) {
        fprintf(stderr, "ERRO fatal: Falha ao inicializar o socket do servidor.\n");
        return 1;
    }
    
    
    printf("A iniciar o loop do servidor single-threaded. Ctrl+C para terminar.\n");
    
    while (1) {
        // 3. Aceitar nova conexão (função bloqueante)
        client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        
        if (client_fd < 0) {
            perror("ERRO: Falha ao aceitar conexão (accept)");
            continue;
        }
        
        // 4. Manipular o cliente na thread principal
        handle_client(client_fd);
    }

    // close(listen_fd); // Apenas necessário num shutdown controlado
    
    return 0;
}