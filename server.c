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
#define ELECTION_TIMEOUT (10.0)
#define CANDIDATE_TIMEOUT (150) // in milliseconds
#define min(x, y) (((x) < (y)) ? (x) : (y))
#define max(x, y) (((x) > (y)) ? (x) : (y))

typedef enum whom_e {
    Unknown,
    Client,
    Follower,
    Candidate,
    Leader,
} whom_e;

typedef enum type_e {
    RequestVote,
    AppendEntries,
    HeartBeat,
    Operation,
} type_e;

// initially all servers start in follower state
whom_e State = Follower;

int CURRENT_SERVER = DEFAULT_PORT;
int KNOWN_PORTS[NUMBER_OF_PORTS] = {7500, 7501};
int leader = 0;

int current_term = 0;
int voted_for = 0;
int *logm = NULL;
int votes = 0;

int commit_index = 0;
int last_applied = 0;

int *next_index = NULL;
int *match_index = NULL;

clock_t last_heartbeat;

struct timeval timeout  = {
    .tv_sec = SELECT_TIMEOUT,
    .tv_usec = 0,
};

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
    type_e type;
    whom_e whom;  // type of server (Candidate, Leader, Client etc)
    int term;   
    int from;  // port of server
    command_t command;
} message_t;

float rand_in_range(float a, float b){
    return a + (float)(rand())/(float)(RAND_MAX) * (b - a);
}

int request_vote(int port, response_t *response){
    printf("Request vote rpcs %d\n", port);
    struct sockaddr_in peer;
    peer.sin_family = AF_INET;
    peer.sin_port = htons(port);
    peer.sin_addr.s_addr = inet_addr(IP_ADDR);

    int sock;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("ошибка вызова socket");
        return (EXIT_FAILURE);
    }

    int rc = connect(sock, (struct sockaddr *)&peer, sizeof(peer)); 
    if (rc != 0) {
        perror("ошибка вызова connect");
        return (EXIT_FAILURE);        
    }
    printf("Connected %d %d\n", CURRENT_SERVER, port);
    message_t message = {
        .whom = Candidate,
        .term = current_term,
        .type = RequestVote,
        .from = CURRENT_SERVER,
    };
    if ((rc = send(sock, &message, sizeof(message), 0)) <= 0) {
        perror("ошибка вызова send");
        return (EXIT_FAILURE);
    }
    printf("Sent success\n");
    if ((rc = recv(sock, response, sizeof(*response), 0)) <= 0) {
        perror("ошибка вызова recv");
        return (EXIT_FAILURE);
    }
    
    close(sock);
    printf("Recieved %d\n", response->val);
    return (EXIT_SUCCESS);
}

void initiate_election(config_t *config){
    printf("Initiate election\n");
    // current_term++;
    State = Candidate;
    voted_for = CURRENT_SERVER;
    votes = 1;
    //reset election timer? 
}

void become_follower(){
    State = Follower;
    voted_for = NULL;
    votes = 0;
    last_heartbeat = clock();
}

void *rpc_handler(void *arg, message_t *message) {
    client_params_t *_client_params = arg;
    client_params_t client_params = *_client_params;
    pthread_mutex_unlock(&_client_params->mutex);
    
    int rc = recv(client_params.fd, message, sizeof(*message), 0);
    if (rc <= 0) {
        printf("Failed\n");
        return ((void *)EXIT_FAILURE);
    }

    printf("Received %d\n", message->command.val);
    if (message->term > current_term){
        become_follower();
    }
    current_term = max(message->term, current_term);

    // Differentiate between clients and leader/other servers
    switch (message->whom){
        case Client:;
            // handle client
            break;
        case Follower:;  // Follower can only redirect clients (?)
            // handle follower
            break;
        case Candidate:;  // Candidate can send only RequestVote (?)
            // handle candidate
            response_t response = {.val = 0,};
            if (voted_for == NULL){
                voted_for = message->from;
                response.val = 1;
            }
            rc = send(client_params.fd, &response, sizeof(response), 0);
            break;
        case Leader:;
            // handle leader
            break;
        default:;
            perror("Unknown peer");
    }


    // Return something in response, this should go into switch statement
    response_t response;
    response.val = 2;
    rc = send(client_params.fd, &response, sizeof(response), 0);
    if (rc <= 0) {
        perror("ошибка вызова send");
    }
    return ((void *)EXIT_SUCCESS);
}

double time_spent(clock_t start_time){
    return (clock() - start_time) / CLOCKS_PER_SEC;
}

void *send_append_entry_rpc(int port){
    printf("Sending rpcs\n");
    
    // TODO: remove this
    return NULL;

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
    if ((rc = send(sock, &command, sizeof(command), 0)) <= 0) {
        perror("ошибка вызова send");
        return ((void *)EXIT_FAILURE);
    }
    printf("Sent success\n");
    if ((rc = recv(sock, &response, sizeof(response), 0)) <= 0) {
        perror("ошибка вызова recv");
        return ((void *)EXIT_FAILURE);
    }
    
    close(sock);
    printf("Recieved %d\n", response.val);
    return ((void *)EXIT_SUCCESS);
}


typedef struct try_get_rpc_t {
    int sock;
    int result;
    int follower;
    config_t * config;
} try_get_rpc_t;

void *try_get_rpc(void *arg){
    printf("Try get rps\n");
    try_get_rpc_t *_params = arg;
    try_get_rpc_t params = *_params;
    // message_t message;
    int sock = params.sock;
    
    // wait for incoming messages
    fd_set sockets;
    FD_ZERO(&sockets);
    FD_SET(sock, &sockets);

    int sel = select(sock + 1, &sockets, NULL, NULL, &timeout); 
    
    printf("Select %d\n", sel);
    if (sel < 0){
        perror("Ошибка select");
        return ((void *)EXIT_FAILURE);
    }

    if (params.follower && sel == 0){
        initiate_election(params.config);
        return NULL;
    }

    if (FD_ISSET(sock, &sockets)){
        int fd = accept(sock, NULL, NULL);
        printf("accepted, got called in try_get_rpc\n");

        if (fd < 0){
            perror("Error in accept");
            return ((void *)EXIT_FAILURE);
        }

        server_params_t server_params = {
            .config = params.config,
            .fd = fd,
        };

        message_t message;
        int rc = rpc_handler((void *)&server_params, &message);

        //interpret message result
        if (message.term > current_term){
            
        }
        _params->result = 0;

        // In case we are not receiving heartbeats from leader,
        // we still want to initiate election 
        // (this case is possible, if we get the client message)

        // TODO: probably, some validation should be added to deal with
        // running election, when we've been set to follower
        if (params.follower && time_spent(last_heartbeat) > ELECTION_TIMEOUT){
            initiate_election(params.config);
            return NULL;
        }
    }
}

void mysleep(int ms){
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
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

    // make socket non blocking
    fcntl(sock, F_SETFL, O_NONBLOCK);

    printf("Socket %d\n", sock);
    rc = bind(sock, (struct sockaddr *)&local, sizeof(local));
    if (rc < 0) {
        perror("ошибка вызова bind");
        return (EXIT_FAILURE);
    }

    // setting the maximum number of clients (connections)
    rc = listen(sock, MAX_CLIENTS);
    if (rc) {
        perror("ошибка вызова listen");
        return (EXIT_FAILURE);
    }

    last_heartbeat = clock();

    for (current_term;;) {
        printf("Curent term %d\n", current_term);
        try_get_rpc_t params;
        memset(&params, 0, sizeof(params));
        int i = 0;
        switch (State){
            case Follower:;
                printf("I'm Follower\n");
                become_follower();
                params.follower = 1;
                try_get_rpc((void *)&params);
                break;
            case Candidate:;
                printf("I'm Candidate\n");
                mysleep((int)rand_in_range(0.0, CANDIDATE_TIMEOUT));
                // create thread to get possible append_entries rps 
                pthread_t thread;
                params.sock = sock;
                params.config = config;
                params.follower = 0;
                int rv = pthread_create(&thread, NULL, try_get_rpc, &params);
                
                // election, trying to get votes, in case we got rpc in thread ^,
                // stop requesting
                for (i = 0; i < NUMBER_OF_PORTS && State == Candidate; i++){
                    if (KNOWN_PORTS[i] != CURRENT_SERVER){
                        response_t response;
                        if ((request_vote(KNOWN_PORTS[i], &response)) != EXIT_SUCCESS){
                            //retry
                        }
                        votes += response.val;
                    }
                }
                
                // first outcome in perfect situation 
                // (no other server is claimed as leader)
                if (votes > NUMBER_OF_PORTS / 2 && State == Candidate){
                    State = Leader;
                    //send_heartbeat();
                }
                break;
            case Leader:;
                // leader actions
                for (i = 0; i < NUMBER_OF_PORTS; i++){
                    if (KNOWN_PORTS[i] != CURRENT_SERVER){
                        send_append_entry_rpc(KNOWN_PORTS[i]);
                    }
                }
                // used for testing
                sleep(5);
                break;
            default:
                printf("Unknown state %d", State);
                break;
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
    
    printf("%d\n", CURRENT_SERVER);
    int rc = run_server(&config);
    if (rc != 0){
        printf("Fail?\n");
    }

    return EXIT_SUCCESS;
}
