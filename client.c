#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <ctype.h>

#define DEFAULT_PORT (7500)
#define IP_ADDR ("127.0.0.1")

typedef struct command_t {
    int val;
} command_t;

typedef struct response_t {
    int val;
} response_t;

typedef struct config_t {
    int port;
} config_t;

typedef struct client_params_t {
    // struct sockaddr_in *peer;
    int id;
    config_t *config;
} client_params_t;

void * run_client(void * arg){
    client_params_t client_params = *(client_params_t *)arg;

    // connecting to server
    struct sockaddr_in peer;
    peer.sin_family = AF_INET;
    peer.sin_port = htons(client_params.config->port);
    peer.sin_addr.s_addr = inet_addr(IP_ADDR);

    int sock;
    int rc;
    char buf[1];
    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("ошибка вызова socket");
        return ((void *)EXIT_FAILURE);
    }
    
    if ((rc = connect(sock, (struct sockaddr *)&peer, sizeof(peer))) > 0) {
        perror("ошибка вызова connect");
        return ((void *)EXIT_FAILURE);        
    }
    // if connection is successful print it
    printf("Connected %d\n", client_params.id);
    
    response_t response;
    command_t command = {15};
    if ((rc = send(sock, &command, sizeof(command), 0)) <= 0) {
        perror("ошибка вызова send");
        return ((void *)EXIT_FAILURE);
    }

    printf("Sent success\n");

    if ((rc = recv(sock, &response, sizeof(response), 0)) <= 0) {
        perror("ошибка вызова recv");
        return ((void *)EXIT_FAILURE);
    }
    
    printf("Recv success\n");
    printf("Recieved %d\n", response.val);
    close(sock);
    return ((void *)EXIT_SUCCESS);
}

int main(int argc, char * argv[]) {
    srand(time(NULL));

    config_t config = {
        .port = DEFAULT_PORT,
    };

    client_params_t params = {
        .config = &config,
        .id = 0,
    };
    int rv = run_client(&params);
    if (rv != 0){
        printf("Failed %d\n", rv);
        return (EXIT_FAILURE);
    }

    return (EXIT_SUCCESS);
}
