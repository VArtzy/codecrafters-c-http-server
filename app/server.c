#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zlib.h>
static char directory[256];
int compressToGzip(const char *input, int inputSize, char *output,
        int outputSize) {
    z_stream zs = {0};
    zs.zalloc = Z_NULL;
    zs.zfree = Z_NULL;
    zs.opaque = Z_NULL;
    zs.avail_in = (uInt)inputSize;
    zs.next_in = (Bytef *)input;
    zs.avail_out = (uInt)outputSize;
    zs.next_out = (Bytef *)output;
    deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 | 16, 8,
            Z_DEFAULT_STRATEGY);
    int ret = deflate(&zs, Z_FINISH);
    deflateEnd(&zs);
    if (ret != Z_STREAM_END) {
        fprintf(stderr, "Compression failed\n");
        return -1;
    }
    return zs.total_out;
}
void *handle_connection(void *arg) {
    int response_len = 0;
    int hasGzip = 0;
    int client_fd = *((int *)arg);
    char buf[4096];
    char buf_copy[4096];
    char resp_buf[4096];
    char *path;
    char *response = malloc(sizeof(char) * 500);
    char *userAgent = malloc(sizeof(char) * 500);
    if (recv(client_fd, buf, 4096, 0) == -1) {
        printf("Receiving failed: %s \n", strerror(errno));
        close(client_fd);
        return NULL;
    }
    strcpy(buf_copy, buf);
    for (char *header = strtok(buf, "\r\n"); header != NULL;
            header = strtok(NULL, "\r\n")) {
        if (strncmp(header, "User-Agent", 10) == 0) {
            userAgent = header + 12;
        }
    }
    if (strtok(buf, " ") == NULL) {
        printf("Invalid request\n");
        close(client_fd);
        return NULL;
    }
    path = strtok(NULL, " ");
    if (strncmp(path, "/files/", 7) == 0 && strcmp(buf, "POST") == 0) {
        char *filename = path + 7;
        char filepath[4096];
        snprintf(filepath, sizeof(filepath), "%s%s", directory, filename);
        char *content = strstr(buf_copy, "\r\n\r\n");
        if (content) {
            content += 4;
            FILE *file_ptr = fopen(filepath, "w");
            fprintf(file_ptr, content);
            fclose(file_ptr);
            response = (char *)"HTTP/1.1 201 Created\r\n\r\n";
        }
    } else if (strncmp(path, "/files/", 7) == 0) {
        const char *fileName = path + 6;
        const char *filepath = strcat(directory, fileName);
        FILE *fd = fopen(filepath, "r");
        if (fd == NULL) {
            response = (char *)"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        } else {
            ssize_t bytes = fread(resp_buf, 1, 4096, fd);
            if (bytes > 0) {
                response = malloc(4096);
                sprintf(response,
                        "HTTP/1.1 200 OK\r\nContent-Type: "
                        "application/octet-stream\r\nContent-Length: %zd\r\n\r\n%s",
                        bytes, resp_buf);
            }
        }
    } else if (strncmp(path, "/echo/", 6) == 0) {
        char *acceptEncoding = NULL;
        char *line = strtok(buf_copy, "\r\n");
        while (line != NULL) {
            if (strncmp(line, "Accept-Encoding:", 16) == 0) {
                acceptEncoding = line;
                line = NULL;
            }
            line = strtok(NULL, "\r\n");
        }
        strtok(acceptEncoding, " ");
        int gzip = 0;
        char *encoding;
        for (encoding = strtok(NULL, ", "); encoding != NULL;
                encoding = strtok(NULL, ", ")) {
            if (strcmp(encoding, "gzip") == 0) {
                gzip = 1;
                break;
            }
        }
        const char *content = path + 6;
        if (gzip == 1) {
            sprintf(response,
                    "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Type: "
                    "text/plain\r\nContent-Length: "
                    "%zu\r\n\r\n%s",
                    strlen(content), content);
            if (gzip) {
                hasGzip = 1;
                char compressed[1024];
                int compressed_len =
                    compressToGzip(content, strlen(content), compressed, 1024);
                if (compressed_len > 0) {
                    response = malloc(1024 + compressed_len);
                    snprintf(response, 1024,
                            "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Type: "
                            "text/plain\r\nContent-Length: %d\r\n\r\n",
                            compressed_len);
                    memcpy(response + strlen(response), compressed, compressed_len);
                }
                response_len = strlen(response) + compressed_len - 3;
            } else {
                sprintf(response,
                        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "
                        "%zu\r\n\r\n%s",
                        strlen(content), content);
            }
        } else if (strcmp(path, "/user-agent") == 0) {
            sprintf(response,
                    "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "
                    "%zu\r\n\r\n%s",
                    strlen(userAgent), userAgent);
        } else if (strcmp(path, "/") == 0) {
            response = (char *)"HTTP/1.1 200 OK\r\n\r\n";
        } else {
            response = (char *)"HTTP/1.1 404 Not Found\r\n\r\n";
        }
        write(client_fd, (const char *)response, strlen(response));
        if (!hasGzip) {
            response_len = strlen(response);
        }
        send(client_fd, response, response_len, 0);
        close(client_fd);
        return NULL;
    }
    int main(int argc, char **argv) {
        setbuf(stdout, NULL);
        int server_fd, client_fd;
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == -1) {
            printf("Socket creation failed: %s...\n", strerror(errno));
            return 1;
        }
        int reuse = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) <
                0) {
            printf("SO_REUSEPORT failed: %s \n", strerror(errno));
            return 1;
        }
        struct sockaddr_in serv_addr = {
            .sin_family = AF_INET,
            .sin_port = htons(4221),
            .sin_addr = {htonl(INADDR_ANY)},
        };
        if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0) {
            printf("Bind failed: %s \n", strerror(errno));
            return 1;
        }
        int connection_backlog = 5;
        if (listen(server_fd, connection_backlog) != 0) {
            printf("Listen failed: %s \n", strerror(errno));
            return 1;
        }
        if (argc == 3) {
            strcpy(directory, argv[2]);
        }
        while (1) {
            printf("Waiting for a client to connect...\n");
            client_addr_len = sizeof(client_addr);
            int client_fd =
                accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
            printf("Client connected\n");
            pthread_t thread_id;
            int *p_client = malloc(sizeof(int));
            *p_client = client_fd;
            pthread_create(&thread_id, NULL, handle_connection, p_client);
            pthread_detach(thread_id);
        }
        close(server_fd);
        return 0;
    }
}
