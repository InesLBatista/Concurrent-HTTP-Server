#ifndef HTTP_H
#define HTTP_H

#include "shared_mem.h"
#include "semaphores.h"
#include "config.h"

typedef struct {
    char method[16];
    char path[512];
    char version[16];
} http_request_t;

void process_http_request(int client_fd, server_config_t* config,
                         shared_data_t* shared_data,
                         semaphores_t* semaphores);
void send_http_error(int fd, int status, const char* message,
                    server_config_t* config);
const char* get_mime_type(const char* filename);

#endif