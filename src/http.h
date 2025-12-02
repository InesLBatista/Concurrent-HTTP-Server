// Inês Batista, 124877
// Maria Quinteiro, 124996

#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>     // Para size_t - necessário para body_len

// Estrutura para representar um pedido HTTP
// Guarda o método, caminho e versão do protocolo
typedef struct {
    char method[16];    // Método HTTP: "GET", "POST", "HEAD"
    char path[512];     // Caminho do recurso
    char version[16];   // Versão do protocolo: "HTTP/1.1", "HTTP/1.0"
} http_request_t;

// Função para analisar um pedido HTTP recebido do cliente
// Recebe o buffer com dados do cliente e preenche a estrutura http_request_t
// Retorna 0 em sucesso, -1 se o pedido estiver mal formado
int parse_http_request(const char* buffer, http_request_t* req);

// Função para enviar uma resposta HTTP para o cliente
// fd: descritor do socket do cliente
// status: código de status HTTP
// status_msg: mensagem correspondente ao status
// content_type: tipo MIME do conteúdo 
// body: conteúdo da resposta (pode ser NULL para respostas sem corpo)
// body_len: tamanho do conteúdo em bytes
void send_http_response(int fd, int status, const char* status_msg,
                       const char* content_type, const char* body, size_t body_len);

// Função para determinar o tipo MIME baseado na extensão do ficheiro
// Recebe o nome do ficheiro e retorna o tipo MIME correspondente
const char* get_mime_type(const char* filename);



// Lê o ficheiro 404.html da pasta de erros e envia como resposta
void send_404_response(int client_fd, const char* document_root);

// Lê o ficheiro 500.html da pasta de erros e envia como resposta
void send_500_response(int client_fd, const char* document_root);

// Lê o ficheiro 403.html da pasta de erros e envia como resposta
void send_403_response(int client_fd, const char* document_root);

// Lê o ficheiro 503.html da pasta de erros e envia como resposta
void send_503_response(int client_fd, const char* document_root);


// Função auxiliar para ler um ficheiro de erro
// Lê o conteúdo de um ficheiro HTML de erro e devolve-o em buffer alocado
// Retorna ponteiro para o conteúdo ou NULL em caso de erro
char* read_error_file(const char* document_root, const char* error_file);

#endif 