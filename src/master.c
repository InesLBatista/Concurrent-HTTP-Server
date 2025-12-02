

// Inês Batista, 124877
// Maria Quinteiro, 124996

#include "master.h"
#include "config.h"
#include "shared_memory.h"
#include "worker.h"     // Necessário para a chamada a worker_main após o fork
#include "stats.h"      // Para a função update_stats
#include "http.h"       // Para as funções send_503_response e send_500_response
#include <stdio.h>      // Funções de I/O padrão
#include <stdlib.h>     // Funções gerais (exit)
#include <unistd.h>     // Funções POSIX (fork, getpid, close)
#include <sys/socket.h> // Funções de socket (socket, bind, listen, accept)
#include <netinet/in.h> // Estruturas de endereço de rede (sockaddr_in)
#include <string.h>     // Funções de manipulação de strings (memset)
#include <signal.h>     // Funções de sinal (signal, SIGTERM, kill)
#include <sys/wait.h>   // Funções de espera por processos filhos (waitpid)
#include <fcntl.h>      // Controlo de descritores de ficheiros
#include <errno.h>      // Variável global de erro (errno)
#include <sys/mman.h>   // Para munmap, caso seja necessário desanexar a SHM

// Constante para o número máximo de Workers que o array pode guardar
#define MAX_WORKERS 100 
// Array estático para guardar os PIDs dos Workers gerados
static pid_t worker_pids[MAX_WORKERS];
// Contador do número de Workers criados com sucesso.
static int num_workers = 0; 
// Flag volátil para controlar o loop principal (terminação graciosa)
volatile sig_atomic_t master_keep_running = 1; 
// Descritor do socket de escuta do servidor. Global para poder ser fechado 
// pelos Workers após o fork, libertando o recurso.
static int server_fd = -1; 



// Handler para sinais (SIGINT - Ctrl+C, SIGTERM - sinal de terminação)
// Esta função é chamada de forma assíncrona quando o Master recebe um destes sinais.
void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        // Altera a flag para falso, fazendo com que o ciclo 'while (master_keep_running)' termine.
        master_keep_running = 0; 
        printf("\n[Master %d] Signal %d received. Initiating graceful shutdown...\n", getpid(), signum);
    }
}

// Cria o socket principal do servidor (AF_INET, SOCK_STREAM), faz bind e listen.
// Retorna o descritor de ficheiro do socket ou -1 em caso de falha.
int create_server_socket(int port) {
    int fd;
    struct sockaddr_in address;
    int opt = 1; // Variável auxiliar para setsockopt

    // 1. Criar o socket (IPv4, TCP)
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("ERROR: Failed to create socket"); 
        return -1;
    }

    // 2. Configurar SO_REUSEADDR (Permite reutilização imediata da porta)
    // Essencial para reiniciar o servidor rapidamente após um encerramento inesperado.
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("ERROR: setsockopt SO_REUSEADDR failed"); 
        close(fd);
        return -1;
    }

    // 3. Configurar a estrutura de endereço
    address.sin_family = AF_INET;           // Família de endereços: IPv4
    address.sin_addr.s_addr = INADDR_ANY;   // Aceita conexões em todas as interfaces
    address.sin_port = htons(port);         // Porta do servidor (convertida para network byte order)

    // 4. Ligar (bind) o socket ao endereço e porta definidos
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("ERROR: Failed to bind"); 
        close(fd);
        return -1;
    }

    // 5. Iniciar o modo de escuta (listen)
    // O backlog (1000) define o tamanho máximo da fila de conexões pendentes.
    if (listen(fd, 1000) < 0) { 
        perror("ERROR: Failed to listen"); 
        close(fd);
        return -1;
    }
    
    printf("[Master %d] Socket created and listening on port %d.\n", getpid(), port); 
    // Armazena o FD globalmente
    server_fd = fd; 
    return fd;
}





// Cria processos worker usando fork().
// Retorna o número de workers criados com sucesso ou -1 em caso de erro fatal.
int create_worker_processes(server_config_t* config) {
    // Verificar se o número de workers excede o máximo suportado pelo array
    if (config->num_workers > MAX_WORKERS) {
        fprintf(stderr, "Warning: Number of workers (%d) exceeds max supported (%d). Limited to %d.\n", 
                config->num_workers, MAX_WORKERS, MAX_WORKERS);
        config->num_workers = MAX_WORKERS;
    }

    printf("[Master %d] Creating %d Worker processes...\n", getpid(), config->num_workers); 
    
    num_workers = 0; // Reinicia o contador global de workers.

    for (int i = 0; i < config->num_workers; i++) {
        pid_t pid = fork(); // Duplica o processo Master.
        
        if (pid < 0) {
            // Falha do fork. Tentar terminar o que já foi criado.
            perror("ERROR: Failed to fork"); 
            terminate_worker_processes(); 
            return -1;
        } else if (pid == 0) {
            // CÓDIGO DO PROCESSO FILHO (WORKER)
            
            // É crucial que o Worker feche o socket de escuta do Master.
            // Se não o fizer, o socket nunca é totalmente libertado (resource leak) 
            // e o Worker pode receber sinais que não deve.
            if (server_fd != -1) {
                close(server_fd); 
            }

            // O Worker inicia o seu ciclo principal. O exit() garante que o Worker
            // não executa o código do Master após este ponto.
            exit(worker_main(config)); 
        } else {
            // CÓDIGO DO PROCESSO PAI (MASTER)
            // Guarda o PID do Worker para poder controlá-lo mais tarde.
            worker_pids[num_workers] = pid;
            num_workers++;
            printf("[Master %d] Worker %d (PID: %d) created.\n", getpid(), num_workers, pid); 
        }
    }
    return num_workers;
}




// Encerra todos os Workers de forma graciosa.
// Envia SIGTERM (sinal de terminação suave) e espera que cada Worker termine.
void terminate_worker_processes(void) {
    printf("[Master %d] Sending SIGTERM to %d Workers...\n", getpid(), num_workers); 
    
    // 1. Enviar SIGTERM a todos os Workers
    for (int i = 0; i < num_workers; i++) {
        if (worker_pids[i] > 0) {
            // SIGTERM é o sinal padrão para terminação graciosa.
            kill(worker_pids[i], SIGTERM); 
        }
    }

    // 2. Esperar que todos os Workers terminem (reap children)
    for (int i = 0; i < num_workers; i++) {
        if (worker_pids[i] > 0) {
            int status;
            // waitpid bloqueia até que o Worker termine. 0 significa sem opções.
            pid_t waited_pid = waitpid(worker_pids[i], &status, 0);
            if (waited_pid > 0) {
                printf("[Master %d] Worker PID: %d terminated.\n", getpid(), waited_pid); 
            }
        }
    }
}



int master_main(server_config_t* config) {
    int client_fd;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // 1. Configurar handlers de sinal
    signal(SIGINT, signal_handler);     // Para Ctrl+C
    signal(SIGTERM, signal_handler);    // Para sinais de terminação externa
    signal(SIGPIPE, SIG_IGN);           // Ignorar SIGPIPE (evita falha quando se tenta 
                                        // escrever num socket fechado pelo cliente)

    // 2. Criar e inicializar a memória partilhada (SHM)
    g_shared_data = create_shared_memory();
    if (g_shared_data == NULL) {
        fprintf(stderr, "ERROR: Failed to create shared memory. Exiting.\n"); 
        return 1;
    }
    // Inicializar o timestamp do início do servidor para estatísticas
    g_shared_data->stats.server_start_time = time(NULL);

    // 3. Criar o socket do servidor
    if (create_server_socket(config->port) < 0) {
        destroy_shared_memory(g_shared_data); // Limpeza da SHM em caso de erro
        return 1;
    }

    // 4. Criar processos Workers
    if (create_worker_processes(config) == 0) {
        // Se a criação falhar, limpar recursos antes de sair
        close(server_fd);
        destroy_shared_memory(g_shared_data);
        return 1;
    }
    
    printf("\n[Master %d] Server running. Waiting for connections at http://localhost:%d\n", getpid(), config->port); // Print em Inglês
    printf("Press Ctrl+C to stop the server.\n\n"); 
    
    int connection_count = 0;

    // 5. Loop principal de aceitação de conexões (PRODUTOR)
    while (master_keep_running) {
        // Chamada de accept() bloqueante: espera por uma nova conexão.
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0) {
            // EINTR: Erro comum quando accept é interrompido por um sinal (e.g., SIGINT/SIGTERM)
            if (errno == EINTR) {
                continue; // Volta ao início do loop para verificar 'master_keep_running' 
            }
            // Ignorar erros de accept durante o encerramento gracioso
            if (master_keep_running) {
                perror("ERROR: Failed to accept connection"); 
            }
            continue;
        }

        connection_count++;

        // LÓGICA PRODUTOR-CONSUMIDOR: Tentar enfileirar o socket
        
        // 1. Tentar obter um slot vazio SEM BLOQUEAR (sem_trywait)
        // Se sem_trywait falhar com EAGAIN, a fila está cheia.
        if (sem_trywait(&g_shared_data->empty_slots) == 0) {
            // SUCESSO: O semáforo 'empty_slots' foi decrementado (slot reservado).
            
            // O Master chama a função que trata da Secção Crítica:
            // - sem_wait(mutex)
            // - Inserir client_fd na queue
            // - sem_post(mutex)
            // - sem_post(full_slots) (acorda um Worker)
            if (enqueue_connection(client_fd) == 0) {
                 printf("[Master %d] Connection %d accepted (socket %d) - Queued: %d/%d\n",
                        getpid(), connection_count, client_fd, 
                        g_shared_data->queue.count, MAX_QUEUE_SIZE);
            } else {
                // Caso haja falha interna no enqueue_connection (e.g., erro no mutex)
                fprintf(stderr, "[Master %d] ERROR: Failed to enqueue connection %d. Sending 500.\n", getpid(), client_fd); 
                close(client_fd);
                // É VITAL repor o 'empty_slots' que foi consumido por sem_trywait no início
                sem_post(&g_shared_data->empty_slots);
                // Responder com erro interno e atualizar estatísticas
                send_500_response(client_fd, config->document_root);
                update_stats(&g_shared_data->stats, 500, 0, &g_shared_data->mutex);
            }

        } else if (errno == EAGAIN) {
            // FALHA: Fila Cheia (sem_trywait retornou EAGAIN)
            
            printf("[Master %d] ERROR: Connection queue full. Sending 503 (socket %d)...\n", getpid(), client_fd); 
            
            // 1. Enviar resposta HTTP 503 (Service Unavailable)
            send_503_response(client_fd, config->document_root);

            // 2. Atualizar estatísticas (incrementa server_errors 5xx)
            update_stats(&g_shared_data->stats, 503, 0, &g_shared_data->mutex);
            
            // 3. Fechar o socket do cliente, pois a conexão não será processada
            close(client_fd);
            
        } else {
            // Outro erro inesperado no sem_trywait
            perror("ERROR: sem_trywait failed unexpectedly"); 
            close(client_fd);
        }
    }

    // 6. LIMPEZA FINAL (Shutdown) - Executado quando 'master_keep_running' é falso
    printf("\n[Master %d] Initiating server cleanup...\n", getpid()); 
    
    // 6.1. Terminar Workers
    terminate_worker_processes();

    // 6.2. Fechar o socket principal
    if (server_fd != -1) {
        close(server_fd);
        server_fd = -1;
    }

    // 6.3. Limpar a memória partilhada (desmapear e remover o segmento)
    destroy_shared_memory(g_shared_data);
    g_shared_data = NULL; // Assegurar que o ponteiro global é limpo

    printf("[Master %d] Shutdown complete. Goodbye!\n", getpid()); 
    return 0;
}