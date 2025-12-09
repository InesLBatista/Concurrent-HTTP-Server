#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>

#define SERVER_PORT 8080
#define SERVER_IP "127.0.0.1"
#define NUM_THREADS 10
#define REQUESTS_PER_THREAD 50

typedef struct {
    int thread_id;
    int success_count;
    int fail_count;
} thread_stats_t;

/*
 * Helper: Connect to Server
 */
int connect_to_server() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(sock);
        return -1;
    }

    return sock;
}

/*
 * Helper: Send Request and Receive Response
 */
int perform_request(const char *path) {
    int sock = connect_to_server();
    if (sock < 0) return 0;

    char request[512];
    snprintf(request, sizeof(request), "GET %s HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n", path);

    if (send(sock, request, strlen(request), 0) < 0) {
        close(sock);
        return 0;
    }

    char buffer[4096];
    int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    close(sock);

    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        /* Check for 200 OK */
        if (strstr(buffer, "HTTP/1.1 200 OK")) {
            return 1;
        }
    }
    return 0;
}

/*
 * Worker Thread Function
 */
void *test_thread(void *arg) {
    thread_stats_t *stats = (thread_stats_t *)arg;
    
    for (int i = 0; i < REQUESTS_PER_THREAD; i++) {
        /* Alternate between two files to test cache thrashing/consistency */
        const char *path = (i % 2 == 0) ? "/index.html" : "/style.css";
        
        if (perform_request(path)) {
            stats->success_count++;
        } else {
            stats->fail_count++;
        }
        
        /* Small random delay to vary concurrency patterns */
        usleep(rand() % 1000);
    }
    return NULL;
}

int main() {
    printf("Starting Concurrent Consistency Test...\n");
    printf("Threads: %d, Requests/Thread: %d\n", NUM_THREADS, REQUESTS_PER_THREAD);

    pthread_t threads[NUM_THREADS];
    thread_stats_t stats[NUM_THREADS];
    
    srand(time(NULL));

    for (int i = 0; i < NUM_THREADS; i++) {
        stats[i].thread_id = i;
        stats[i].success_count = 0;
        stats[i].fail_count = 0;
        if (pthread_create(&threads[i], NULL, test_thread, &stats[i]) != 0) {
            perror("pthread_create");
            return 1;
        }
    }

    int total_success = 0;
    int total_fail = 0;

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
        total_success += stats[i].success_count;
        total_fail += stats[i].fail_count;
    }

    printf("\nTest Completed.\n");
    printf("Total Requests: %d\n", total_success + total_fail);
    printf("Success: %d\n", total_success);
    printf("Failed:  %d\n", total_fail);

    if (total_fail == 0) {
        printf("✓ PASSED: No dropped connections or errors.\n");
        return 0;
    } else {
        printf("✗ FAILED: %d errors detected.\n", total_fail);
        return 1;
    }
}
