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
dup.c
#include <arpa/inet.h> // definitions for internet operations
#include <ctype.h> // character types
#include <dirent.h> // format of directory entries
#include <errno.h> // error return value
#include <netinet/in.h> // internet address family
#include <netinet/ip.h> // internet protocol family
#include <pthread.h> // threads
#include <stdint.h> // integer types
#include <stdio.h> // standard buffered input/output
#include <stdlib.h> // standard library definitions
#include <string.h> // string operations
#include <sys/socket.h> // internet protocol family
#include <unistd.h> // standard symbolic constants and types
#include <zlib.h> // gzip compression
#include "colors.h"
#include "tpool.h"
#include "utils.h"
#define MAX_THREADS 4
#define C_OK 0
#define C_ERR 1
#define PORT 4221
#define FLAG_DIRECTORY "--directory"
#define BUFFER_SIZE 1024
#define FILE_BUFFER_SIZE 1024
#define REQEUST_BUFFER_SIZE 1024
#define RESPONSE_BUFFER_SIZE 4096
#define STATUS_OK "HTTP/1.1 200 OK\r\n"
#define STATUS_CREATED "HTTP/1.1 201 Created\r\n\r\n"
#define STATUS_NOT_FOUND "HTTP/1.1 404 Not Found\r\n\r\n"
#define STATUS_INTERNAL_SERVER_ERROR
"HTTP/1.1 500 Internal Server Error\r\n\r\n"
#define STATUS_METHOD_NOT_ALLOWED "HTTP/1.1 405 Method Not Allowed\r\n\r\n"
#define CONTENT_LENGTH "Content-Length: "
#define CONTENT_TYPE_TEXT "Content-Type: text/plain\r\n"
#define CONTENT_TYPE_FILE "Content-Type: application/octet-stream\r\n"
#define CONTENT_ENCODING_GZIP "Content-Encoding: gzip\r\n"
#define CLRF "\r\n"
    struct ThreadArgs {
  struct sockaddr_in client_addr;
  int client_fd;
} ThreadArgs;
struct Request {
  char *method;
  char *path;
  char *version;
  char *accept;
  char *accept_encoding;
  char *host;
  char *user_agent;
  char *content_length;
  char *body;
  size_t size;
} Request;
char *option_directory = NULL;
int server_listen();
void server_process_client(void *arg);
void request_parse(char *buffer, struct Request *request);
void request_print(const struct Request *request);
void response_build(char *buffer, struct Request *request);
int compressToGzip(const char *input, int inputSize, char *output,
                   int outputSize);
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
  deflate(&zs, Z_FINISH);
  deflateEnd(&zs);
  return zs.total_out;
}
void request_print(const struct Request *request) {
  printf(YELLOW "%s\n" RESET, request->method);
  printf(YELLOW "%s\n" RESET, request->path);
  printf(YELLOW "%s\n" RESET, request->version);
  printf(YELLOW "%s\n" RESET, request->accept);
  printf(YELLOW "%s\n" RESET, request->accept_encoding);
  printf(YELLOW "%s\n" RESET, request->host);
  printf(YELLOW "%s\n" RESET, request->user_agent);
  printf(YELLOW "%s\n" RESET, request->content_length);
  printf(YELLOW "%s\n" RESET, request->body);
}
void request_parse(char *buffer, struct Request *request) { // TODO: optimize
  char *token = strtok(buffer, " ");
  request->method = token;
  token = strtok(NULL, " ");
  request->path = token;
  token = strtok(NULL, " ");
  request->version = token;
  token = strtok(NULL, "\r\n");
  while (token != NULL) {
    if (!request->accept) {
      if (strstr(token, "Accept:") != NULL ||
          strstr(token, "accept:") != NULL) {
        request->accept = token;
      }
    }
    if (!request->accept_encoding) {
      if (strstr(token, "Accept-Encoding:") != NULL ||
          strstr(token, "accept-encoding:") != NULL) {
        request->accept_encoding = token;
      }
    }
    if (!request->user_agent) {
      if (strstr(token, "User-Agent:") != NULL ||
          strstr(token, "user-agent:") != NULL) {
        request->user_agent = token;
      }
    }
    if (!request->host) {
      if (strstr(token, "Host:") != NULL || strstr(token, "host:") != NULL) {
        request->host = token;
      }
    }
    if (strstr(token, "Content-Length:") != NULL ||
        strstr(token, "content-length:") != NULL) {
      request->content_length = token;
    } else {
      request->body = token;
    }
    token = strtok(NULL, "\r\n");
  }
}
void response_build(char *buffer, struct Request *request) { // TODO: optimize
  // printf(CYAN "Request Buffer:\n" YELLOW "%s\n" RESET, buffer); // debug null
  if (!request->path) {
    snprintf(buffer, RESPONSE_BUFFER_SIZE, "%s", STATUS_INTERNAL_SERVER_ERROR);
    sprintf(buffer, "%s", STATUS_INTERNAL_SERVER_ERROR);
  } else if (strcmp(request->path, "/") == 0) {
    snprintf(buffer, RESPONSE_BUFFER_SIZE, "%s%s", STATUS_OK, CLRF);
    sprintf(buffer, "%s%s", STATUS_OK, CLRF);
  } else if (strstr(request->path, "/user-agent") != NULL) {
    strremove(request->user_agent, "user-agent: ");
    strremove(request->user_agent, "User-Agent: ");
    snprintf(buffer, RESPONSE_BUFFER_SIZE, "%s%s%s%zd\r\n\r\n%s\r\n", STATUS_OK,
             CONTENT_TYPE_TEXT, CONTENT_LENGTH, strlen(request->user_agent),
             request->user_agent);
    sprintf(buffer, "%s%s%s%zd\r\n\r\n%s\r\n", STATUS_OK, CONTENT_TYPE_TEXT,
            CONTENT_LENGTH, strlen(request->user_agent), request->user_agent);
  } else if (strstr(request->path, "/echo/") != NULL) {
    if (request->accept_encoding &&
        strstr(request->accept_encoding, "gzip") != NULL) {
      strremove(request->path, "/echo/");
      snprintf(buffer, RESPONSE_BUFFER_SIZE, "%s%s%s%s%zd\r\n\r\n%s\r\n",
               STATUS_OK, CONTENT_ENCODING_GZIP, CONTENT_TYPE_TEXT,
               CONTENT_LENGTH, strlen(request->path), request->path);
      char body[BUFFER_SIZE];
      int len =
          compressToGzip(request->path, strlen(request->path), body, 1024);
      if (len < 0) {
        printf(RED "Compression failed: %s...\n" RESET, strerror(errno));
      }
      sprintf(buffer, "%s%s%s%s%d\r\n\r\n", STATUS_OK, CONTENT_TYPE_TEXT,
              CONTENT_ENCODING_GZIP, CONTENT_LENGTH, len);
      memcpy(buffer + strlen(buffer), body, len);
      request->size = len;
    } else {
      strremove(request->path, "/echo/");
      snprintf(buffer, RESPONSE_BUFFER_SIZE, "%s%s%s%zd\r\n\r\n%s\r\n",
               STATUS_OK, CONTENT_TYPE_TEXT, CONTENT_LENGTH,
               strlen(request->path), request->path);
      sprintf(buffer, "%s%s%s%zd\r\n\r\n%s\r\n", STATUS_OK, CONTENT_TYPE_TEXT,
              CONTENT_LENGTH, strlen(request->path), request->path);
    }
  } else if (strstr(request->path, "/files/") != NULL) {
    Expand 14 lines
        fread(data, sizeof(char), size, file_ptr);
    fclose(file_ptr);
    snprintf(buffer, RESPONSE_BUFFER_SIZE, "%s%s%s%d\r\n\r\n%s\r\n",
             STATUS_OK, CONTENT_TYPE_FILE, CONTENT_LENGTH, size, data);
    sprintf(buffer, "%s%s%s%d\r\n\r\n%s\r\n", STATUS_OK, CONTENT_TYPE_FILE,
            CONTENT_LENGTH, size, data);
  } else {
    snprintf(buffer, RESPONSE_BUFFER_SIZE, "%s", STATUS_NOT_FOUND);
    sprintf(buffer, "%s", STATUS_NOT_FOUND);
  }
}
else if (strstr(request->method, "POST") != NULL) // POST
{
  char filepath[1024] = {0};
  request->path++; // move pointer forward one
  strcat(filepath, option_directory);
  strcat(filepath, request->path);
  strremove(filepath, "files/");
  FILE *file_prt;
  file_prt = fopen(filepath, "w");
  fprintf(file_prt, request->body);
  fclose(file_prt);
  snprintf(buffer, RESPONSE_BUFFER_SIZE, "%s", STATUS_CREATED);
  sprintf(buffer, "%s", STATUS_CREATED);
}
else {
  snprintf(buffer, RESPONSE_BUFFER_SIZE, "%s", STATUS_METHOD_NOT_ALLOWED);
  sprintf(buffer, "%s", STATUS_METHOD_NOT_ALLOWED);
}
}
else {
  snprintf(buffer, RESPONSE_BUFFER_SIZE, "%s", STATUS_NOT_FOUND);
  sprintf(buffer, "%s", STATUS_NOT_FOUND);
}
}
int server_listen() {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1) {
    printf(RED "Socket creation failed: %s...\n" RESET, strerror(errno));
    return C_ERR;
  }
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
      0) {
    printf(RED "SO_REUSEPORT failed: %s \n" RESET, strerror(errno));
    return C_ERR;
  }
  struct sockaddr_in serv_addr = {
      .sin_family = AF_INET,
      .sin_port = htons(PORT),
      .sin_addr = {htonl(INADDR_ANY)},
  };
  if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0) {
    printf(RED "Bind failed: %s \n" RESET, strerror(errno));
    return C_ERR;
  }
  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    printf(RED "Listen failed: %s \n" RESET, strerror(errno));
    return C_ERR;
  }
  printf(CYAN "Server listening: %s:%d <----------\n" RESET,
         inet_ntoa(serv_addr.sin_addr), ntohs(serv_addr.sin_port));
  return server_fd;
}
void server_process_client(void *arg) {
  struct ThreadArgs *thread_args = (struct ThreadArgs *)(arg);
  char response_buffer[RESPONSE_BUFFER_SIZE];
  char request_buffer[REQEUST_BUFFER_SIZE];
  if (recv(thread_args->client_fd, request_buffer, sizeof(request_buffer), 0) ==
      -1) {
    printf(RED "Recieved failed: %s...\n" RESET, strerror(errno));
  }
  // printf(CYAN "Request Buffer:\n" YELLOW "%s\n" RESET, request_buffer);
  struct Request request;
  request_parse(request_buffer, &request);
  response_build(response_buffer, &request);
  // request_print(&request);
  // printf(CYAN "Response Buffer:\n" YELLOW "%s\n" RESET, response_buffer);
  if (send(thread_args->client_fd, response_buffer, strlen(response_buffer),
           0) == -1) {
    if (send(thread_args->client_fd, response_buffer,
             strlen(response_buffer) + request.size, 0) == -1) {
      printf(RED "Send failed: %s...\n" RESET, strerror(errno));
    }
    printf(GREEN "Message sent: %s:%d <----------\n" RESET,
           inet_ntoa(thread_args->client_addr.sin_addr),
           ntohs(thread_args->client_addr.sin_port));
    close(thread_args->client_fd);
    free(thread_args);
  }
  int main(int argc, char *argv[]) {
    if (argv[1] != NULL) {
      if (strcmp(argv[1], FLAG_DIRECTORY) == 0) {
        option_directory = argv[2];
        printf(YELLOW "Directory path set: " RESET "%s\n", option_directory);
      }
    }
    setbuf(stdout, NULL);
    threadpool thread_pool = tpool_init(MAX_THREADS);
    printf(GREEN "Thread pool created: %d threads\n" RESET, MAX_THREADS);
    int server_fd = server_listen();
    for (;;) {
      void *thread_args_ptr = malloc(sizeof(struct ThreadArgs));
      struct ThreadArgs *thread_args = (struct ThreadArgs *)(thread_args_ptr);
      socklen_t client_addr_len = sizeof(thread_args->client_addr);
      thread_args->client_fd =
          accept(server_fd, (struct sockaddr *)&thread_args->client_addr,
                 &client_addr_len);
      if (thread_args->client_fd == -1) {
        printf(RED "Client connection failed: %s \n" RESET, strerror(errno));
      }
      printf(CYAN "Client connected: %s:%d <----------\n" RESET,
             inet_ntoa(thread_args->client_addr.sin_addr),
             ntohs(thread_args->client_addr.sin_port));
      if (tpool_add_work(thread_pool, server_process_client,
                         (void *)thread_args) == -1) {
        printf(RED "Failed to create pthread: %s\n" RESET, strerror(errno));
        close(thread_args->client_fd);
        free(thread_args);
        free(thread_args_ptr);
      }
    }
    printf(YELLOW "Waiting for thread pool work to complete..." RESET);
    tpool_wait(thread_pool);
    printf(RED "Killing threadpool..." RESET);
    tpool_destroy(thread_pool);
    printf(RED "Closing server socket..." RESET);
    close(server_fd);
    return C_OK;
  }
