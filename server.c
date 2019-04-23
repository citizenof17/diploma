#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>
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
#define HEARTBEAT_TIMEOUT (10.0)

typedef enum whom_e {
    Unknown,
    Client,
    Follower,
    Candidate,
    Leader,
} whom_e;


// initially all servers start in follower state
whom_e State = Follower;

int CURRENT_SERVER = DEFAULT_PORT;
int KNOWN_PORTS[NUMBER_OF_PORTS] = {7500, 7501};
int leader = 0;

int current_term = 0;
int voted_for = 0;
int *log = NULL;

int commit_index = 0;
int last_applied = 0;

int *next_index = NULL;
int *match_index = NULL;

typedef struct command_t {
    int val;
} command_t;

typedef struct logg {
    command_t command;
    int term;
} log_t;

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




typedef struct message_t {
    whom_e whom;
    command_t command;
} message_t;

void request_vote(int port, response_t *response){

}

void initiate_election(config_t *config){
    printf("Initiate election\n");
    current_term++;
    State = Candidate;
    voted_for = CURRENT_SERVER;
    //reset election timer? 

    int i = 0;
    int votes = 1;
    for (i; i < NUMBER_OF_PORTS; i++){
        if (KNOWN_PORTS[i] != CURRENT_SERVER){
            response_t response;
            request_vote(KNOWN_PORTS[i], &response);
            votes += response.val;
        }
    }

    if (CURRENT_SERVER & 1){
        leader = 1;
    }
    printf("LEADER %d\n", leader);
}

void *rpc_handler(void *arg) {
    client_params_t *_client_params = arg;
    client_params_t client_params = *_client_params;
    pthread_mutex_unlock(&_client_params->mutex);
    message_t message;
    
    int rc = recv(client_params.fd, &message, sizeof(message), 0);
    if (rc <= 0) {
        printf("Failed\n");
        return ((void *)EXIT_FAILURE);
    }

    printf("Received %d\n", message.command.val);

    // Differentiate between clients and leader/other servers
    switch (message.whom){
        case Client:
            // handle client
            break;
        case Follower:
            // handle follower
            break;
        case Candidate:
            // handle candidate
            break;
        case Leader:
            // handle leader
            break;
        default:
            perror("Unknown peer");
    }


    // Return something in response, this should go into switch statement
    response_t response;
    response.val = 2;
    rc = send(client_params.fd, &response, sizeof(response), 0);
    if (rc <= 0) {
        perror("ошибка вызова send");
    }
}

double time_spent(clock_t start_time){
    return (clock() - start_time) / CLOCKS_PER_SEC;
}

void *send_rpcs(port){
    printf("Sending rpcs\n");
    struct sockaddr_in peer;
    peer.sin_family = AF_INET;
    peer.sin_port = htons(port);
    peer.sin_addr.s_addr = inet_addr(IP_ADDR);

    int sock;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("ошибка вызова socket");
        return ((void *)EXIT_FAILURE);
    }

    int rc = connect(sock, (struct sockaddr *)&peer, sizeof(peer)); 
    if (rc > 0) {
        perror("ошибка вызова connect");
        return ((void *)EXIT_FAILURE);        
    }
    // if connection is successful print it
    printf("Connected %d\n", CURRENT_SERVER);
    response_t response;
    command_t command = {15};
    printf("Hi1\n");
    if ((rc = send(sock, &command, sizeof(command), 0)) <= 0) {
        printf("Hi2\n");
        perror("ошибка вызова send");
        return ((void *)EXIT_FAILURE);
    }
    printf("Hi3\n");

    printf("Sent success\n");

    if ((rc = recv(sock, &response, sizeof(response), 0)) <= 0) {
        perror("ошибка вызова recv");
        return ((void *)EXIT_FAILURE);
    }
    
    close(sock);
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
    struct timeval timeout = {
        .tv_sec = SELECT_TIMEOUT,
        .tv_usec = 0,
    };

    clock_t last_heartbeat = clock();

    for (;;) {
        if (State == Follower){
            // wait for incoming messages from leader/client
            fd_set sockets;
            FD_ZERO(&sockets);
            FD_SET(sock, &sockets);

            int sel = select(sock + 1, &sockets, NULL, NULL, &timeout); 
            if (sel < 0){
                perror("Ошибка select");
                return (EXIT_FAILURE);
            }

            // timeout, initiate election
            if (sel == 0){
                initiate_election(config);
                continue;
            }

            if (FD_ISSET(sock, &sockets)){
                int fd = accept(sock, NULL, NULL);
                printf("accepted, got called\n");

                if (fd < 0){
                    perror("Error in accept");
                    return (EXIT_FAILURE);
                }

                // we do want to have mutex here
                server_params_t server_params ={
                    .config = config,
                    .fd = fd,
                };
                pthread_mutex_lock(&server_params.mutex);
                pthread_t thread;
                int rv = pthread_create(&thread, NULL, rpc_handler, &server_params);
                if (rv == 0){
                    //lock mutex
                    pthread_mutex_unlock(&server_params.mutex);
                }
            }
            
            // In case we are not receiving heartbeats from leader,
            // we still want to initiate election 
            // (this case is possible, if we get the client message)

            // probably, some validation should be added to deal with
            // running election, when we've been set to follower
            if (time_spent(last_heartbeat) > HEARTBEAT_TIMEOUT){
                initiate_election(config);
            }
        }
        else{
            printf("Leader actions\n");
            // leader actions
            int i = 0;
            for (i; i < NUMBER_OF_PORTS; i++){
                if (KNOWN_PORTS[i] != CURRENT_SERVER){
                    send_rpcs(KNOWN_PORTS[i]);
                }
            }
            sleep(5);
        }
        current_term++;
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

    state = 
    
    printf("%d\n", CURRENT_SERVER);
    int rv = run_server(&config);
    if (rv != 0){
        printf("Fail?\n");
    }

    return EXIT_SUCCESS;
}
