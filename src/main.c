// Inês Batista, 124877
// Maria Quinteiro, 124996

#include "http.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>

// ============================================================================
// FUNÇÃO: parse_http_request()
// ============================================================================
int parse_http_request(const char* buffer, http_request_t* req) {
    char* line_end = strstr(buffer, "\r\n");
    if (!line_end) return -1;

    char first_line[1024];
    size_t len = line_end - buffer;
    strncpy(first_line, buffer, len);
    first_line[len] = '\0';

    if (sscanf(first_line, "%15s %511s %15s", 
               req->method, req->path, req->version) != 3) {
        return -1;
    }
    
    return 0;
}

// ============================================================================
// FUNÇÃO: send_http_response()
// ============================================================================
void send_http_response(int fd, int status, const char* status_msg,
                       const char* content_type, const char* body, size_t body_len) {
    char header[2048];
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char date_buffer[64];
    
    strftime(date_buffer, sizeof(date_buffer), "%a, %d %b %Y %H:%M:%S %Z", tm_info);
    
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Server: ConcurrentHTTP/1.0\r\n"
        "Date: %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_msg, date_buffer, content_type, body_len);
    
    send(fd, header, header_len, 0);
    
    if (body && body_len > 0) {
        send(fd, body, body_len, 0);
    }
}

// ============================================================================
// FUNÇÃO: get_mime_type()
// ============================================================================
const char* get_mime_type(const char* filename) {
    const char* ext = strrchr(filename, '.');
    if (!ext) return "text/plain";
    
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) 
        return "text/html; charset=utf-8";
    if (strcmp(ext, ".css") == 0) 
        return "text/css";
    if (strcmp(ext, ".js") == 0) 
        return "application/javascript";
    if (strcmp(ext, ".json") == 0) 
        return "application/json";
    if (strcmp(ext, ".png") == 0) 
        return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) 
        return "image/jpeg";
    if (strcmp(ext, ".gif") == 0) 
        return "image/gif";
    if (strcmp(ext, ".ico") == 0) 
        return "image/x-icon";
    if (strcmp(ext, ".txt") == 0) 
        return "text/plain; charset=utf-8";
    if (strcmp(ext, ".pdf") == 0) 
        return "application/pdf";
    
    return "application/octet-stream";
}

// ============================================================================
// FUNÇÃO: load_error_page()
// ============================================================================
static int load_error_page(const char* document_root, const char* error_file,
                          char** content, size_t* size) {
    char filepath[512];
    FILE* file = NULL;
    struct stat file_stat;
    
    snprintf(filepath, sizeof(filepath), "%s/errors/%s", document_root, error_file);
    
    if (stat(filepath, &file_stat) != 0) {
        return -1;
    }
    
    if (!S_ISREG(file_stat.st_mode)) {
        return -1;
    }
    
    file = fopen(filepath, "rb");
    if (!file) {
        return -1;
    }
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size <= 0) {
        fclose(file);
        return -1;
    }
    
    *content = malloc(file_size + 1);
    if (!*content) {
        fclose(file);
        return -1;
    }
    
    size_t bytes_read = fread(*content, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != (size_t)file_size) {
        free(*content);
        *content = NULL;
        return -1;
    }
    
    (*content)[file_size] = '\0';
    *size = file_size;
    
    return 0;
}

// ============================================================================
// FUNÇÃO: send_error_response()
// ============================================================================
void send_error_response(int client_fd, int status, const char* status_msg,
                        const char* document_root, const char* error_file) {
    char* error_content = NULL;
    size_t error_size = 0;
    
    // Tentar carregar página de erro personalizada
    if (load_error_page(document_root, error_file, &error_content, &error_size) == 0) {
        send_http_response(client_fd, status, status_msg, 
                          "text/html; charset=utf-8", error_content, error_size);
        free(error_content);
        return;
    }
    
    // Fallback: página de erro padrão
    char default_error[1024];
    int default_size = snprintf(default_error, sizeof(default_error),
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <title>%d %s</title>\n"
        "    <style>\n"
        "        body { font-family: Arial, sans-serif; padding: 40px; text-align: center; }\n"
        "        h1 { color: #333; margin-bottom: 20px; }\n"
        "        .error-code { font-size: 72px; color: #666; margin: 0; }\n"
        "        .error-message { font-size: 24px; color: #888; margin: 20px 0; }\n"
        "        .server-info { color: #aaa; font-size: 14px; margin-top: 40px; }\n"
        "    </style>\n"
        "</head>\n"
        "<body>\n"
        "    <h1 class=\"error-code\">%d</h1>\n"
        "    <h2 class=\"error-message\">%s</h2>\n"
        "    <p>Concurrent HTTP Server</p>\n"
        "    <div class=\"server-info\">\n"
        "        <p>Authors: Inês Batista (124877) & Maria Quinteiro (124996)</p>\n"
        "        <p>Sistemas Operativos 2024/2025</p>\n"
        "    </div>\n"
        "</body>\n"
        "</html>",
        status, status_msg, status, status_msg);
    
    send_http_response(client_fd, status, status_msg, 
                      "text/html; charset=utf-8", default_error, default_size);
}