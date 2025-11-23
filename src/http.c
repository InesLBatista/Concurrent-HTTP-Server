// http.c
// Inês Batista, Maria Quinteiro

// Implementa parsing completo de pedidos HTTP e serving de ficheiros estáticos.
// Inclui deteção de MIME types, gestão de diretórios e respostas de erro.

#include "http.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/sendfile.h>

// Parseia a primeira linha de um pedido HTTP
// Buffer: conteúdo completo do pedido HTTP
// Req: estrutura onde guardar o pedido parseado
// Retorna: 0 em sucesso, -1 em erro de parsing
int parse_http_request(const char* buffer, http_request_t* req) {
    // Encontra fim da primeira linha (CRLF)
    char* line_end = strstr(buffer, "\r\n");
    if (!line_end) {
        return -1;  // Formato HTTP inválido
    }
    
    // Extrai primeira linha
    char first_line[1024];
    size_t len = line_end - buffer;
    strncpy(first_line, buffer, len);
    first_line[len] = '\0';
    
    // Parseia método, path e versão
    if (sscanf(first_line, "%15s %511s %15s", 
               req->method, req->path, req->version) != 3) {
        return -1;  // Linha mal formada
    }
    
    return 0;  // Sucesso
}

// Valida se o método HTTP é suportado
// Method: método HTTP a validar
// Retorna: 1 se suportado, 0 se não suportado
int validate_http_method(const char* method) {
    return (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0);
}

// Constrói caminho completo no filesystem baseado no document root e path do pedido
// Document_root: diretório base dos ficheiros (ex: ./www)
// Request_path: path do pedido HTTP (ex: /index.html)
// Full_path: buffer onde guardar o caminho completo
void build_full_path(const char* document_root, const char* request_path, char* full_path) {
    // Concatena document root com request path
    snprintf(full_path, 1024, "%s%s", document_root, request_path);
    
    // Se o path terminar com '/', adiciona index.html
    if (full_path[strlen(full_path) - 1] == '/') {
        strncat(full_path, "index.html", 1024 - strlen(full_path) - 1);
    }
}

// Envia resposta HTTP completa
// Fd: descritor de socket do cliente
// Status: código de status HTTP (200, 404, etc.)
// Status_msg: mensagem de status ("OK", "Not Found", etc.)
// Content_type: tipo MIME do conteúdo
// Body: conteúdo da resposta (pode ser NULL para HEAD)
// Body_len: tamanho do conteúdo em bytes
void send_http_response(int fd, int status, const char* status_msg,
                       const char* content_type, const char* body, size_t body_len) {
    char header[2048];
    
    // Constrói cabeçalhos HTTP
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Server: ConcurrentHTTP/1.0\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_msg, content_type, body_len);
    
    // Envia cabeçalhos
    send(fd, header, header_len, 0);
    
    // Envia corpo (exceto para método HEAD)
    if (body && body_len > 0 && strstr(header, "HEAD") == NULL) {
        send(fd, body, body_len, 0);
    }
}

// Envia resposta de erro HTTP
// Fd: descritor de socket do cliente
// Status_code: código de erro HTTP (404, 403, 500, etc.)
void send_error_response(int fd, int status_code) {
    const char* status_msg = "";
    const char* body = "";
    size_t body_len = 0;
    
    // Define mensagem e corpo baseado no código de erro
    switch (status_code) {
        case 400:
            status_msg = "Bad Request";
            body = "<html><body><h1>400 Bad Request</h1></body></html>";
            break;
        case 403:
            status_msg = "Forbidden";
            body = "<html><body><h1>403 Forbidden</h1></body></html>";
            break;
        case 404:
            status_msg = "Not Found";
            body = "<html><body><h1>404 Not Found</h1></body></html>";
            break;
        case 500:
            status_msg = "Internal Server Error";
            body = "<html><body><h1>500 Internal Server Error</h1></body></html>";
            break;
        default:
            status_code = 500;
            status_msg = "Internal Server Error";
            body = "<html><body><h1>500 Internal Server Error</h1></body></html>";
            break;
    }
    
    body_len = strlen(body);
    send_http_response(fd, status_code, status_msg, "text/html", body, body_len);
}

// Envia um ficheiro como resposta HTTP
// Fd: descritor de socket do cliente
// File_path: caminho para o ficheiro a servir
void send_file_response(int fd, const char* file_path) {
    // Abre ficheiro para leitura
    FILE* file = fopen(file_path, "rb");
    if (!file) {
        send_error_response(fd, 404);
        return;
    }
    
    // Obtém tamanho do ficheiro
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Lê conteúdo do ficheiro
    char* file_content = malloc(file_size);
    if (!file_content) {
        fclose(file);
        send_error_response(fd, 500);
        return;
    }
    
    fread(file_content, 1, file_size, file);
    fclose(file);
    
    // Obtém MIME type e envia resposta
    const char* mime_type = get_mime_type(file_path);
    send_http_response(fd, 200, "OK", mime_type, file_content, file_size);
    
    free(file_content);
}

// Determina MIME type baseado na extensão do ficheiro
// Filename: nome do ficheiro
// Retorna: string com MIME type
const char* get_mime_type(const char* filename) {
    const char* ext = strrchr(filename, '.');
    if (!ext) return "application/octet-stream";
    
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0)
        return "text/html";
    else if (strcmp(ext, ".css") == 0)
        return "text/css";
    else if (strcmp(ext, ".js") == 0)
        return "application/javascript";
    else if (strcmp(ext, ".png") == 0)
        return "image/png";
    else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    else if (strcmp(ext, ".gif") == 0)
        return "image/gif";
    else if (strcmp(ext, ".txt") == 0)
        return "text/plain";
    else
        return "application/octet-stream";
}

// Verifica se o path é um diretório
// Path: caminho a verificar
// Retorna: 1 se for diretório, 0 se não for
int is_directory(const char* path) {
    struct stat statbuf;
    if (stat(path, &statbuf) != 0)
        return 0;
    return S_ISDIR(statbuf.st_mode);
}

// Verifica se um ficheiro existe
// Path: caminho do ficheiro
// Retorna: 1 se existe, 0 se não existe
int file_exists(const char* path) {
    return access(path, F_OK) == 0;
}

// Obtém tamanho de um ficheiro
// Path: caminho do ficheiro
// Retorna: tamanho em bytes, ou 0 em erro
size_t get_file_size(const char* path) {
    struct stat statbuf;
    if (stat(path, &statbuf) != 0)
        return 0;
    return statbuf.st_size;
}