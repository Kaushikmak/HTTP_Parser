#include "http_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BUFFER_SIZE 4096

void parse_http_request(char *buffer, int bytes_received, http_request *req) {
    // start with a clean slate
    memset(req, 0, sizeof(http_request));

    // RFC 2616 Section 4.1: Ignore leading CRLFs
    // just skipping empty lines at the very beginning
    char *start = buffer;
    while (strncmp(start, "\r\n", 2) == 0) {
        start += 2;
    }

    // find the empty line b/w header and body delimited by \r\n\r\n
    char *header_end = strstr(start, "\r\n\r\n");
    if (!header_end) {
        printf("[!] Invalid request: No \\r\\n\\r\\n delimiter found.\n");
        return;
    }

    // grab the very first line (Request-Line like "GET / HTTP/1.1")
    char *line = strtok(start, "\r\n");
    if (line) {
        sscanf(line, "%15s %255s %15s", req->method, req->path, req->version);
        
        // RFC 2616 Section 5.2: If Request-URI is an absoluteURI, host is part of it.
        // Scheme names are case-insensitive (Section 3.2.3)
        // if they gave us a full URL, we don't strictly need a separate Host header
        if (strncasecmp(req->path, "http://", 7) == 0 || strncasecmp(req->path, "https://", 8) == 0) {
            req->has_host = 1;
        }
    }

    // loop through the rest of the headers one by one
    while ((line = strtok(NULL, "\r\n")) != NULL) {
        // stop if we've reached the empty line b/w header and body
        if (line >= header_end) break;
        if (strncasecmp(line, "Content-Length:", 15) == 0) {
            req->content_length = atoi(line + 15);
        } else if (strncasecmp(line, "Host:", 5) == 0) {
            req->has_host = 1;
        }
    }

    // body starts right after the \r\n\r\n delimiter (which is 4 bytes long)
    req->body = header_end + 4;
}

void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    // read whatever the client sent us
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }

    // strtok changes original buffer,keep a backup copy
    // so we don't ruin the body payload
    char raw_copy[BUFFER_SIZE];
    memcpy(raw_copy, buffer, bytes_read);
    raw_copy[bytes_read] = '\0';

    http_request req;
    parse_http_request(buffer, bytes_read, &req);

    // recover the body pointer from our HOLY backup copy
    // again, looking for that empty line b/w header and body delimited by \r\n\r\n
    char *boundary = strstr(raw_copy, "\r\n\r\n");
    if (boundary) req.body = boundary + 4;

    printf("\n--- Parsed HTTP Request ---\n");
    printf("Method:         %s\n", req.method);
    printf("Path:           %s\n", req.path);
    printf("Version:        %s\n", req.version);
    printf("Content-Length: %d\n", req.content_length);

    if (req.content_length > 0) {
        printf("Body Payload:\n%.*s\n", req.content_length, req.body);
    } else {
        printf("Body Payload:   [None]\n");
    }
    printf("---------------------------\n");

    // RFC 2616 Section 9 and 14.23: Host header MUST accompany all HTTP/1.1 requests.
    // HTTP/1.1 requires a Host header, so yell at them if they forgot it
    if (strcmp(req.version, "HTTP/1.1") == 0 && !req.has_host) {
        const char *bad_resp = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\nMissing Host header\n";
        write(client_fd, bad_resp, strlen(bad_resp));
        close(client_fd);
        return;
    }

    // import html
    FILE *file = fopen("index.html", "rb");
    if (!file) {
        const char *not_found = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\nFile not found\n";
        write(client_fd, not_found, strlen(not_found));
        close(client_fd);
        return;
    }

    // find out how big the file is so we can set Content-Length
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *file_buf = malloc(file_size);
    if (!file_buf) {
        fclose(file);
        close(client_fd);
        return;
    }
    fread(file_buf, 1, file_size, file);
    fclose(file);

    // blast back a happy response header
    char response_header[1024];
    int header_len = snprintf(response_header, sizeof(response_header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n", // empty line b/w header and body delimited by \r\n\r\n
        file_size);

    write(client_fd, response_header, header_len);
    // send the actual file contents (the body)
    write(client_fd, file_buf, file_size);
    
    free(file_buf);
    
    // hang up on the client
    close(client_fd);
}

void start_server(int port) {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int opt = 1; // used for SO_REUSEADDR
    socklen_t addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // lets us restart the server quickly without getting "Address already in use"
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // listen on all interfaces
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("HTTP/1.1 server listening on http://localhost:%d\n", port);

    // infinite loop handling one client at a time
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (client_fd < 0) {
            perror("accept failed");
            continue;
        }
        handle_client(client_fd);
    }

    close(server_fd);
}
