#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

typedef struct {
    char method[16];
    char path[256];
    char version[16];
    int content_length;
    int has_host;
    char *body;
} http_request;

void parse_http_request(char *buffer, int bytes_received, http_request *req);
void start_server(int port);

#endif
