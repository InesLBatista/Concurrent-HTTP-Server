// http.h
// Inês Batista, Maria Quinteiro

// Define estruturas e funções para parsing de pedidos HTTP e construção
// de respostas. Suporta métodos GET e HEAD com parsing da primeira linha
// do pedido HTTP.

#ifndef HTTP_H
#define HTTP_H

typedef struct {
    char method[16];
    char path[512];
    char version[16];
} http_request_t;

int parse_http_request(const char* buffer, http_request_t* req);
void send_http_response(int fd, int status, const char* status_msg,
                       const char* content_type, const char* body, size_t body_len);

#endif