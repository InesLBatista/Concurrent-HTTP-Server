#ifndef WORKER_H
#define WORKER_H

#include <stddef.h>
#include <time.h>
#include <pthread.h>

long get_time_diff_ms(struct timespec start, struct timespec end);
void get_client_ip(int client_fd, char *ip_buffer, size_t buffer_len);
const char *get_mime_type(const char *path);
void handle_client(int client_socket);

void start_worker_process(int ipc_socket);

#endif