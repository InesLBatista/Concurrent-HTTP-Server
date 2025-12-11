#define _POSIX_C_SOURCE 199309L 

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> 
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h> 
#include <sys/time.h> 

#include "http.h"
#include "config.h"
#include "shared_mem.h"
#include "thread_pool.h"
#include <sys/uio.h>
#include "logger.h"
#include "worker.h"
#include "cache.h"

/* Access global config and shared structures */
extern server_config_t config;
extern connection_queue_t *queue;

/*
 * Helper: Calculate Time Difference in Milliseconds
 * Purpose: Computes the elapsed time between two timespec structs.
 * Used for tracking request latency.
 */
long get_time_diff_ms(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
}

/*
 * Helper: Get Client IP Address
 * Purpose: Extracts the client's IP address string from the socket file descriptor.
 * This is used specifically for the access logs.
 */
void get_client_ip(int client_fd, char *ip_buffer, size_t buffer_len) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    if (getpeername(client_fd, (struct sockaddr *)&addr, &addr_len) == 0) {
        inet_ntop(AF_INET, &addr.sin_addr, ip_buffer, buffer_len);
    } else {
        strncpy(ip_buffer, "unknown", buffer_len);
    }
}

/*
 * Helper: Determine MIME Type
 * Purpose: Returns the correct Content-Type header based on the file extension.
 * Defaults to "application/octet-stream" for unknown types.
 */
const char *get_mime_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext)
        return "application/octet-stream";
    if (strcmp(ext, ".html") == 0)
        return "text/html";
    if (strcmp(ext, ".css") == 0)
        return "text/css";
    if (strcmp(ext, ".js") == 0)
        return "application/javascript";
    if (strcmp(ext, ".png") == 0)
        return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(ext, ".pdf") == 0)
        return "application/pdf";
    return "application/octet-stream";
}

/*
 * Handle Client Request (Core Logic)
 * Purpose: Processes a single HTTP request from start to finish.
 *
 * Workflow:
 * 1. Updates "Active Connections" stat.
 * 2. Reads and parses the HTTP request.
 * 3. Validates method (GET/HEAD only) and security (no ".." paths).
 * 4. Resolves the physical file path (handling index.html).
 * 5. Checks the In-Memory Cache (for small files).
 * 6. If not cached, reads from disk and populates the cache.
 * 7. Sends the HTTP response.
 * 8. Updates final stats and logs the request.
 *
 * - Uses shared memory semaphores to atomic updates to global stats.
 * - Uses cache_get/cache_put which handle their own Read-Write locks.
 */

/*
 * Helper: Send Error Page
 * Purpose: Serves a custom error page from www/errors/ if available.
 * Falls back to a hardcoded string if the file is missing.
 */
static void send_error_page(int client_fd, int status_code, const char *status_text, long *bytes_sent)
{
    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/errors/%d.html", config.document_root, status_code);

    struct stat st;
    if (stat(filepath, &st) == 0) {
        /* Serve custom error page */
        FILE *fp = fopen(filepath, "rb");
        if (fp) {
            char *buf = malloc(st.st_size);
            if (buf) {
                size_t rb = fread(buf, 1, st.st_size, fp);
                if (rb == (size_t)st.st_size) {
                    send_http_response(client_fd, status_code, status_text, "text/html", buf, st.st_size);
                    *bytes_sent = st.st_size;
                    free(buf);
                    fclose(fp);
                    return;
                }
                free(buf);
            }
            fclose(fp);
        }
    }

    /* Fallback: Hardcoded error message */
    char body[512];
    snprintf(body, sizeof(body), "<h1>%d %s</h1>", status_code, status_text);
    size_t len = strlen(body);
    send_http_response(client_fd, status_code, status_text, "text/html", body, len);
    *bytes_sent = len;
}

void handle_client(int client_socket)
{
    struct timespec start_time, end_time;
    
    /* 1. Increment Active Connections (Critical Section) */
    sem_wait(&stats->mutex);
    stats->active_connections++;
    sem_post(&stats->mutex);

    char client_ip[INET_ADDRSTRLEN];
    get_client_ip(client_socket, client_ip, sizeof(client_ip));

    /* Set Socket Timeout for Keep-Alive */
    struct timeval tv;
    tv.tv_sec = config.keep_alive_timeout > 0 ? config.keep_alive_timeout : 5;
    tv.tv_usec = 0;
    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &start_time);

        /* Read Request */
        char buffer[2048];
        ssize_t bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

        int status_code = 0;
        long bytes_sent = 0;
        http_request_t req = {0}; 

        if (bytes <= 0)
        {
            /* Connection closed or timeout */
            break; 
        }
        buffer[bytes] = '\0';

    if (parse_http_request(buffer, &req) != 0)
    {
        status_code = 400;
        send_error_page(client_socket, 400, "Bad Request", &bytes_sent);
        /* Don't close immediately, just break loop to cleanup */
        status_code = 400;
        goto update_stats_and_log; 
    }

    /* Validate Method (Only GET and HEAD supported) */
    int is_head = (strcmp(req.method, "HEAD") == 0);
    if (strcmp(req.method, "GET") != 0 && strcmp(req.method, "HEAD") != 0)
    {
        status_code = 405;
        send_error_page(client_socket, 405, "Method Not Allowed", &bytes_sent);
        status_code = 405;
        goto update_stats_and_log;
    }

    /* Security: Prevent Directory Traversal */
    if (strstr(req.path, ".."))
    {
        status_code = 403;
        send_error_page(client_socket, 403, "Forbidden", &bytes_sent);
        status_code = 403;
        goto update_stats_and_log;
    }

    /* Dashboard Stats Endpoint */
    if (strcmp(req.path, "/stats") == 0)
    {
        sem_wait(&stats->mutex);
        char json_body[1024];
        snprintf(json_body, sizeof(json_body),
            "{"
            "\"active_connections\": %d,"
            "\"total_requests\": %ld,"
            "\"bytes_transferred\": %ld,"
            "\"status_200\": %ld,"
            "\"status_404\": %ld,"
            "\"status_500\": %ld,"
            "\"avg_response_time_ms\": %ld"
            "}",
            stats->active_connections,
            stats->total_requests,
            stats->bytes_transferred,
            stats->status_200,
            stats->status_404,
            stats->status_500,
            (stats->total_requests > 0) ? (stats->average_response_time / stats->total_requests) : 0
        );
        sem_post(&stats->mutex);

        size_t len = strlen(json_body);
        send_http_response(client_socket, 200, "OK", "application/json", json_body, len);
        bytes_sent = len;
        status_code = 200;
        goto update_stats_and_log;
    }

    /* Range Request Support */
    long range_start = -1;
    long range_end = -1;
    char *range_header = strstr(buffer, "Range: bytes=");
    if (range_header) {
        range_header += 13; /* Skip "Range: bytes=" */
        char *dash = strchr(range_header, '-');
        if (dash) {
            *dash = '\0';
            range_start = atol(range_header);
            if (*(dash + 1) != '\r' && *(dash + 1) != '\n') {
                range_end = atol(dash + 1);
            }
            *dash = '-'; /* Restore buffer */
        }
    }

    /* Resolve Path with Virtual Host Support */
    char full_path[2048];
    char vhost_path[1024];
    int vhost_found = 0;

    /* Parse Host Header */
    char *host_header = strstr(buffer, "Host: ");
    if (host_header) {
        host_header += 6; /* Skip "Host: " */
        char *end = strchr(host_header, '\r');
        if (!end) end = strchr(host_header, '\n');
        if (end) {
            char host[256];
            size_t len = end - host_header;
            if (len > 255) len = 255;
            strncpy(host, host_header, len);
            host[len] = '\0';
            
            /* Remove port if present (e.g. localhost:8080 -> localhost) */
            char *colon = strchr(host, ':');
            if (colon) *colon = '\0';

            /* Check if directory exists: www/host */
            snprintf(vhost_path, sizeof(vhost_path), "%s/%s", config.document_root, host);
            struct stat st_vhost;
            if (stat(vhost_path, &st_vhost) == 0 && S_ISDIR(st_vhost.st_mode)) {
                snprintf(full_path, sizeof(full_path), "%s%s", vhost_path, req.path);
                vhost_found = 1;
            }
        }
    }

    if (!vhost_found) {
        snprintf(full_path, sizeof(full_path), "%s%s", config.document_root, req.path);
    }

    /* Directory Handling (Serve index.html) */
    struct stat st;
    if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode))
    {
        strncat(full_path, "/index.html", sizeof(full_path) - strlen(full_path) - 1);
    }

    /* File Existence Check */
    if (stat(full_path, &st) != 0) {
        status_code = 404;
        send_error_page(client_socket, 404, "Not Found", &bytes_sent);
        goto update_stats_and_log;
    }

    long fsize = st.st_size;
    char *content = NULL;
    size_t read_bytes = 0;

    /* * CACHING LOGIC
     * Only cache files smaller than 1MB to preserve memory.
     */
    if (fsize > 0 && fsize < (1 * 1024 * 1024)) {
        /* Try to retrieve from cache first */
        if (cache_get(full_path, &content, &read_bytes) == 0) {
            /* HIT: 'content' is now a malloc'd copy from the cache */
        } else {
            /* MISS: Read from disk */
            FILE *fp = fopen(full_path, "rb");
            if (!fp) {
                status_code = 404;
                send_error_page(client_socket, 404, "Not Found", &bytes_sent);
                goto update_stats_and_log;
            }
            char *buf = malloc(fsize);
            if (!buf) {
                fclose(fp);
                status_code = 500;
                send_error_page(client_socket, 500, "Internal Server Error", &bytes_sent);
                goto update_stats_and_log;
            }
            size_t rb = fread(buf, 1, fsize, fp);
            fclose(fp);
            
            if (rb != (size_t)fsize) {
                free(buf);
                status_code = 500;
                send_error_page(client_socket, 500, "Internal Server Error", &bytes_sent);
                goto update_stats_and_log;
            }
            read_bytes = rb;
            content = buf;

            /* Update Cache (Best Effort) */
            cache_put(full_path, content, read_bytes);
        }
    } else {
        /* Large files: Direct Disk Read (No Caching) */
        FILE *fp = fopen(full_path, "rb");
        if (!fp) {
            status_code = 404;
            send_error_page(client_socket, 404, "Not Found", &bytes_sent);
            goto update_stats_and_log;
        }
        char *buf = malloc(fsize);
        if (!buf) {
            fclose(fp);
            status_code = 500;
            send_error_page(client_socket, 500, "Internal Server Error", &bytes_sent);
            goto update_stats_and_log;
        }
        size_t rb = fread(buf, 1, fsize, fp);
        fclose(fp);
        if (rb != (size_t)fsize) {
            free(buf);
            status_code = 500;
            // ... send 500 ...
            close(client_socket);
            goto update_stats_and_log;
        }
        content = buf;
        read_bytes = rb;
    }

    /* Send Response */
    const char *mime = get_mime_type(full_path);
    
    if (range_start != -1) {
        /* Partial Content */
        if (range_end == -1 || range_end >= fsize) range_end = fsize - 1;
        long content_length = range_end - range_start + 1;
        
        status_code = 206;
        
        /* Construct Content-Range header */
        char extra_headers[128];
        snprintf(extra_headers, sizeof(extra_headers), "Content-Range: bytes %ld-%ld/%ld\r\n", range_start, range_end, fsize);
        
        /* Send Header */
        char header[1024];
        snprintf(header, sizeof(header), 
            "HTTP/1.1 206 Partial Content\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %ld\r\n"
            "%s"
            "Connection: keep-alive\r\n"
            "\r\n", mime, content_length, extra_headers);
        send(client_socket, header, strlen(header), 0);
        
        /* Send Body */
        if (!is_head) {
            if (content) {
                /* From Cache or Full Read */
                send(client_socket, content + range_start, content_length, 0);
            } else {
                /* Direct Disk Read for Range */
                /* Note: If we didn't read full file above, we need to re-open or seek. 
                   For simplicity in this architecture, we assumed full read for small files.
                   For large files (else block), we need to handle it. 
                */
                 /* Re-open for seek if content is NULL (Large file path) */
                 FILE *fp = fopen(full_path, "rb");
                 if (fp) {
                     fseek(fp, range_start, SEEK_SET);
                     char *chunk = malloc(content_length);
                     if (chunk) {
                         size_t rb = fread(chunk, 1, content_length, fp);
                         if (rb > 0) {
                            send(client_socket, chunk, rb, 0);
                         }
                         free(chunk);
                     }
                     fclose(fp);
                 }
            }
        }
        bytes_sent = content_length;
    }
    else {
        /* Normal 200 OK */
        status_code = 200;
        if (is_head)
        {
            send_http_response(client_socket, 200, "OK", mime, NULL, fsize);
            bytes_sent = 0;
        }
        else
        {
            send_http_response(client_socket, 200, "OK", mime, content, fsize);
            bytes_sent = fsize;
        }
    }

    free(content);
    /* close(client_socket); REMOVED for Keep-Alive */

/* * Cleanup Label: Updates stats and logs the request. 
 * Reached via goto from error handlers or normal completion.
 */
update_stats_and_log:
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    long elapsed_ms = get_time_diff_ms(start_time, end_time);

    /* Update Shared Stats (Critical Section) */
    sem_wait(&stats->mutex);
    /* stats->active_connections--; MOVED to end of connection */
    stats->total_requests++;
    stats->bytes_transferred += bytes_sent;
    stats->average_response_time += elapsed_ms;

    if (status_code == 200) stats->status_200++;
    else if (status_code == 404) stats->status_404++;
    else if (status_code == 500) stats->status_500++;
    
    sem_post(&stats->mutex);

    /* Log Request (Apache Format) */
    const char *log_method = (req.method[0] != '\0') ? req.method : "-";
    const char *log_path = (req.path[0] != '\0') ? req.path : "-";
    
    log_request(&queue->log_mutex, client_ip, log_method, log_path, status_code, bytes_sent);

    /* Check for Connection: close header to break loop */
    /* Simple check: if request contained "Connection: close" (not implemented in parser yet, assuming keep-alive by default) */
    /* For now, we rely on timeout or client closing. */
    } /* End of while(1) */

    close(client_socket);
    sem_wait(&stats->mutex);
    stats->active_connections--;
    sem_post(&stats->mutex);
}

/*
 * Receive a File Descriptor via UNIX Domain Socket
 * Purpose: Receives a file descriptor sent by another process. The kernel
 * will automatically add the FD to this process's file table and return
 * its new integer value via the ancillary data.
 *
 * Parameters:
 * - socket: The UNIX domain socket to receive from.
 *
 * Return:
 * - The new valid file descriptor on success.
 * - -1 on failure (recvmsg error or no FD received).
 */
static int recv_fd(int socket)
{
    struct msghdr msg = {0};

    /* Prepare buffer for the dummy byte */
    char buf[1] = {0};
    struct iovec io = {.iov_base = buf, .iov_len = 1};

    /* Union for alignment of receiving buffer */
    union
    {
        char buf[CMSG_SPACE(sizeof(int))];
        struct cmsghdr align;
    } u;

    memset(&u, 0, sizeof(u));

    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = u.buf;
    msg.msg_controllen = sizeof(u.buf);

    /* Perform the receive operation */
    if (recvmsg(socket, &msg, 0) < 0)
        return -1;

    /* Extract the FD from the ancillary data */
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    
    /* Verify we received the expected type of message (SCM_RIGHTS) */
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
    {
        /* Return the file descriptor integer */
        return *((int *)CMSG_DATA(cmsg));
    }
    
    return -1; /* Failed to receive a valid FD */
}

/*
 * Start Worker Process
 * Purpose: This is the main entry point for a Worker process. It initializes 
 * process-local resources (cache, thread pool, logger thread) and enters 
 * a loop to receive client connections from the Master process.
 *
 * Parameters:
 * - ipc_socket: The UNIX domain socket used to receive File Descriptors 
 * from the Master process.
 */
void start_worker_process(int ipc_socket)
{
    printf("Worker (PID: %d) started\n", getpid());

    /* Initialize time zone information for logging */
    tzset();
    
    /* * Initialize shared queue structures. 
     * Note: In this architecture, this primarily sets up the shared log_mutex 
     * needed for thread-safe logging across processes.
     */
    init_shared_queue(config.max_queue_size);

    /* * Start the Logger Flush Thread
     * This background thread ensures logs are written to disk periodically 
     * even if the buffer isn't full.
     */
    pthread_t flush_tid;
    if (pthread_create(&flush_tid, NULL, logger_flush_thread, (void *)&queue->log_mutex) != 0) {
        perror("Failed to create logger flush thread");
    }

    /* * Initialize Local Request Queue
     * This queue acts as the buffer between the Worker process (Main Thread) 
     * and its pool of worker threads.
     */
    local_queue_t local_q;
    if (local_queue_init(&local_q, config.max_queue_size) != 0) {
        perror("local_queue_init");
    }
    
    /* * Initialize File Cache
     * Sets up the in-memory LRU cache with the size defined in server.conf.
     */
    size_t cache_bytes = (size_t)config.cache_size_mb * 1024 * 1024;
    if (cache_init(cache_bytes) != 0) {
        perror("cache_init");
    }

    /* * Create Thread Pool
     * Spawns a fixed number of threads (consumer) that will block waiting 
     * for work on the local_q.
     */
    int thread_count = config.threads_per_worker > 0 ? config.threads_per_worker : 0;
    pthread_t *threads = NULL;
    if (thread_count > 0) {
        threads = malloc(sizeof(pthread_t) * thread_count);
        if (!threads) {
            perror("Failed to allocate worker threads array");
            thread_count = 0;
        }
    }

    int created = 0;
    for (int i = 0; i < thread_count; i++) {
        if (pthread_create(&threads[i], NULL, worker_thread, &local_q) != 0) {
            perror("pthread_create");
            break;
        }
        created++;
    }

    /* * Main Loop: Receive and Dispatch
     * 1. Block waiting for a File Descriptor from Master (IPC).
     * 2. Enqueue the FD into the local thread pool queue.
     */
    while (1)
    {
        int client_fd = recv_fd(ipc_socket);
        if (client_fd < 0) {
            /* IPC socket closed or error — begin shutdown sequence */
            break;
        }

        /* * Dispatch to Thread Pool
         * Try to add the client FD to the local queue. If the queue is full,
         * we reject the request immediately with 503 to prevent overload.
         */
        if (local_queue_enqueue(&local_q, client_fd) != 0) {
            fprintf(stderr, "[Worker %d] Queue full! Rejecting client.\n", getpid());
            
            long bytes_sent = 0;
            send_error_page(client_fd, 503, "Service Unavailable", &bytes_sent);

            close(client_fd);
        }
    }

    /* * === Graceful Shutdown Sequence === 
     */

    /* 1. Signal Worker Threads to Stop */
    /* Acquire lock to ensure condition broadcast is not missed by threads */
    pthread_mutex_lock(&local_q.mutex); 
    local_q.shutting_down = 1;
    pthread_cond_broadcast(&local_q.cond);
    pthread_mutex_unlock(&local_q.mutex);

    /* 2. Stop Logger Thread */
    logger_request_shutdown();
    pthread_join(flush_tid, NULL);

    /* 3. Join Worker Threads */
    for (int i = 0; i < created; i++) {
        pthread_join(threads[i], NULL);
    }

    /* 4. Cleanup Resources */
    if (threads) free(threads);
    local_queue_destroy(&local_q);
    cache_destroy();
    
    close(ipc_socket);
}