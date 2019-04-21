#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define DEFAULT_PORT (7500)
#define BAD_PORT (1)
#define MAX_PORT (65535)
#define IP_ADDR ("127.0.0.1")
#define MAX_CLIENTS (5)
#define NUMBER_OF_PORTS (2)
#define SELECT_TIMEOUT (3)

int CURRENT_SERVER = DEFAULT_PORT;
int KNOWN_PORTS[NUMBER_OF_PORTS] = {7500, 7501};
int leader = 0;

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

typedef struct server_params_t {
    config_t *config;
    int fd;
    pthread_mutex_t mutex;
} server_params_t;

void *rpc_handler(void *arg) {
    client_params_t *_client_params = arg;
    client_params_t client_params = *_client_params;
    pthread_mutex_unlock(&_client_params->mutex);
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

double wait_for(clock_t start_time){
    return (clock() - start_time) / CLOCKS_PER_SEC;
}

void *receive_rpcs(int sock, config_t *config){
    // In `timeout` try to get incoming calls
    // for each call create a thread
    // if thread is not freeing the mutex in `thread_timeout` - kill the thread and unlock the mutex
    printf("Receiving rpcs\n");
    clock_t start_time = clock();
    double timeout = 0.5;
    double thread_timeout = 0.005;
    client_params_t client_params;
    pthread_mutex_init(&client_params.mutex, NULL);
    pthread_mutex_lock(&client_params.mutex);
    client_params.config = config;

    while (wait_for(start_time) < timeout) {
        printf("wait %f\n", wait_for(start_time));
        struct sockaddr server_name;
        socklen_t server_name_len;
        // client connected
        printf("Creating a thread\n");
        int fd = accept(sock, &server_name, &server_name_len);
        if (fd < 0) {
            perror("ошибка вызова accept");
            break;
        }
        // creating a thread 
        pthread_t thread;
        client_params.fd = fd;
        int rv = pthread_create(&thread, NULL, rpc_handler, &client_params);

        clock_t start_thread = clock();
        while (pthread_mutex_trylock(&client_params.mutex) 
                && wait_for(start_thread) < thread_timeout){}
        if (pthread_mutex_trylock(&client_params.mutex) && rv == 0){
            pthread_kill(thread);
            pthread_mutex_unlock(&client_params.mutex);
        }
        
        pthread_mutex_trylock(&client_params.mutex);
    }
}

void *send_rpcs(sock, port){
    printf("Sending rpcs\n");
    struct sockaddr_in peer;
    peer.sin_family = AF_INET;
    peer.sin_port = htons(port);
    peer.sin_addr.s_addr = inet_addr(IP_ADDR);

    int rc = connect(sock, (struct sockaddr *)&peer, sizeof(peer)); 
    if (rc > 0) {
        perror("ошибка вызова connect");
        return ((void *)EXIT_FAILURE);        
    }
    // if connection is successful print it
    printf("Connected %d\n", CURRENT_SERVER);
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
    
    printf("Recieved %d\n", response.val);
    return ((void *)EXIT_SUCCESS);
}

int run_server(config_t *config) {
    printf("Running %d\n", config->port);
    struct sockaddr_in local;
    int rc;
    // tuning the server
    local.sin_family = AF_INET;
    local.sin_port = htons(config->port);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    // int sock_out = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror ("ошибка вызова socket");
        return (EXIT_FAILURE);
    }

    fcntl(sock, F_SETFL, O_NONBLOCK);

    rc = bind(sock, (struct sockaddr *)&local, sizeof(local));
    if (rc < 0) {
        perror("ошибка вызова bind");
        return (EXIT_FAILURE);
    }

    // setting the maximum number of clients
    rc = listen(sock, MAX_CLIENTS);
    if (rc) {
        perror("ошибка вызова listen");
        return (EXIT_FAILURE);
    }

    while (1){
        if (!leader){
            // wait for incoming messages from leader/client (it might be hearbeats)
            fd_set sockets;
            FD_ZERO(&sockets);
            FD_SET(sock, &sockets);

            struct timeval timeout;
            timeout.tv_sec = SELECT_TIMEOUT;
            timeout.tv_usec = 0;
            printf("Entering select\n");
            int sel = select(sock + 1, &sockets, NULL, NULL, &timeout); 
            printf("Right after select %d\n", sel);
            if (sel < 0){
                perror("Ошибка select");
                return (EXIT_FAILURE);
            }

            // timeout
            if (sel == 0){
                continue;
            }

            
            if (FD_ISSET(sock, &sockets)){
                printf("In FD_ISSET\n");
                int fd = accept(sock, NULL, NULL);
                printf("ACCEPTED\n");
                if (fd < 0){
                    perror("Error in accept");
                    return (EXIT_FAILURE);
                }

                server_params_t server_params ={
                    .config = config,
                    .fd = fd,
                };
                pthread_t thread;
                int rv = pthread_create(&thread, NULL, rpc_handler, &server_params);
                if (rv == 0){
                    //lock mutex
                }
            }
            printf("AFTER ACCEPT\n");
        }
        else{
            // leader actions
        }
    }
    //close(sock);
}

int parse_str(int *num, char *str){
    *num = 0;

    int len = strlen(str);
    int i;
    for(i = 0; i < len; i++){
        if (isdigit(str[i])){
            *num *= 10;
            *num += str[i] - '0';
        }
        else{
            return BAD_PORT;
        }
        if (*num > MAX_PORT){
            *num = 0;
            return BAD_PORT;
        }
    }
    return 0;
}

int main(int argc, char * argv[]){
    parse_str(&CURRENT_SERVER, argv[1]);
    
    config_t config = {
        .port = CURRENT_SERVER,
    };
    
    int rv = run_server(&config);
    if (rv != 0){
        printf("Fail?\n");
    }

    return EXIT_SUCCESS;
}
