#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>

#define BUFFER_SIZE 1000
#define DEFAULT_PORT 80

static unsigned long long total_requests = 0;
static unsigned long long total_received_bytes = 0;
static unsigned long long total_sent_bytes = 0;
pthread_mutex_t mutex;

void read_http_request(int client_fd, char *request)
{
    int bytes_read = read(client_fd, request, BUFFER_SIZE - 1);
    if (bytes_read < 0)
    {
        perror("Read failed");
        return;
    }

    else if (bytes_read == 0)
    {
        printf("Client disconnected\n");
        return;
    }
    request[bytes_read] = '\0';
}

void send_http_response(int client_fd, char *message)
{
    const char *default_msg = "HTTP/1.1 200 OK\n"
                              "Server: webserver-c\n"
                              "Content-type: text/html\n"
                              "Content-Length: %d\n"
                              "Connection: Keep-Alive\n"
                              "\n%s";

    int response_buffer_size = strlen(default_msg) + 100 + strlen(message);
    char *response = malloc(response_buffer_size);

    snprintf(response, response_buffer_size, default_msg, (int)strlen(message), message);
    total_sent_bytes += strlen(response);
    write(client_fd, response, strlen(response));
    free(response);
}

void send_error_response(int client_fd)
{
    const char *error_resp = "HTTP/1.1 404 Not Found\r\n"
                             "\r\n"
                             "Not found.\n";
    write(client_fd, error_resp, strlen(error_resp));
}

void handle_static_request(int client_fd, char *request)
{
    char file_path[BUFFER_SIZE];
    int result = sscanf(request, "GET /static/%s ", file_path);

    if (result < 1 || result == EOF)
    {
        send_error_response(client_fd);
    }

    int fd = open(file_path, O_RDONLY);
    char file_contents[BUFFER_SIZE];
    int bytes_read;
    while ((bytes_read = read(fd, file_contents, sizeof(file_contents))) > 0)
    {
        write(client_fd, file_contents, bytes_read);
    }

    pthread_mutex_lock(&mutex);
    total_sent_bytes += bytes_read;
    pthread_mutex_unlock(&mutex);

    close(fd);
}

void handle_stats_request(int client_fd)
{
    char stats[BUFFER_SIZE];
    snprintf(stats, sizeof(stats),
             "<html><body><h1>Server Statistics</h1>"
             "<p>Total requests: %llu</p>"
             "<p>Total received bytes: %llu</p>"
             "<p>Total sent bytes: %llu</p></body></html>",
             total_requests, total_received_bytes, total_sent_bytes);

    pthread_mutex_lock(&mutex);
    total_sent_bytes += strlen(stats);
    pthread_mutex_unlock(&mutex);

    send_http_response(client_fd, stats);
}

void handle_calc_request(int client_fd, char *request)
{
    int num1, num2;
    int result = sscanf(request, "GET /calc/%d/%d ", &num1, &num2);

    if (result < 2 || result == EOF)
    {
        send_error_response(client_fd);
    }

    char response_body[BUFFER_SIZE];
    snprintf(response_body, sizeof(response_body), "Sum of %d and %d is %d.\n", num1, num2, num1 + num2);

    pthread_mutex_lock(&mutex);
    total_sent_bytes += strlen(response_body);
    pthread_mutex_unlock(&mutex);

    send_http_response(client_fd, response_body);
}

void *handle_client(int *arg)
{
    int bytes_read, client_fd;
    char request[BUFFER_SIZE];

    client_fd = *(arg);
    free(arg);

    read_http_request(client_fd, request);

    if (strstr(request, "GET /static/") == request)
    {
        handle_static_request(client_fd, request);
    }
    else if (strstr(request, "GET /stats") == request)
    {
        handle_stats_request(client_fd);
    }
    else if (strstr(request, "GET /calc/") == request)
    {
        handle_calc_request(client_fd, request);
    }
    else
    {
        send_error_response(client_fd);
    }

    pthread_mutex_lock(&mutex);
    total_requests++;
    pthread_mutex_unlock(&mutex);

    close(client_fd);
}

int main(int argc, char **argv)
{
    if (argc != 3 || strcmp(argv[1], "-p") != 0)
    {
        fprintf(stderr, "Usage: %s -p <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int server_fd, *client_fd;
    struct sockaddr_in addr;
    pthread_t thread_id;
    pthread_mutex_init(&mutex, NULL);

    int port = atoi(argv[2]);

    if (port <= 0)
    {
        fprintf(stderr, "Invalid port number!");
        return EXIT_FAILURE;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("Bind failed");
        return EXIT_FAILURE;
    }

    listen(server_fd, 1);

    while (1)
    {
        client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, NULL, NULL);
        printf("Client connected\n");
        if (pthread_create(&thread_id, NULL, (void *)handle_client, client_fd) != 0)
        {
            perror("Thread creation failed");
            close(*client_fd);
            free(client_fd);
        }
        else
        {
            pthread_detach(thread_id);
        }
    }
    close(server_fd);
    pthread_mutex_destroy(&mutex);
    return 0;
}