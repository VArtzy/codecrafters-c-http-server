#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <zlib.h>

char *directory = NULL;


int compressToGzip(const char *input, int inputSize, char *output, int outputSize) {
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
  // deflate(&zs, Z_FINISH);
  deflateEnd(&zs);
  return zs.total_out;
}

void http_handler(int conn) {
    uint8_t buff[1024];
    read(conn, buff, sizeof(buff));
    strtok(buff, " ");
    char* path = strtok(0, " ");
    if (strncmp(path, "/files", 6) == 0 && directory != NULL && strcmp(buff, "POST") == 0) {
        strtok(0, "\r\n\r\n");
        strtok(0, "\r\n\r\n");
        strtok(0, "\r\n\r\n");
        char *content = strtok(0, "\r\n\r\n");
        char *filename = path + 7;
        const char *filepath = strcat(directory, filename);
        FILE *fptr = fopen(filepath, "w");
        fprintf(fptr, content);
        fclose(fptr);
        char response[] = "HTTP/1.1 201 Created\r\n\r\n";
        send(conn, response, sizeof(response), 0);
    }
    if (strncmp(path, "/files", 6) == 0 && directory != NULL) {
        char *file = strchr(path + 1, '/'); 
        if (file != NULL) {
            const char *filepath = strcat(directory, file);
            FILE *fd = fopen(filepath, "r");
            if (fd != NULL) {
                char fbuff[2048];
                int contentLength = fread(fbuff, 1, 2048, fd);       
                fclose(fd);
                const char *format = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %zu\r\n\r\n%s";
                char response[1024];
                sprintf(response, format, contentLength, fbuff);
                send(conn, response, sizeof(response), 0);
            } else {
                char response[] = "HTTP/1.1 404 Not Found\r\n\r\n";
                send(conn, response, sizeof(response), 0);
            }
        }
    } else if (strncmp(path, "/user-agent", 11) == 0) {
        strtok(0, "\r\n");
        strtok(0, "\r\n");
        char* userAgent = strtok(0, "\r\n") + 12;
        const char *format = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%s";
        char response[1024];
        sprintf(response, format, strlen(userAgent), userAgent);
        send(conn, response, sizeof(response), 0);
    } else if (strncmp(path, "/echo/", 6) == 0) {
        strtok(0, "\r\n\r\n");
        strtok(0, "\r\n\r\n");
        char *contentEncoding = strtok(0, "\r\n");
        size_t contentLength = strlen(path) - 6;
        char *content = path + 6;
        char response[1024];
        if (contentEncoding != NULL) {
            if (strstr(contentEncoding, "gzip") != NULL) {
                char compressed[1024];
                int compressedLength = compressToGzip(content, strlen(content), compressed, 1024);
                sprintf(response, "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n", compressedLength);
            } else {
                sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%s", contentLength, content);
            }
        } else {
            sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%s", contentLength, content);
        }
        send(conn, response, sizeof(response), 0);
    } else if (strcmp(path, "/") == 0) {
        char response[] = "HTTP/1.1 200 OK\r\n\r\n";
        send(conn, response, sizeof(response), 0);
    } else {
        char response[] = "HTTP/1.1 404 Not Found\r\n\r\n";
        send(conn, response, sizeof(response), 0);
    }
}

int main(int argc, char **argv) {
    if (argc >= 2 && strncmp(argv[1], "--directory", sizeof("--directory")) == 0) {
        directory = argv[2];
    }
	// Disable output buffering
	setbuf(stdout, NULL);

	int server_fd, client_addr_len;
	struct sockaddr_in client_addr;
    pthread_t tid;

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}

	// Since the tester restarts your program quite often, setting REUSE_PORT
	// ensures that we don't run into 'Address already in use' errors
	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
		printf("SO_REUSEPORT failed: %s \n", strerror(errno));
		return 1;
	}

	struct sockaddr_in serv_addr = { .sin_family = AF_INET ,
									 .sin_port = htons(4221),
									 .sin_addr = { htonl(INADDR_ANY) },
									};

	if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
		printf("Bind failed: %s \n", strerror(errno));
		return 1;
	}

	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0) {
		printf("Listen failed: %s \n", strerror(errno));
		return 1;
	}

	printf("Waiting for a client to connect...\n");

    while(1) {
        client_addr_len = sizeof(client_addr);
        int conn = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
        if (conn < 0) {
            break;
        }
        pthread_create(&tid, NULL, http_handler, conn); 
        printf("Client connected\n");
    }

	close(server_fd);

	return 0;
}
