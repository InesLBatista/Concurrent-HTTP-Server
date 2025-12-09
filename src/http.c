#include <stdio.h>      
#include <string.h>     
#include <sys/socket.h> 
#include <time.h>
#include "http.h"

/*
 * Parse HTTP Request
 * Purpose: Extracts the Method (GET/POST), Path, and HTTP Version from the 
 * raw request buffer. It only parses the first line (Request Line) as that 
 * is sufficient for this server's static file serving needs.
 *
 * Parameters:
 * - buffer: The raw data received from the client socket.
 * - req: Pointer to the http_request_t struct to populate.
 *
 * Return:
 * - 0 on successful parsing.
 * - -1 if the request line is malformed or incomplete.
 */
int parse_http_request(const char *buffer, http_request_t *req)
{
    /* Find the end of the request line (marked by CRLF) */
    char *line_end = strstr(buffer, "\r\n");

    if (!line_end)
        return -1; /* Incomplete request */

    /* Copy the first line to a temporary buffer for safe parsing */
    char first_line[1024];
    size_t len = line_end - buffer;
    
    /* Ensure we don't overflow the temp buffer */
    if (len >= sizeof(first_line))
        len = sizeof(first_line) - 1;
        
    strncpy(first_line, buffer, len);
    first_line[len] = '\0';

    /* * Extract the three standard components:
     * 1. Method (e.g., "GET")
     * 2. Path (e.g., "/index.html")
     * 3. Version (e.g., "HTTP/1.1")
     */
    if (sscanf(first_line, "%s %s %s", req->method, req->path, req->version) != 3)
    {
        return -1;
    }

    return 0;
}

/*
 * Send HTTP Response
 * Purpose: Constructs a valid HTTP/1.1 response header including the status line,
 * date, content type, and length, then sends it followed by the actual body.
 *
 * Parameters:
 * - fd: The client socket file descriptor.
 * - status: HTTP status code (e.g., 200, 404).
 * - status_msg: Text description of the status (e.g., "OK", "Not Found").
 * - content_type: MIME type of the body (e.g., "text/html").
 * - body: Pointer to the content data (can be NULL for HEAD requests).
 * - body_len: Size of the body content in bytes.
 */
void send_http_response(int fd, int status, const char *status_msg, const char *content_type, const char *body, size_t body_len)
{
    /* 1. Generate current time in HTTP-compliant GMT format (RFC 1123) */
    time_t now = time(NULL);
    struct tm tm_data;
    gmtime_r(&now, &tm_data); /* Thread-safe GMT conversion */

    char date_str[128];
    strftime(date_str, sizeof(date_str), "%a, %d %b %Y %H:%M:%S GMT", &tm_data);

    /* 2. Format the HTTP Response Header */
    char header[2048];
    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 %d %s\r\n"
                              "Date: %s\r\n"                 
                              "Content-Type: %s\r\n"
                              "Content-Length: %zu\r\n"
                              "Server: ConcurrentHTTP/1.0\r\n"
                              "Connection: close\r\n"
                              "\r\n", /* End of headers */
                              status, status_msg, 
                              date_str,                     
                              content_type, body_len);

    /* 3. Send Headers */
    send(fd, header, header_len, 0);

    /* 4. Send Body (if present) */
    if (body && body_len > 0)
    {
        send(fd, body, body_len, 0);
    }
}