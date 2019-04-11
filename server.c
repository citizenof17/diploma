#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_PORT (7500)
#define IP_ADDR ("127.0.0.1")
#define MAX_CLIENTS (5)

typedef struct command_t {
    int val;
} command_t;

typedef struct response_t {
    int val;
} response_t;

typedef struct status_t {
    int val;
} status_t;

typedef struct config_t {
    int port;
} config_t; 

typedef struct client_params_t {
    config_t *config;
    int fd;
    pthread_mutex_t mutex;
} client_params_t;

void *rpc_handler(void *arg) {
    client_params_t client_params = *(client_params_t *)arg;
    pthread_mutex_unlock (&client_params.mutex);
    command_t query;
    response_t response;
    
    int rc = recv(client_params.fd, &query, sizeof(query), 0);
    if (rc <= 0) {
        printf("Failed\n");
        return ((void *)EXIT_FAILURE);
    }
    else {
        printf("Received %d\n", query.val);
    }

    response.val = 2;
    rc = send(client_params.fd, &response, sizeof(response), 0);
    if (rc <= 0) {
        perror("ошибка вызова send");
    }  
}

int run_server(config_t *config) {
    printf("Running\n");
    struct sockaddr_in local;
    int rc;
    char buf[1];
    // tuning the server
    local.sin_family = AF_INET;
    local.sin_port = htons(config->port);
    local.sin_addr.s_addr = htonl(INADDR_ANY);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror ("ошибка вызова socket");
        return (EXIT_FAILURE);
    }

    rc = bind(sock, (struct sockaddr *)&local, sizeof(local));
    if (rc < 0) {
        perror("ошибка вызова bind");
        return (EXIT_FAILURE);
    }

    // setting the maximum number of clients
    if (rc = listen(sock, MAX_CLIENTS)) {
        perror("ошибка вызова listen");
        return (EXIT_FAILURE);
    }

    client_params_t client_params;
    pthread_mutex_init(&client_params.mutex, NULL);
    pthread_mutex_lock(&client_params.mutex);
    client_params.config = config;

    int i = 0;
    int n = 5;
    for (i; i < n; ++i){
        struct sockaddr client_name;
        socklen_t client_name_len;
        // client connected
        int fd = accept(sock, &client_name, &client_name_len);

        if (fd < 0) {
            perror("ошибка вызова accept");
            break;
        }

        // creating a thread 
        pthread_t thread;
        client_params.fd = fd;
        int rv = pthread_create (&thread, NULL, rpc_handler, &client_params);
        if (rv == 0){
            pthread_mutex_lock (&client_params.mutex);
        }
    }
}

int main(int argc, char * argv[]){
    config_t config = {
        .port = DEFAULT_PORT
    };
    
    int rv = run_server(&config);
    if (rv != 0){
        printf("Fail?\n");
    }

    return EXIT_SUCCESS;
}
