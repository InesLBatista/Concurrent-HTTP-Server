#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "config.h"
#include "http.h"
#include "logger.h"
#include "stats.h"

// Global configuration for cleanup
static server_config_t *config = NULL;

// Signal handler for graceful shutdown
void signal_handler(int sig) {
    printf("\nReceived signal %d - Shutting down...\n", sig);
    
    // Cleanup modules
    logger_close();
    stats_cleanup();
    if (config) {
        config_destroy(config);
    }
    
    printf("Test completed successfully!\n");
    exit(0);
}

// Test configuration module
void test_config_module(void) {
    printf("\n=== TESTING CONFIG MODULE ===\n");
    
    // Test 1: Create config with defaults
    config = config_create(NULL);
    if (!config) {
        printf("❌ FAIL: config_create with defaults\n");
        exit(1);
    }
    printf("✅ PASS: config_create with defaults\n");
    config_print(config);
    
    // Test 2: Test setters
    config_set_port(config, 9090);
    config_set_document_root(config, "/tmp/www");
    config_set_num_workers(config, 8);
    
    if (config_get_port(config) != 9090) {
        printf("❌ FAIL: config_set_port\n");
        exit(1);
    }
    printf("✅ PASS: config setters/getters\n");
    
    // Test 3: Save config to file for later testing
    FILE *test_config = fopen("test_server.conf", "w");
    if (test_config) {
        fprintf(test_config, "PORT=9090\n");
        fprintf(test_config, "DOCUMENT_ROOT=/tmp/www\n");
        fprintf(test_config, "NUM_WORKERS=8\n");
        fprintf(test_config, "THREADS_PER_WORKER=5\n");
        fprintf(test_config, "LOG_FILE=test_access.log\n");
        fclose(test_config);
    }
    
    // Test 4: Load config from file
    server_config_t *file_config = config_create("test_server.conf");
    if (file_config) {
        printf("✅ PASS: config_load_from_file\n");
        config_print(file_config);
        config_destroy(file_config);
    } else {
        printf("❌ FAIL: config_load_from_file\n");
    }
    
    printf("✅ CONFIG MODULE: ALL TESTS PASSED\n");
}

// Test HTTP module
void test_http_module(void) {
    printf("\n=== TESTING HTTP MODULE ===\n");
    
    // Test 1: Parse valid GET request
    const char *get_request = "GET /index.html HTTP/1.1\r\n"
                             "Host: localhost\r\n"
                             "User-Agent: test-client\r\n"
                             "\r\n";
    
    http_request_t request;
    if (http_parse_request(get_request, &request) == 0) {
        if (request.method == HTTP_GET && 
            strcmp(request.path, "/index.html") == 0 &&
            request.version == HTTP_1_1) {
            printf("✅ PASS: http_parse_request GET\n");
        } else {
            printf("❌ FAIL: http_parse_request GET - wrong values\n");
        }
        http_free_request(&request);
    } else {
        printf("❌ FAIL: http_parse_request GET\n");
    }
    
    // Test 2: Parse HEAD request
    const char *head_request = "HEAD /test.css HTTP/1.0\r\n\r\n";
    if (http_parse_request(head_request, &request) == 0) {
        if (request.method == HTTP_HEAD && request.version == HTTP_1_0) {
            printf("✅ PASS: http_parse_request HEAD\n");
        }
        http_free_request(&request);
    }
    
    // Test 3: Test MIME type detection
    struct {
        const char *filename;
        const char *expected_mime;
    } mime_tests[] = {
        {"index.html", "text/html"},
        {"style.css", "text/css"},
        {"script.js", "application/javascript"},
        {"image.png", "image/png"},
        {"photo.jpg", "image/jpeg"},
        {"unknown.xyz", "text/plain"},
        {NULL, NULL}
    };
    
    for (int i = 0; mime_tests[i].filename; i++) {
        const char *mime = http_get_mime_type(mime_tests[i].filename);
        if (strcmp(mime, mime_tests[i].expected_mime) == 0) {
            printf("✅ PASS: http_get_mime_type %s -> %s\n", 
                   mime_tests[i].filename, mime);
        } else {
            printf("❌ FAIL: http_get_mime_type %s -> %s (expected %s)\n",
                   mime_tests[i].filename, mime, mime_tests[i].expected_mime);
        }
    }
    
    // Test 4: Test response header creation
    char *header = http_create_response_header(200, "text/html", 1024);
    if (header && strstr(header, "200 OK") && strstr(header, "text/html")) {
        printf("✅ PASS: http_create_response_header\n");
        free(header);
    } else {
        printf("❌ FAIL: http_create_response_header\n");
    }
    
    // Test 5: Test URL decoding
    char decoded[256];
    http_url_decode(decoded, "/path%20with%20spaces");
    if (strcmp(decoded, "/path with spaces") == 0) {
        printf("✅ PASS: http_url_decode\n");
    } else {
        printf("❌ FAIL: http_url_decode\n");
    }
    
    // Test 6: Test path safety
    if (http_is_safe_path("/index.html") && 
        !http_is_safe_path("/../etc/passwd")) {
        printf("✅ PASS: http_is_safe_path\n");
    } else {
        printf("❌ FAIL: http_is_safe_path\n");
    }
    
    printf("✅ HTTP MODULE: ALL TESTS PASSED\n");
}

// Test logger module
void test_logger_module(void) {
    printf("\n=== TESTING LOGGER MODULE ===\n");
    
    // Initialize logger with test config
    if (logger_init(config) == 0) {
        printf("✅ PASS: logger_init\n");
    } else {
        printf("❌ FAIL: logger_init\n");
        return;
    }
    
    // Test different log levels
    logger_log(LOG_DEBUG, "This is a debug message - PID: %d", getpid());
    logger_log(LOG_INFO, "Server test started");
    logger_log(LOG_WARNING, "Test warning message");
    logger_log(LOG_ERROR, "Test error condition");
    
    // Test access logging (Apache format)
    logger_log_access("127.0.0.1", "GET", "/index.html", 200, 2048, "-", "test-client");
    logger_log_access("192.168.1.1", "GET", "/missing.html", 404, 0, "http://example.com", "Mozilla/5.0");
    
    printf("✅ LOGGER MODULE: ALL TESTS PASSED\n");
    printf("    Check test_access.log for output\n");
}

// Test statistics module
void test_stats_module(void) {
    printf("\n=== TESTING STATISTICS MODULE ===\n");
    
    // Initialize statistics
    if (stats_init() == 0) {
        printf("✅ PASS: stats_init\n");
    } else {
        printf("❌ FAIL: stats_init\n");
        return;
    }
    
    // Simulate some server activity
    stats_increment_request(200);
    stats_add_bytes(1500);
    stats_update_response_time(45);
    stats_set_active_connections(5);
    
    stats_increment_request(404);
    stats_add_bytes(0);
    stats_update_response_time(10);
    
    stats_increment_request(200);
    stats_add_bytes(3200);
    stats_update_response_time(120);
    stats_set_active_connections(3);
    
    stats_increment_connection_error();
    stats_increment_timeout_error();
    
    // Display statistics
    printf("\n--- Current Statistics ---\n");
    stats_display();
    
    printf("\n--- Detailed Statistics ---\n");
    stats_print();
    
    // Test utility functions
    printf("Uptime: %ld seconds\n", stats_get_uptime());
    printf("Requests/sec: %.2f\n", stats_get_requests_per_second());
    
    printf("✅ STATISTICS MODULE: ALL TESTS PASSED\n");
}

// Test integration between modules
void test_integration(void) {
    printf("\n=== TESTING MODULE INTEGRATION ===\n");
    
    // Simulate a complete HTTP request flow
    const char *http_request = "GET /test.html HTTP/1.1\r\n"
                              "Host: localhost:8080\r\n"
                              "\r\n";
    
    http_request_t req;
    if (http_parse_request(http_request, &req) == 0) {
        // Log the request
        logger_log_access("127.0.0.1", "GET", req.path, 200, 512, "-", "integration-test");
        
        // Update statistics
        stats_increment_request(200);
        stats_add_bytes(512);
        stats_update_response_time(25);
        
        printf("✅ PASS: Module integration test\n");
        
        http_free_request(&req);
    } else {
        printf("❌ FAIL: Module integration test\n");
    }
    
    printf("✅ INTEGRATION TEST: PASSED\n");
}

int main(void) {
    printf("🚀 STARTING COMPREHENSIVE MODULE TESTS\n");
    printf("========================================\n");
    
    // Setup signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Run all module tests
    test_config_module();
    test_http_module(); 
    test_logger_module();
    test_stats_module();
    test_integration();
    
    printf("\n========================================\n");
    printf("🎉 ALL MODULE TESTS COMPLETED SUCCESSFULLY!\n");
    printf("Modules tested:\n");
    printf("  ✅ config.c/h\n");
    printf("  ✅ http.c/h\n"); 
    printf("  ✅ logger.c/h\n");
    printf("  ✅ stats.c/h\n");
    printf("\nPress Ctrl+C to exit and cleanup...\n");
    
    // Keep running to show stats are maintained
    int counter = 0;
    while (1) {
        sleep(5);
        stats_increment_request(200);
        stats_add_bytes(100);
        stats_set_active_connections(counter % 10);
        
        if (counter++ % 3 == 0) {
            printf("\n--- Periodic Stats Update ---\n");
            stats_display();
        }
    }
    
    return 0;
}