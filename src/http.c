// http.c
// Inês Batista, Maria Quinteiro

// Implementa parsing de pedidos HTTP e construção de respostas.
// parse_http_request extrai método, path e versão da primeira linha HTTP.
// send_http_response constrói cabeçalhos HTTP completos com suporte para
// diferentes status codes e tipos de conteúdo.

#include "http.h"
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>

int parse_http_request(const char* buffer, http_request_t* req) {
    char* line_end = strstr(buffer, "\r\n");
    if (!line_end) return -1;
    
    char first_line[1024];
    size_t len = line_end - buffer;
    strncpy(first_line, buffer, len);
    first_line[len] = '\0';
    
    if (sscanf(first_line, "%s %s %s", req->method, req->path, req->version) != 3) {
        return -1;
    }
    return 0;
}

void send_http_response(int fd, int status, const char* status_msg,
                       const char* content_type, const char* body, size_t body_len) {
    char header[2048];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Server: ConcurrentHTTP/1.0\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_msg, content_type, body_len);
    
    send(fd, header, header_len, 0);
    if (body && body_len > 0) {
        send(fd, body, body_len, 0);
    }
}