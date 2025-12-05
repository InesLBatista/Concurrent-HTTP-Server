#include "http.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>  /* ADICIONADO: Para ENOENT e outras constantes de erro */

const char* get_mime_type(const char* filename) {
    const char* ext = strrchr(filename, '.');
    if (!ext) return "application/octet-stream";
    
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) {
        return "text/html";
    } else if (strcmp(ext, ".css") == 0) {
        return "text/css";
    } else if (strcmp(ext, ".js") == 0) {
        return "application/javascript";
    } else if (strcmp(ext, ".png") == 0) {
        return "image/png";
    } else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) {
        return "image/jpeg";
    } else if (strcmp(ext, ".gif") == 0) {
        return "image/gif";
    } else if (strcmp(ext, ".pdf") == 0) {
        return "application/pdf";
    } else if (strcmp(ext, ".txt") == 0) {
        return "text/plain";
    } else if (strcmp(ext, ".json") == 0) {
        return "application/json";
    } else if (strcmp(ext, ".xml") == 0) {
        return "application/xml";
    } else {
        return "application/octet-stream";
    }
}

int parse_http_request(const char* buffer, http_request_t* req) {
    /* Find end of first line */
    const char* line_end = strstr(buffer, "\r\n");
    if (!line_end) return -1;
    
    /* Copy first line */
    char first_line[1024];
    size_t len = line_end - buffer;
    if (len >= sizeof(first_line)) len = sizeof(first_line) - 1;
    
    strncpy(first_line, buffer, len);
    first_line[len] = '\0';
    
    /* Parse method, path, version */
    if (sscanf(first_line, "%15s %511s %15s", 
               req->method, req->path, req->version) != 3) {
        return -1;
    }
    
    /* Decode URL-encoded characters */
    char* decoded_path = req->path;
    char* src = req->path;
    char* dst = req->path;
    
    while (*src) {
        if (*src == '%' && isxdigit(src[1]) && isxdigit(src[2])) {
            char hex[3] = {src[1], src[2], '\0'};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    
    /* Check for query string */
    char* query = strchr(req->path, '?');
    if (query) *query = '\0';
    
    /* Handle directory requests */
    if (req->path[strlen(req->path)-1] == '/') {
        strcat(req->path, "index.html");
    }
    
    return 0;
}

void send_http_response(int fd, int status, const char* status_msg,
                       const char* content_type, const char* body, 
                       size_t body_len, const char* server_name) {
    char header[2048];
    time_t now = time(NULL);
    struct tm* tm_info = gmtime(&now);
    char date_buf[64];
    
    strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %H:%M:%S GMT", tm_info);
    
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Date: %s\r\n"
        "Server: %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_msg, date_buf, server_name, 
        content_type, body_len);
    
    send(fd, header, header_len, 0);
    
    if (body && body_len > 0) {
        send(fd, body, body_len, 0);
    }
}

void send_http_error(int fd, int status, const char* message,
                    server_config_t* config) {
    char body[2048];
    const char* status_msg;
    
    switch (status) {
        case 400: status_msg = "Bad Request"; break;
        case 403: status_msg = "Forbidden"; break;
        case 404: status_msg = "Not Found"; break;
        case 500: status_msg = "Internal Server Error"; break;
        case 503: status_msg = "Service Unavailable"; break;
        default: status_msg = "Error"; break;
    }
    
    int body_len = snprintf(body, sizeof(body),
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head><title>%d %s</title></head>\n"
        "<body>\n"
        "<h1>%d %s</h1>\n"
        "<p>%s</p>\n"
        "<hr>\n"
        "<p>%s</p>\n"
        "</body>\n"
        "</html>\n",
        status, status_msg, status, status_msg, message, config->server_name);
    
    send_http_response(fd, status, status_msg, "text/html", 
                       body, body_len, config->server_name);
}

void serve_file(int fd, const char* filepath, server_config_t* config,
                int head_only) {
    struct stat st;
    
    if (stat(filepath, &st) < 0) {
        if (errno == ENOENT) {
            send_http_error(fd, 404, "File not found", config);
        } else {
            send_http_error(fd, 403, "Access forbidden", config);
        }
        return;
    }
    
    /* Check if it's a directory */
    if (S_ISDIR(st.st_mode)) {
        char index_path[1024];
        snprintf(index_path, sizeof(index_path), "%s/index.html", filepath);
        
        if (stat(index_path, &st) == 0) {
            serve_file(fd, index_path, config, head_only);
        } else {
            send_http_error(fd, 403, "Directory listing not supported", config);
        }
        return;
    }
    
    /* Check file permissions */
    if (!(st.st_mode & S_IRUSR)) {
        send_http_error(fd, 403, "Access forbidden", config);
        return;
    }
    
    /* Open file */
    int file_fd = open(filepath, O_RDONLY);
    if (file_fd < 0) {
        send_http_error(fd, 500, "Could not open file", config);
        return;
    }
    
    /* Get MIME type */
    const char* mime_type = get_mime_type(filepath);
    
    /* Send headers */
    send_http_response(fd, 200, "OK", mime_type, NULL, st.st_size, 
                      config->server_name);
    
    /* Send file content (if not HEAD request) */
    if (!head_only) {
        char buffer[8192];
        ssize_t bytes_read;
        
        while ((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0) {
            send(fd, buffer, bytes_read, 0);
        }
    }
    
    close(file_fd);
}

void process_http_request(int client_fd, server_config_t* config,
                         shared_data_t* shared_data,
                         semaphores_t* semaphores) {
    char buffer[8192];
    ssize_t bytes_read;
    
    /* Read request */
    bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }
    
    buffer[bytes_read] = '\0';
    
    /* Parse request */
    http_request_t req;
    if (parse_http_request(buffer, &req) != 0) {
        send_http_error(client_fd, 400, "Bad Request", config);
        log_request(semaphores, "unknown", "UNKNOWN", "/", 400, 0);
        update_statistics(shared_data, semaphores, 400, 0);
        return;
    }
    
    /* Check supported methods */
    if (strcmp(req.method, "GET") != 0 && strcmp(req.method, "HEAD") != 0) {
        send_http_error(client_fd, 501, "Not Implemented", config);
        log_request(semaphores, "unknown", req.method, req.path, 501, 0);
        update_statistics(shared_data, semaphores, 501, 0);
        return;
    }
    
    /* Build file path */
    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s%s", 
             config->document_root, req.path);
    
    /* Check for path traversal attacks */
    if (strstr(req.path, "..") != NULL) {
        send_http_error(client_fd, 403, "Forbidden", config);
        log_request(semaphores, "unknown", req.method, req.path, 403, 0);
        update_statistics(shared_data, semaphores, 403, 0);
        return;
    }
    
    /* Get file size for logging */
    struct stat st;
    size_t file_size = 0;
    if (stat(filepath, &st) == 0) {
        file_size = st.st_size;
    }
    
    /* Serve file */
    int head_only = (strcmp(req.method, "HEAD") == 0);
    serve_file(client_fd, filepath, config, head_only);
    
    /* Log request (simplified - would get client IP in real implementation) */
    log_request(semaphores, "127.0.0.1", req.method, req.path, 200, 
                head_only ? 0 : file_size);
    
    /* Update statistics */
    update_statistics(shared_data, semaphores, 200, 
                     head_only ? 0 : file_size);
}