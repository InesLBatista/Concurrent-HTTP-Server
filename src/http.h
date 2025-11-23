// http.h
// Inês Batista, Maria Quinteiro

// Define estruturas e funções completas para parsing de pedidos HTTP e 
// construção de respostas. Suporta métodos GET e HEAD, serving de ficheiros
// estáticos, deteção de MIME types e gestão de diretórios.

#ifndef HTTP_H
#define HTTP_H

#include <sys/stat.h>

// Estrutura para representar um pedido HTTP parseado
typedef struct {
    char method[16];        // Método HTTP: GET, HEAD, etc.
    char path[512];         // Caminho do recurso solicitado
    char version[16];       // Versão HTTP: HTTP/1.0, HTTP/1.1
    char full_path[1024];   // Caminho completo no filesystem
} http_request_t;

// Funções de parsing e processamento
int parse_http_request(const char* buffer, http_request_t* req);
int validate_http_method(const char* method);
void build_full_path(const char* document_root, const char* request_path, char* full_path);

// Funções de resposta HTTP
void send_http_response(int fd, int status, const char* status_msg,
                       const char* content_type, const char* body, size_t body_len);
void send_error_response(int fd, int status_code);
void send_file_response(int fd, const char* file_path);

// Funções de utilidade
const char* get_mime_type(const char* filename);
int is_directory(const char* path);
int file_exists(const char* path);
size_t get_file_size(const char* path);

#endif