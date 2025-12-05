#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>

// ==============================================
// CONFIGURAÇÃO
// ==============================================

#define DEFAULT_THREADS 50
#define REQUESTS_PER_THREAD 200
#define SERVER_PORT 8080
#define SERVER_HOST "127.0.0.1"
#define MAX_THREADS 200
#define BUFFER_SIZE 4096

// ==============================================
// ESTRUTURAS DE DADOS
// ==============================================

typedef struct {
    int id;
    int requests_sent;
    int requests_ok;
    int requests_404;
    int requests_error;
    long total_bytes;
    double total_time;
    int cache_hits;
    int cache_misses;
} ThreadStats;

typedef struct {
    ThreadStats *stats;
    int num_threads;
    pthread_mutex_t mutex;
    volatile int running;
} TestContext;

// ==============================================
// FUNÇÕES AUXILIARES
// ==============================================

// Obter timestamp em milissegundos
double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

// Criar conexão TCP
int create_connection() {
    int sockfd;
    struct sockaddr_in server_addr;
    struct hostent *server;
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return -1;
    }
    
    // Configurar timeout
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    server = gethostbyname(SERVER_HOST);
    if (server == NULL) {
        close(sockfd);
        return -1;
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    if (server->h_addr_list[0] != NULL) {
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
    } else {
        close(sockfd);
        return -1;
    }
    server_addr.sin_port = htons(SERVER_PORT);
    
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        return -1;
    }
    
    return sockfd;
}

// Enviar requisição HTTP e analisar resposta
int send_http_request(int thread_id, int request_num, ThreadStats *local_stats) {
    int sockfd;
    char request[1024];
    char response[BUFFER_SIZE];
    double start_time, end_time;
    int status_code = 0;
    size_t content_length = 0;
    int cache_status = -1; // -1: desconhecido, 0: miss, 1: hit
    
    // Selecionar recurso para testar
    const char *resources[] = {
        "/",
        "/index.html",
        "/style.css", 
        "/script.js",
        "/test.jpg",
        "/nonexistent.html",
        "/sub/page.html"
    };
    int resource_index = request_num % 7;
    const char *resource = resources[resource_index];
    
    // Criar requisição
    snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: localhost:%d\r\n"
        "User-Agent: ConcurrentTest/1.0 (Thread-%d)\r\n"
        "Connection: close\r\n"
        "\r\n",
        resource, SERVER_PORT, thread_id);
    
    // Criar conexão
    sockfd = create_connection();
    if (sockfd < 0) {
        return -1;
    }
    
    start_time = get_time_ms();
    
    // Enviar requisição
    if (send(sockfd, request, strlen(request), 0) < 0) {
        close(sockfd);
        return -1;
    }
    
    // Receber resposta
    ssize_t total = 0;
    ssize_t n;
    
    while (total < BUFFER_SIZE - 1) {
        n = recv(sockfd, response + total, BUFFER_SIZE - 1 - total, 0);
        if (n <= 0) {
            break;
        }
        total += n;
        response[total] = '\0';
        
        // Verificar se temos o cabeçalho completo
        if (strstr(response, "\r\n\r\n") != NULL) {
            break;
        }
    }
    
    end_time = get_time_ms();
    
    if (total <= 0) {
        close(sockfd);
        return -1;
    }
    
    // Analisar resposta
    char *header_end = strstr(response, "\r\n\r\n");
    if (header_end) {
        *header_end = '\0';
        
        // Extrair status code
        char *status_line = response;
        if (strncmp(status_line, "HTTP/1.", 7) == 0) {
            char *status_start = strchr(status_line, ' ');
            if (status_start) {
                status_code = atoi(status_start + 1);
            }
        }
        
        // Extrair Content-Length
        char *cl_header = strstr(response, "Content-Length:");
        if (cl_header) {
            content_length = atoi(cl_header + 15);
        }
        
        // Verificar cache status
        char *cache_header = strstr(response, "X-Cache:");
        if (cache_header) {
            if (strstr(cache_header, "HIT")) {
                cache_status = 1;
            } else if (strstr(cache_header, "MISS")) {
                cache_status = 0;
            }
        }
    }
    
    close(sockfd);
    
    // Atualizar estatísticas locais
    local_stats->requests_sent++;
    local_stats->total_time += (end_time - start_time);
    local_stats->total_bytes += content_length;
    
    if (status_code == 200) {
        local_stats->requests_ok++;
    } else if (status_code == 404) {
        local_stats->requests_404++;
    } else {
        local_stats->requests_error++;
    }
    
    if (cache_status == 1) {
        local_stats->cache_hits++;
    } else if (cache_status == 0) {
        local_stats->cache_misses++;
    }
    
    return status_code;
}

// ==============================================
// THREAD WORKER
// ==============================================

void *thread_worker(void *arg) {
    ThreadStats *local_stats = (ThreadStats *)arg;
    int thread_id = local_stats->id;
    
    printf("[Thread %d] Iniciando...\n", thread_id);
    
    for (int i = 0; i < REQUESTS_PER_THREAD; i++) {
        send_http_request(thread_id, i, local_stats);
        
        // Pequena pausa aleatória entre requisições
        if ((i + 1) % 10 == 0) {
            usleep(1000 * (rand() % 10)); // 0-10ms
        }
    }
    
    printf("[Thread %d] Concluído: %d requisições\n", thread_id, local_stats->requests_sent);
    
    return NULL;
}

// ==============================================
// FUNÇÃO PRINCIPAL
// ==============================================

int main(int argc, char *argv[]) {
    int num_threads = DEFAULT_THREADS;
    pthread_t threads[MAX_THREADS];
    ThreadStats stats[MAX_THREADS];
    TestContext context;
    
    // Parse argumentos
    if (argc > 1) {
        num_threads = atoi(argv[1]);
        if (num_threads < 1 || num_threads > MAX_THREADS) {
            fprintf(stderr, "Número de threads inválido. Use 1-%d\n", MAX_THREADS);
            return 1;
        }
    }
    
    printf("╔════════════════════════════════════════════╗\n");
    printf("║    CONCURRENT HTTP SERVER - LOAD TEST     ║\n");
    printf("╚════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Configuração:\n");
    printf("  Threads:          %d\n", num_threads);
    printf("  Requisições/thread: %d\n", REQUESTS_PER_THREAD);
    printf("  Total:            %d requisições\n", num_threads * REQUESTS_PER_THREAD);
    printf("  Servidor:         %s:%d\n", SERVER_HOST, SERVER_PORT);
    printf("\n");
    
    // Inicializar estatísticas
    memset(stats, 0, sizeof(stats));
    for (int i = 0; i < num_threads; i++) {
        stats[i].id = i + 1;
    }
    
    pthread_mutex_init(&context.mutex, NULL);
    context.stats = stats;
    context.num_threads = num_threads;
    context.running = 1;
    
    // Iniciar teste
    printf("Iniciando teste de carga...\n");
    printf("════════════════════════════════════════════\n");
    
    double start_time = get_time_ms();
    
    // Criar threads
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, thread_worker, &stats[i]) != 0) {
            fprintf(stderr, "Erro ao criar thread %d\n", i);
            return 1;
        }
    }
    
    // Monitorar progresso
    printf("\nProgresso:\n");
    int last_completed = 0;
    
    while (context.running) {
        sleep(2);
        
        int total_completed = 0;
        for (int i = 0; i < num_threads; i++) {
            total_completed += stats[i].requests_sent;
        }
        
        int progress = total_completed * 100 / (num_threads * REQUESTS_PER_THREAD);
        printf("  %d%% completado (%d/%d requisições)\n", 
               progress, total_completed, num_threads * REQUESTS_PER_THREAD);
        
        if (total_completed >= num_threads * REQUESTS_PER_THREAD) {
            context.running = 0;
        }
        
        // Se não houve progresso desde a última verificação
        if (total_completed == last_completed) {
            printf("  ⚠️  Possível estagnação\n");
        }
        last_completed = total_completed;
    }
    
    // Aguardar threads terminarem
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double end_time = get_time_ms();
    double total_time = (end_time - start_time) / 1000.0; // segundos
    
    // Calcular estatísticas totais
    int total_requests = 0;
    int total_ok = 0;
    int total_404 = 0;
    int total_errors = 0;
    long total_bytes = 0;
    double total_response_time = 0;
    int total_cache_hits = 0;
    int total_cache_misses = 0;
    
    for (int i = 0; i < num_threads; i++) {
        total_requests += stats[i].requests_sent;
        total_ok += stats[i].requests_ok;
        total_404 += stats[i].requests_404;
        total_errors += stats[i].requests_error;
        total_bytes += stats[i].total_bytes;
        total_response_time += stats[i].total_time;
        total_cache_hits += stats[i].cache_hits;
        total_cache_misses += stats[i].cache_misses;
    }
    
    // Exibir resultados
    printf("\n════════════════════════════════════════════\n");
    printf("RESULTADOS DO TESTE DE CARGA\n");
    printf("════════════════════════════════════════════\n");
    printf("\n");
    
    printf("Tempo total:           %.2f segundos\n", total_time);
    printf("Requisições totais:    %d\n", total_requests);
    printf("Taxa de requisições:   %.2f req/seg\n", total_requests / total_time);
    printf("Throughput:            %.2f MB/seg\n", 
           (total_bytes / (1024.0 * 1024.0)) / total_time);
    printf("\n");
    
    printf("Distribuição de respostas:\n");
    printf("  200 OK:              %d (%.1f%%)\n", 
           total_ok, (double)total_ok * 100 / total_requests);
    printf("  404 Not Found:       %d (%.1f%%)\n", 
           total_404, (double)total_404 * 100 / total_requests);
    printf("  Outros/Erros:        %d (%.1f%%)\n", 
           total_errors, (double)total_errors * 100 / total_requests);
    printf("\n");
    
    if (total_response_time > 0 && total_requests > 0) {
        printf("Tempo médio resposta:  %.2f ms\n", 
               total_response_time / total_requests);
    }
    
    printf("Estatísticas de cache:\n");
    int total_cached = total_cache_hits + total_cache_misses;
    if (total_cached > 0) {
        printf("  Hits:                %d (%.1f%%)\n", 
               total_cache_hits, (double)total_cache_hits * 100 / total_cached);
        printf("  Misses:              %d (%.1f%%)\n", 
               total_cache_misses, (double)total_cache_misses * 100 / total_cached);
        printf("  Hit ratio:           %.1f%%\n", 
               (double)total_cache_hits * 100 / total_cached);
    } else {
        printf("  Sem dados de cache disponíveis\n");
    }
    
    printf("\n");
    printf("Bytes transferidos:    %.2f MB\n", total_bytes / (1024.0 * 1024.0));
    
    // Verificar consistência
    printf("\nVerificação de consistência:\n");
    if (total_requests == (num_threads * REQUESTS_PER_THREAD)) {
        printf("  ✅ Todas as requisições foram processadas\n");
    } else {
        printf("  ⚠️  %d requisições não foram processadas\n", 
               (num_threads * REQUESTS_PER_THREAD) - total_requests);
    }
    
    if (total_errors == 0) {
        printf("  ✅ Nenhum erro de conexão\n");
    } else {
        printf("  ⚠️  %d erros de conexão\n", total_errors);
    }
    
    printf("\n════════════════════════════════════════════\n");
    printf("TESTE CONCLUÍDO\n");
    printf("════════════════════════════════════════════\n");
    
    // Limpar
    pthread_mutex_destroy(&context.mutex);
    
    return 0;
}