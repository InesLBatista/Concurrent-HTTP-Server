/* worker.c - VERSÃO COMPLETA COM LOGGING */

#include "worker.h"
#include "thread_pool.h"
#include "http.h"
#include "logger.h"
#include "shared_mem.h"
#include "cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Definir PATH_MAX se não definido */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef MAX_QUEUE_SIZE
#define MAX_QUEUE_SIZE 100
#endif

/* Variáveis globais */
volatile int worker_running = 1;
static cache_t *worker_cache = NULL;
static logger_t *worker_logger = NULL;  /* ADICIONADO: Logger por worker */

/* ==================== FUNÇÕES DE INICIALIZAÇÃO ==================== */

static void worker_cache_init(int worker_id) {
    worker_cache = cache_create(10, CACHE_MAX_ENTRIES);
    if (!worker_cache) {
        fprintf(stderr, "Worker %d: Failed to create cache\n", worker_id);
        exit(EXIT_FAILURE);
    }
    printf("Worker %d: Cache initialized\n", worker_id);
}

static void worker_cache_cleanup(int worker_id) {
    if (worker_cache) {
        cache_print_stats(worker_cache);
        cache_destroy(worker_cache);
        worker_cache = NULL;
        printf("Worker %d: Cache cleaned up\n", worker_id);
    }
}

/* ADICIONADO: Inicializar logger */
static void worker_logger_init(int worker_id) {
    char log_filename[256];
    snprintf(log_filename, sizeof(log_filename), 
             "logs/worker%d.log", worker_id);
    
    /* Criar diretório logs se não existir */
    mkdir("logs", 0755);
    
    worker_logger = logger_init(log_filename, 1); /* Habilitar rotação */
    if (!worker_logger) {
        fprintf(stderr, "Worker %d: Failed to initialize logger\n", worker_id);
        exit(EXIT_FAILURE);
    }
    
    /* Configurar tamanho máximo para 10MB */
    logger_set_max_size(worker_logger, 10 * 1024 * 1024);
    logger_set_max_backups(worker_logger, 5);
    
    printf("Worker %d: Logger initialized (%s)\n", worker_id, log_filename);
}

/* ADICIONADO: Limpar logger */
static void worker_logger_cleanup(int worker_id) {
    if (worker_logger) {
        printf("\nWorker %d: Final logger statistics:\n", worker_id);
        printf("  Buffer entries: %zu\n", logger_get_buffer_count(worker_logger));
        printf("  File size: %.2f MB\n", 
               logger_get_file_size(worker_logger) / (1024.0 * 1024.0));
        
        logger_destroy(worker_logger);
        worker_logger = NULL;
        printf("Worker %d: Logger cleaned up\n", worker_id);
    }
}

/* ==================== FUNÇÕES AUXILIARES ==================== */

static double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static const char* get_content_type(const char* filename) {
    const char* ext = strrchr(filename, '.');
    if (!ext) return "text/plain";
    
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".txt") == 0) return "text/plain";
    if (strcmp(ext, ".json") == 0) return "application/json";
    
    return "application/octet-stream";
}

static int is_static_file(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return 0;
    
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0 ||
        strcmp(ext, ".css") == 0 || strcmp(ext, ".js") == 0 ||
        strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0 ||
        strcmp(ext, ".png") == 0 || strcmp(ext, ".gif") == 0 ||
        strcmp(ext, ".ico") == 0 || strcmp(ext, ".txt") == 0 ||
        strcmp(ext, ".pdf") == 0 || strcmp(ext, ".svg") == 0) {
        return 1;
    }
    
    return 0;
}

/* ADICIONADO: Função para extrair User-Agent da requisição */
static void parse_http_headers(const char* request, char* user_agent, size_t ua_size) {
    /* Inicializar com valor padrão */
    strncpy(user_agent, "-", ua_size);
    
    /* Procurar User-Agent header */
    const char* ua_start = strstr(request, "User-Agent:");
    if (ua_start) {
        ua_start += 11; /* Pular "User-Agent:" */
        
        /* Pular espaços */
        while (*ua_start == ' ') ua_start++;
        
        /* Copiar até o fim da linha */
        const char* ua_end = strstr(ua_start, "\r\n");
        if (ua_end) {
            size_t len = ua_end - ua_start;
            if (len > ua_size - 1) len = ua_size - 1;
            strncpy(user_agent, ua_start, len);
            user_agent[len] = '\0';
        }
    }
}

/* ==================== FUNÇÃO DE LOGGING ==================== */

/* ADICIONADO: Função para fazer logging da requisição */
static void log_http_request(struct sockaddr_in* client_addr,
                            const char* method, const char* uri,
                            const char* protocol, int status,
                            size_t bytes_sent, const char* user_agent) {
    
    if (!worker_logger) return;
    
    /* Converter IP para string */
    char remote_addr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr->sin_addr), remote_addr, INET_ADDRSTRLEN);
    
    /* Determinar nível de log baseado no status */
    log_level_t level = LOG_LEVEL_INFO;
    if (status >= 400 && status < 500) {
        level = LOG_LEVEL_WARNING;
    } else if (status >= 500) {
        level = LOG_LEVEL_ERROR;
    }
    
    /* Log no formato Apache Combined */
    logger_log(worker_logger, level, remote_addr, "-",
               method, uri, protocol, status, bytes_sent,
               "-", user_agent);
}

/* ==================== FUNÇÕES PRINCIPAIS ==================== */

void worker_signal_handler(int signum) {
    worker_running = 0;
    printf("Worker received signal %d, shutting down...\n", signum);
}

int dequeue_connection(shared_data_t* data, semaphores_t* sems) {
    if (sem_wait(sems->filled_slots) == -1) {
        if (errno == EINTR) return -1;
        perror("sem_wait filled_slots");
        return -1;
    }
    
    if (sem_wait(sems->queue_mutex) == -1) {
        sem_post(sems->filled_slots);
        if (errno == EINTR) return -1;
        perror("sem_wait queue_mutex");
        return -1;
    }
    
    int client_fd = -1;
    client_fd = shared_queue_dequeue(&data->queue);
    
    if (client_fd == -1) {
        sem_post(sems->queue_mutex);
        sem_post(sems->filled_slots);
        return -1;
    }
    
    sem_post(sems->queue_mutex);
    sem_post(sems->empty_slots);
    
    return client_fd;
}

static int serve_file_direct(int client_fd, const char* full_path) {
    FILE* file = fopen(full_path, "rb");
    if (!file) return -1;
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size > 100 * 1024 * 1024) {
        fclose(file);
        return -1;
    }
    
    char* buffer = malloc(file_size);
    if (!buffer) {
        fclose(file);
        return -1;
    }
    
    size_t bytes_read = fread(buffer, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != file_size) {
        free(buffer);
        return -1;
    }
    
    char response_header[512];
    snprintf(response_header, sizeof(response_header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n"
             "\r\n",
             get_content_type(full_path), file_size);
    
    send(client_fd, response_header, strlen(response_header), 0);
    send(client_fd, buffer, file_size, 0);
    
    free(buffer);
    return 0;
}

static int serve_file_with_cache(int client_fd, const char* filepath, 
                                const char* document_root,
                                shared_data_t* shared_data,
                                int worker_id) {
    
    char full_path[PATH_MAX];
    struct stat file_stat;
    
    snprintf(full_path, sizeof(full_path), "%s/%s", document_root, filepath);
    
    if (stat(full_path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
        return -1;
    }
    
    if (file_stat.st_size <= CACHE_MAX_FILE_SIZE) {
        double start_time = get_time_ms();
        cache_entry_t* cached_entry = cache_get_read(worker_cache, full_path);
        double end_time = get_time_ms();
        
        if (cached_entry) {
            /* Cache HIT */
            log_cache_operation(worker_id, filepath, 1);
            
            char response_header[512];
            snprintf(response_header, sizeof(response_header),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "X-Cache: HIT\r\n"
                     "\r\n",
                     get_content_type(filepath), cached_entry->size);
            
            send(client_fd, response_header, strlen(response_header), 0);
            send(client_fd, cached_entry->data, cached_entry->size, 0);
            
            update_cache_statistics(shared_data, 1);
            cache_entry_release(cached_entry);
            
            return 0;
        } else {
            /* Cache MISS */
            log_cache_operation(worker_id, filepath, 0);
            
            FILE* file = fopen(full_path, "rb");
            if (!file) return -1;
            
            char* file_data = malloc(file_stat.st_size);
            if (!file_data) {
                fclose(file);
                return -1;
            }
            
            size_t bytes_read = fread(file_data, 1, file_stat.st_size, file);
            fclose(file);
            
            if (bytes_read != file_stat.st_size) {
                free(file_data);
                return -1;
            }
            
            char response_header[512];
            snprintf(response_header, sizeof(response_header),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "X-Cache: MISS\r\n"
                     "\r\n",
                     get_content_type(filepath), bytes_read);
            
            send(client_fd, response_header, strlen(response_header), 0);
            send(client_fd, file_data, bytes_read, 0);
            
            cache_put(worker_cache, full_path, file_data, bytes_read);
            update_cache_statistics(shared_data, 0);
            
            free(file_data);
            return 0;
        }
    } else {
        /* Arquivo grande, sem cache */
        log_cache_operation(worker_id, filepath, -1);
        return serve_file_direct(client_fd, full_path);
    }
}

/* ADICIONADO: Processar requisição com logging */
static void process_http_request_with_logging(int client_fd, 
                                             server_config_t* config,
                                             shared_data_t* shared_data,
                                             semaphores_t* semaphores,
                                             int worker_id,
                                             struct sockaddr_in* client_addr) {
    
    char buffer[4096];
    ssize_t bytes_read;
    
    bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }
    
    buffer[bytes_read] = '\0';
    
    /* Parsear requisição */
    char method[16], path[256], protocol[16];
    if (sscanf(buffer, "%15s %255s %15s", method, path, protocol) != 3) {
        send_http_error(client_fd, 400, "Bad Request", config);
        update_statistics(shared_data, semaphores, 400, 0);
        return;
    }
    
    /* Extrair User-Agent para logging */
    char user_agent[256];
    parse_http_headers(buffer, user_agent, sizeof(user_agent));
    
    /* Verificar método */
    if (strcmp(method, "GET") != 0) {
        send_http_error(client_fd, 501, "Not Implemented", config);
        log_http_request(client_addr, method, path, protocol, 501, 0, user_agent);
        update_statistics(shared_data, semaphores, 501, 0);
        return;
    }
    
    /* Proteção contra path traversal */
    if (strstr(path, "..") != NULL) {
        send_http_error(client_fd, 403, "Forbidden", config);
        log_http_request(client_addr, method, path, protocol, 403, 0, user_agent);
        update_statistics(shared_data, semaphores, 403, 0);
        return;
    }
    
    /* Servir index.html para raiz */
    if (strcmp(path, "/") == 0) {
        strcpy(path, "/index.html");
    }
    
    const char* requested_file = path + 1;
    
    if (is_static_file(requested_file)) {
        double start_time = get_time_ms();
        
        int result = serve_file_with_cache(client_fd, requested_file,
                                          config->document_root,
                                          shared_data, worker_id);
        
        double end_time = get_time_ms();
        double response_time = end_time - start_time;
        
        size_t bytes_sent = 0;
        
        if (result == 0) {
            /* Sucesso */
            bytes_sent = strlen(requested_file) + 100; /* approx */
            shared_stats_update_request(shared_data, 200, bytes_sent, response_time);
            log_http_request(client_addr, method, path, protocol, 200, bytes_sent, user_agent);
        } else {
            /* Erro 404 */
            send_http_error(client_fd, 404, "Not Found", config);
            shared_stats_update_request(shared_data, 404, 0, response_time);
            log_http_request(client_addr, method, path, protocol, 404, 0, user_agent);
        }
    } else {
        /* Não é arquivo estático */
        send_http_error(client_fd, 404, "Not Found", config);
        update_statistics(shared_data, semaphores, 404, 0);
        log_http_request(client_addr, method, path, protocol, 404, 0, user_agent);
    }
}

/* ==================== THREAD WORKER ==================== */

void* worker_thread_func(void* arg) {
    worker_thread_arg_t* thread_arg = (worker_thread_arg_t*)arg;
    thread_pool_t* pool = (thread_pool_t*)thread_arg->pool;
    
    while (worker_running) {
        pthread_mutex_lock(&pool->mutex);
        
        while (!pool->shutdown && pool->queue_count == 0) {
            pthread_cond_wait(&pool->cond, &pool->mutex);
        }
        
        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }
        
        if (pool->queue_count > 0) {
            int client_fd = pool->queue[pool->queue_front];
            pool->queue_front = (pool->queue_front + 1) % pool->queue_size;
            pool->queue_count--;
            pthread_mutex_unlock(&pool->mutex);
            
            /* Obter endereço do cliente */
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            getpeername(client_fd, (struct sockaddr*)&client_addr, &addr_len);
            
            double start_time = get_time_ms();
            
            /* Processar com logging */
            process_http_request_with_logging(client_fd, thread_arg->config,
                                            thread_arg->shared_data,
                                            thread_arg->semaphores,
                                            thread_arg->worker_id,
                                            &client_addr);
            
            double end_time = get_time_ms();
            double response_time = end_time - start_time;
            
            close(client_fd);
            
        } else {
            pthread_mutex_unlock(&pool->mutex);
        }
    }
    
    return NULL;
}

/* ==================== FUNÇÕES DE ESTATÍSTICAS ==================== */

void update_statistics(shared_data_t* data, semaphores_t* sems, 
                       int status_code, size_t bytes) {
    shared_stats_update_request(data, status_code, bytes, 0.0);
}

void update_cache_statistics(shared_data_t* data, int cache_hit) {
    if (!data) return;
    shared_stats_update_cache(data, cache_hit);
}

void update_error_statistics(shared_data_t* data) {
    if (!data) return;
    shared_stats_update_error(data);
}

/* ==================== FUNÇÃO PRINCIPAL DO WORKER ==================== */

void worker_main(int worker_id, shared_data_t* shared_data, 
                 semaphores_t* semaphores, server_config_t* config) {
    
    /* Configurar handlers de sinal */
    signal(SIGINT, worker_signal_handler);
    signal(SIGTERM, worker_signal_handler);
    
    printf("Worker %d started (PID: %d)\n", worker_id, getpid());
    
    /* Inicializar subsistemas */
    worker_cache_init(worker_id);
    worker_logger_init(worker_id);  /* ADICIONADO */
    
    /* Inicializar thread pool */
    thread_pool_t pool;
    thread_pool_init(&pool, config->threads_per_worker, config->max_queue_size);
    
    /* Criar argumentos para threads */
    worker_thread_arg_t thread_arg;
    thread_arg.worker_id = worker_id;
    thread_arg.shared_data = shared_data;
    thread_arg.semaphores = semaphores;
    thread_arg.config = config;
    thread_arg.pool = &pool;
    
    /* Criar threads worker */
    pthread_t* threads = malloc(sizeof(pthread_t) * config->threads_per_worker);
    if (!threads) {
        perror("malloc");
        thread_pool_destroy(&pool);
        worker_cache_cleanup(worker_id);
        worker_logger_cleanup(worker_id);
        return;
    }
    
    for (int i = 0; i < config->threads_per_worker; i++) {
        if (pthread_create(&threads[i], NULL, worker_thread_func, &thread_arg) != 0) {
            perror("pthread_create");
            
            for (int j = 0; j < i; j++) {
                pthread_join(threads[j], NULL);
            }
            
            free(threads);
            thread_pool_destroy(&pool);
            worker_cache_cleanup(worker_id);
            worker_logger_cleanup(worker_id);
            return;
        }
    }
    
    /* Timers para estatísticas */
    time_t last_stat_display = time(NULL);
    time_t last_logger_stat = time(NULL);
    int stats_interval = 60;
    int logger_stats_interval = 30;
    
    /* Loop principal */
    while (worker_running) {
        int client_fd = dequeue_connection(shared_data, semaphores);
        
        if (client_fd >= 0) {
            pthread_mutex_lock(&pool.mutex);
            
            if (pool.queue_count < pool.queue_size) {
                pool.queue[pool.queue_rear] = client_fd;
                pool.queue_rear = (pool.queue_rear + 1) % pool.queue_size;
                pool.queue_count++;
                pthread_cond_signal(&pool.cond);
            } else {
                /* Fila cheia - erro 503 */
                send_http_error(client_fd, 503, "Service Unavailable", config);
                
                /* Log do erro */
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                getpeername(client_fd, (struct sockaddr*)&client_addr, &addr_len);
                
                char remote_addr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(client_addr.sin_addr), remote_addr, INET_ADDRSTRLEN);
                
                if (worker_logger) {
                    logger_log(worker_logger, LOG_LEVEL_ERROR, remote_addr, "-",
                              "GET", "/", "HTTP/1.1", 503, 0, "-", "-");
                }
                
                update_error_statistics(shared_data);
                close(client_fd);
            }
            
            pthread_mutex_unlock(&pool.mutex);
        } else if (client_fd == -1 && errno == EINTR) {
            continue;
        } else {
            usleep(10000);
        }
        
        /* Mostrar estatísticas periódicas */
        time_t now = time(NULL);
        
        if (difftime(now, last_stat_display) >= stats_interval) {
            printf("\n[Worker %d] Statistics:\n", worker_id);
            printf("  Thread pool queue: %d/%d\n", pool.queue_count, pool.queue_size);
            printf("  Shared queue: %d/%d\n", shared_data->queue.size, shared_data->queue.capacity);
            last_stat_display = now;
        }
        
        if (difftime(now, last_logger_stat) >= logger_stats_interval) {
            if (worker_logger) {
                printf("\n[Worker %d] Logger stats:\n", worker_id);
                printf("  Buffer entries: %zu\n", logger_get_buffer_count(worker_logger));
                printf("  File size: %.2f MB\n", logger_get_file_size(worker_logger) / (1024.0 * 1024.0));
                
                /* Forçar flush periódico */
                logger_flush(worker_logger);
            }
            last_logger_stat = now;
        }
    }
    
    /* Shutdown */
    printf("Worker %d shutting down...\n", worker_id);
    thread_pool_shutdown(&pool);
    
    for (int i = 0; i < config->threads_per_worker; i++) {
        pthread_join(threads[i], NULL);
    }
    
    free(threads);
    thread_pool_destroy(&pool);
    
    /* Limpar recursos */
    worker_cache_cleanup(worker_id);
    worker_logger_cleanup(worker_id);  /* ADICIONADO */
    
    printf("Worker %d exited\n", worker_id);
}

/* ==================== FUNÇÕES AUXILIARES ==================== */

void log_cache_operation(int worker_id, const char* filename, int cache_hit) {
    const char* status = (cache_hit == 1) ? "HIT" : 
                        (cache_hit == 0) ? "MISS" : "BYPASS";
    printf("[Worker %d] Cache %s: %s\n", worker_id, status, filename);
}

void log_operation_time(int worker_id, const char* operation, 
                       double start_time_ms, double end_time_ms) {
    double duration_ms = end_time_ms - start_time_ms;
    if (duration_ms > 100.0) {
        printf("[Worker %d] Slow %s: %.2f ms\n", worker_id, operation, duration_ms);
    }
}