#define _GNU_SOURCE

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
#define CLIENT_PORT (8000)
#define DEFAULT_LOG_SIZE (100)
#define BAD_PORT (1)
#define MAX_PORT (65535)
#define IP_ADDR ("127.0.0.1")
#define MAX_CLIENTS (5)
#define NUMBER_OF_PORTS (2)
#define SELECT_TIMEOUT (5.0)    // in seconds
#define ELECTION_TIMEOUT (10.0)  // in seconds
#define CANDIDATE_TIMEOUT (5000) // in milliseconds
#define RECV_TIMEOUT (2)         // in seconds
#define min(x, y) (((x) < (y)) ? (x) : (y))
#define max(x, y) (((x) > (y)) ? (x) : (y))

// rv = retval = return value
// rc = return code

// TODO: improve leader election efficiency
// TODO: add rpc handling in following style:
//  first send message_t with type of message (type_e: AppendEntries, etc)
//  then send message ofchoosen type itself

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

struct timespec timeout = {
    .tv_sec = SELECT_TIMEOUT,
    .tv_nsec = 0,
};

struct timeval recv_timeout = {
    .tv_sec = RECV_TIMEOUT,
    .tv_usec = 0,
};

typedef struct command_t {
    int val;
    // TODO: extend command
} command_t;

typedef struct prepare_message_t {
    type_e type;
    whom_e whom;
    int from;
} prepare_message_t;

typedef struct prepare_message_client_t {
    int ready;
    int leader_id;
} prepare_message_client_t;

typedef struct entry_t {
    int term;
    int index;
    command_t command;
} entry_t;

typedef struct log_arr_t {
    int size;
    int last; // position to write, inclusive, starting from 1
    entry_t *entries;
} log_arr_t;

typedef struct response_t {
    int val;
    int term;
} response_t;

typedef struct ae_rpc_message_t {
    int term;
    int leader_id;
    int prev_log_index;
    int prev_log_term;
    int leader_commit;
    entry_t entries;
} ae_rpc_message_t;

typedef struct ae_rpc_response_t {
    int term;
    int success;
} ae_rpc_response_t;

typedef struct rv_rpc_message_t {
    int term;
    int candidate_id;
    int last_log_index;
    int last_log_term;
} rv_rpc_message_t;

typedef struct rv_rpc_response_t {
    int term;
    int vote_granted;
} rv_rpc_response_t;

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
    int from; // TODO: remove, change to candidate_id
    int candidate_id;  // port of server
    int last_log_index;
    int last_log_term;
    command_t command;
} message_t;

typedef struct entry_received_t {
    int term;
    int index;
    int commited;
} entry_received_t;

struct deque_entry_t;

typedef struct deque_entry_t {
    entry_t entry;
    struct deque_entry_t *next;
} deque_entry_t;

typedef struct deque_t {
    deque_entry_t *first;
    deque_entry_t *last;
} deque_t;

void *deque_add(deque_t *deque, entry_t entry){
    deque_entry_t *dentry = (deque_entry_t *)malloc(sizeof(deque_entry_t));
    dentry->entry = entry;
    dentry->next = NULL;

    if (deque->last == NULL){
        deque->last = dentry;
    }
    else{
        deque->last = deque->last->next = dentry;
    }

    if (deque->first == NULL){
        deque->first = deque->last;
    }
}

void *deque_pop(deque_t *deque){
    deque_entry_t *del_dentry = deque->first;
    if (del_dentry != NULL){
        deque->first = del_dentry->next;
        free(del_dentry);
    }
}

void *clear_deque(deque_t *deque){
    while (deque->first != NULL){
        deque_pop(deque);
    }
}

whom_e State = Follower;

// int SERVER_ID = SERVER_ID;
int SERVER_ID;
int KNOWN_PORTS[NUMBER_OF_PORTS] = {7500, 7501};
int leader = 0;
deque_t client_requests;  // needed to store all unresponded commands from client
                          // it's stored until not commited, after commit - send
                          // client a notification that this entry is commited
                          // but this doubles memory used for entries
int leader_id;

int last_log_index = 0; // this sohuld be equal to log_arr.last - 1
                        // index of last written log entry
int last_log_term = 0;  // term of last entry in log

int current_term = 0;
int voted_for = 0;
log_arr_t log_arr;
int votes = 0;

int commit_index = 0;
int last_applied = 0;

int next_index[NUMBER_OF_PORTS] = {0, 0, 0};
int match_index[NUMBER_OF_PORTS] = {0, 0, 0};

clock_t last_heartbeat;

command_t gen_command(){
    command_t command = {
        .val = rand(),
    };
    return command;
}

entry_t make_entry(command_t command, int term, int index){
    entry_t entry = {
        .term = term,
        .index = index,
        .command = command,
    };
    return entry;
}

void increase_log_size(log_arr_t *logg){
    logg->entries = 
        (entry_t *)realloc(logg->entries, 2 * logg->size * sizeof(entry_t));
    logg->size *= 2;
}

void push_back(log_arr_t *logg, entry_t entry){
    if (logg->last == logg->size){
        increase_log_size(logg);
    }
    logg->entries[logg->last++] = entry;
    last_log_index = entry.index;
    last_log_term = entry.term;
    match_index[SERVER_ID] = last_log_index;
}

void print_command(command_t command){
    printf("Val: %d\n", command.val);
}

void print_entry(entry_t entry){
    printf("Term %d, Index %d ", entry.term, entry.index);
    print_command(entry.command);
}

void print_log(log_arr_t logg){
    int i;
    printf("===================\n");
    printf("|| Printing log   ||\n");
    printf("===================\n");
    printf("Commitindex: %d\n\n", commit_index);
    printf("Log size: %d, log last %d\n", logg.size, logg.last);
    printf("\n");
    for (i = 0; i <= logg.last; i++){
        printf("Index %d: \n", i);
        print_entry(logg.entries[i]);
    }

    printf("===================\n");
    printf("|| Log is printed ||\n");
    printf("===================\n");
}

float rand_in_range(float a, float b){
    return a + (float)(rand())/(float)(RAND_MAX) * (b - a);
}

void flsh(){
    fflush(stdout);
    fflush(stderr);
}

void timestamp(){
    printf("Timestamp: %d\n",(int)time(NULL));
}

// Do not rely on this function when using `sleep`
double time_spent(clock_t start_time){
    return (double)(clock() - start_time) / CLOCKS_PER_SEC;
}

int eq_command(const command_t a, const command_t b){
    return a.val == b.val;
}

int eq_entries(const entry_t a, const entry_t b){
    return eq_command(a.command, b.command) && 
        a.index == b.index &&
        a.term == b.index;
}

int safe_leave(int sock, pthread_mutex_t *mutex){
    int rc;
    rc = close(sock);
    if (rc == -1){
        perror("ERROR IN CLOSE");
    }
    if (mutex != NULL){
        rc = pthread_mutex_unlock(mutex);
        if (rc != 0){
            perror("Error in unlock");
            return (EXIT_FAILURE);
        }
        printf("Leaving wrap_try_get_rpc -------------------^\n");
    }
    flsh();
    return (EXIT_SUCCESS);
}

void become_follower(int new_leader_id){
    printf("Become follower\n");
    State = Follower;
    voted_for = 0;
    votes = 0;
    if (new_leader_id != -1){
        leader_id = new_leader_id;
    }
    clear_deque(&client_requests);
}

void become_leader(){
    printf("Become Leader, votes = %d\n", votes);
    State = Leader;
    leader_id = SERVER_ID;
    int i;
    for (i = 0; i < NUMBER_OF_PORTS; i++){
        if (i != SERVER_ID){
            next_index[i] = log_arr.last;
        }
    }
}

void initiate_election(config_t *config){
    printf("Initiate election\n");
    // current_term++;
    State = Candidate;
    voted_for = SERVER_ID;
    votes = 1;
    //reset election timer? 
}

int send_prep_message(int sock, type_e type, whom_e whom, int from){
    prepare_message_t prep_mess = { 
        .type = type,
        .whom = whom,
        .from = from
    };
    int rc;
    rc = send(sock, &prep_mess, sizeof(prep_mess), 0);
    if (rc <= 0){
        perror("Error in send in send prep message");
        return (EXIT_FAILURE);
    }

    int ready = 0;
    rc = recv(sock, &ready, sizeof(ready), 0);
    if (rc <= 0 || !ready){
        perror("Error in recv in send prep message");
        return (EXIT_FAILURE);
    }

    return (EXIT_SUCCESS);
}

int request_vote(int receiver_id){
    int port = KNOWN_PORTS[receiver_id];
    printf("Request vote rpc %d\n", port);
    struct sockaddr_in peer;
    peer.sin_family = AF_INET;
    peer.sin_port = htons(port);
    peer.sin_addr.s_addr = inet_addr(IP_ADDR);

    int sock;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("ошибка вызова socket");
        safe_leave(sock, NULL);
        return (EXIT_FAILURE);
    }

    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));
    int rc = connect(sock, (struct sockaddr *)&peer, sizeof(peer)); 
    if (rc != 0) {
        perror("ошибка вызова connect");
        safe_leave(sock, NULL);
        return (EXIT_FAILURE);        
    }
    printf("Connected %d %d\n", SERVER_ID, port);
    rc = send_prep_message(sock, RequestVote, Candidate, SERVER_ID);
    if (rc != EXIT_SUCCESS){
        perror("Error in prep message in request vote");
        safe_leave(sock, NULL);
        return (EXIT_FAILURE);
    }

    rv_rpc_message_t message = {
        .term = current_term,
        .candidate_id = SERVER_ID,
        .last_log_index = last_log_index,
        .last_log_term = last_log_term,
    };

    rc = send(sock, &message, sizeof(message), 0);
    if (rc <= 0) {
        perror("ошибка вызова send");
        safe_leave(sock, NULL);
        return (EXIT_FAILURE);
    }

    rv_rpc_response_t response;
    rc = recv(sock, &response, sizeof(response), 0);
    if (rc <= 0) {
        perror("ошибка вызова recv");
        safe_leave(sock, NULL);        
        return (EXIT_FAILURE);
    }

    if (response.term > current_term){
        current_term = response.term;
        become_follower(receiver_id);
    }

    votes += response.vote_granted;
    safe_leave(sock, NULL);
    printf("Received %d\n", response.vote_granted);
    return (EXIT_SUCCESS);
}

void *handle_request_vote_rpc(int sock){
    int rc;
    int ready = 1;
    rc = send(sock, &ready, sizeof(ready), 0);
    if (rc <= 0){
        perror("Ошибка вызова send в handle_request_vote_rpc");
        safe_leave(sock, NULL);
        return ((void *)EXIT_FAILURE);
    }

    rv_rpc_message_t message;
    rc = recv(sock, &message, sizeof(message), 0);
    if (rc <= 0) {
        perror("Error in handle_request_vote_rpc recv");
        safe_leave(sock, NULL);
        return ((void *)EXIT_FAILURE);
    }

    rv_rpc_response_t response = {
        .term = current_term,
        .vote_granted = 0,
    };

    if (message.term < current_term){
        //1. Reply false if term < currentTerm (§5.1)
    }
    else {
        // 2. If votedFor is null or candidateId, and candidate’s log is at
        // least as up-to-date as receiver’s log, grant vote (§5.2, §5.4)
        if ((!voted_for || voted_for == message.candidate_id) && 
        message.last_log_index >= last_log_index && 
        message.last_log_term >= last_log_term){
            response.vote_granted = 1;
        }
    }

    rc = send(sock, &response, sizeof(response), 0);
    printf("Sending data to Candidate vote_granted: %d\n",response.vote_granted);
    if (rc <= 0){
        perror("Error in handle_request_vote_rpc send");
        safe_leave(sock, NULL);
        return ((void *)EXIT_FAILURE);
    }

    return ((void *) EXIT_SUCCESS);
}

void *handle_append_entries_rpc(int sock){
    printf("Handling append entry rpc\n");

    int rc;
    int ready = 1;
    rc = send(sock, &ready, sizeof(ready), 0);
    if (rc <= 0){
        perror("Ошибка вызова send в handle_append_entries_rpc");
        safe_leave(sock, NULL);
        return ((void *)EXIT_FAILURE);
    }

    ae_rpc_message_t message;
    rc = recv(sock, &message, sizeof(message), 0);
    if (rc <= 0) {
        perror("Error in handle_append_entries_rpc recv");
        safe_leave(sock, NULL);
        return ((void *)EXIT_FAILURE);
    }

    ae_rpc_response_t response = {
        .term = current_term,
        .success = 0,
    };

    int entry_index = message.entries.index;

    if (message.term < current_term){
        // Reply false if term < currentTerm
    }
    else if (log_arr.size <= message.prev_log_index || 
             log_arr.entries[message.prev_log_index].term != message.prev_log_term){
        // 2. Reply false if log doesn’t contain an entry at prevLogIndex
        // whose term matches prevLogTerm (§5.3)
    }
    else {
        // 3. If an existing entry conflicts with a new one (same index
        // but different terms), delete the existing entry and all that
        // follow it (§5.3)

        if (entry_index != -1) { // lets differentiate heartbeats by this 
            if (entry_index >= log_arr.size){
                increase_log_size(&log_arr);
            }
            if (eq_entries(log_arr.entries[entry_index], message.entries)){
                memset(&log_arr.entries[entry_index], 0,
                 (log_arr.last - entry_index) * sizeof(entry_t));
                log_arr.last = entry_index;
            }
            
            // 4. Append any new entries not already in the log
            push_back(&log_arr, message.entries);
            printf("==========\n");
            printf("Received entry from leader:\n");
            print_entry(message.entries);
            printf("==========\n");
        }
        // 5. If leaderCommit > commitIndex, set commitIndex =
        // min(leaderCommit, index of last new entry)
        if (message.leader_commit > commit_index){
            commit_index = min(message.leader_commit, entry_index);
        }
        response.success = 1;
        become_follower(message.leader_id);
    }

    rc = send(sock, &response, sizeof(response), 0);
    if (rc <= 0){
        perror("Error in handle_append_entries_rpc send");
        safe_leave(sock, NULL);
        return ((void *)EXIT_FAILURE);
    }

    return ((void *) EXIT_SUCCESS);
}

void *handle_client(int sock){
    int rc;
    prepare_message_client_t response = {
        .ready = 0,
        .leader_id = leader_id,
    };

    if (leader_id == SERVER_ID){
        // we are leader, handle client
        response.ready = 1;
    }
    rc = send(sock, &response, sizeof(response), 0);
    if (rc <= 0){
        perror("Error in send in handle_client");
        return ((void *)EXIT_FAILURE);
    }

    if (leader_id != SERVER_ID){
        return ((void *)EXIT_SUCCESS);
    }

    // we are leadet and can recieve command from client

    command_t command;
    rc = recv(sock, &command, sizeof(command), 0);
    if (rc <= 0){
        perror("Error in recv in handle_client");
        return ((void *)EXIT_FAILURE);
    }

    entry_t entry = make_entry(command, current_term, log_arr.last);
    push_back(&log_arr, entry);
    printf("===========\n");
    printf("Received entry from client\n");
    print_entry(entry);
    printf("===========\n");
    deque_add(&client_requests, entry);

    entry_received_t entry_received = {
        .term = entry.term,
        .index = entry.index,
        .commited = 0,
    };
    rc = send(sock, &entry_received, sizeof(entry_received), 0);
    if (rc <= 0){
        perror("Error in send in handle_client");
        return ((void *)EXIT_FAILURE);
    }
    return ((void *)EXIT_SUCCESS);
}

void *rpc_handler(int fd, pthread_mutex_t *vote_mutex) {
    // client_params_t *_client_params = arg;
    // client_params_t client_params = *_client_params;
    // pthread_mutex_unlock(&_client_params->mutex);
    
    int rc;
    prepare_message_t message;

    rc = recv(fd, &message, sizeof(message), 0);
    if (rc <= 0) {
        printf("Failed\n");
        return ((void *)EXIT_FAILURE);
    }

    printf("Received prep message from %d, my term: %d\n", message.from, current_term);
    // Differentiate between clients and leader/other servers
    switch (message.whom){
        case Client:;
            // handle client
            handle_client(fd);
            break;
        case Follower:;  // Follower can only redirect clients (?)
            // handle follower
            break;
        case Candidate:;  // Candidate can send only RequestVote (?)
            // handle candidate
            pthread_mutex_lock(vote_mutex); 
            handle_request_vote_rpc(fd);
            pthread_mutex_unlock(vote_mutex); 
            break;
        case Leader:;
            handle_append_entries_rpc(fd);
            break;
        default:;
            perror("Unknown peer");
    }
    return ((void *)EXIT_SUCCESS);
}

void *send_append_entries_rpc(int port, int follower_id){
    printf("Send append entries\n");

    struct sockaddr_in peer;
    peer.sin_family = AF_INET;
    peer.sin_port = htons(port);
    peer.sin_addr.s_addr = inet_addr(IP_ADDR);

    int sock;
    int rc;
    int hearbeat = 0;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("ошибка вызова socket");
        return ((void *)EXIT_FAILURE);
    }

    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));
    rc = connect(sock, (struct sockaddr *)&peer, sizeof(peer)); 
    if (rc != 0) {
        perror("ошибка вызова connect");
        safe_leave(sock, NULL);
        return ((void *)EXIT_FAILURE);        
    }

    // send notificiation message about incoming append entries:

    rc = send_prep_message(sock, AppendEntries, State, SERVER_ID);
    if (rc != (EXIT_SUCCESS)){
        perror("Error in send prep message");
        safe_leave(sock, NULL);
        return ((void *)EXIT_FAILURE);
    }

    int prev_log_index = next_index[follower_id] - 1;

    if (next_index[follower_id] >= log_arr.size){
        increase_log_size(&log_arr);
    }

    ae_rpc_message_t message = {
        .term = current_term,
        .leader_id = SERVER_ID,
        .prev_log_index = prev_log_index,
        .prev_log_term = log_arr.entries[prev_log_index].term,
        .entries = log_arr.entries[next_index[follower_id]],
        .leader_commit = commit_index,        
    };

    if (next_index[follower_id] >= last_log_index){  //heartbeat
        message.entries.index = -1;
        hearbeat = 1;
    }

    rc = send(sock, &message, sizeof(message), 0);
    if (rc <= 0) {
        perror("ошибка вызова send");
        safe_leave(sock, NULL);
        return ((void *)EXIT_FAILURE);
    }
    printf("Send success\n");

    ae_rpc_response_t response;
    rc = recv(sock, &response, sizeof(response), 0);
    if (rc <= 0) {
        perror("ошибка вызова recv");
        safe_leave(sock, NULL);
        return ((void *)EXIT_FAILURE);
    }

    if (response.term > current_term){
        current_term = response.term;
        become_follower(follower_id);
    }
    else {
        if (response.success) {
            if (!hearbeat){
                match_index[follower_id] = next_index[follower_id];
                next_index[follower_id]++;
            }
        }
        else {
            next_index[follower_id]--;
            // retry;
        }
    }
    flsh();
    safe_leave(sock, NULL);
    printf("Received %d\n", response.success);
    return ((void *)EXIT_SUCCESS);
}

void *send_append_entries_rpc_to_all(){
    // should be parallel
    int i;
    for (i = 0; i < NUMBER_OF_PORTS; i++){
        if (i == SERVER_ID){ continue; }
        send_append_entries_rpc(KNOWN_PORTS[i], i);
    }
}

void *send_request_vote_rpc_to_all(){
    // TODO: Make it parallel
    int i;
    int rc;
    for (i = 0; i < NUMBER_OF_PORTS && State == Candidate; i++){
        if (i == SERVER_ID){ continue; }
        // retry
        int j;
        for (j = 0; j < 2; j++){
            rc = request_vote(i);
            if (rc == EXIT_SUCCESS){
                break;
            }
        }
    }
}

typedef struct try_get_rpc_t {
    int sock;
    int result;
    int follower;
    config_t * config;
    pthread_mutex_t mutex;
    pthread_mutex_t vote_mutex;
} try_get_rpc_t;

void *try_get_rpc(void *arg){
    try_get_rpc_t *_params = arg;
    try_get_rpc_t params = *_params;
    int sock = params.sock;

    // wait for incoming messages
    fd_set sockets;
    FD_ZERO(&sockets);  //clear set
    FD_SET(sock, &sockets); //add sock to set

    // using pselect instead of select because select may decrease the timeout
    int sel = pselect(sock + 1, &sockets, NULL, NULL, &timeout, NULL); 
    printf("Select %d\n", sel);
    if (sel < 0){
        perror("Ошибка select");
        return ((void *)EXIT_FAILURE);
    }

    if (params.follower && sel == 0){
        initiate_election(params.config);
        return ((void *)EXIT_SUCCESS);
    }

    if (FD_ISSET(sock, &sockets)){ 
        // struct sockaddr_in client_name;
        // socklen_t client_name_len;
        // int fd = accept(sock, &client_name, &client_name_len);
        int rc;
        int fd = accept(sock, NULL, NULL);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));
        printf("accepted, got called in try_get_rpc\n");
        if (fd <= 0){
            perror("Error in accept");
            return ((void *)EXIT_FAILURE);
        }
        rc = rpc_handler(fd, &_params->vote_mutex);
        // In case we are not receiving heartbeats from leader,
        // we still want to initiate election 
        // (this case is possible, if we get the client message)

        // TODO: probably, some validation should be added to deal with
        // running election, when we've been set to follower
        if (params.follower && time_spent(last_heartbeat) > ELECTION_TIMEOUT){
            initiate_election(params.config);
        }
        close(fd);
    }
    return ((void *)EXIT_SUCCESS);
}


void *wrap_try_get_rpc(void *arg){
    printf("wrap try get rpc -------------------v\n");
    try_get_rpc_t *_params = arg;
    try_get_rpc_t params = *_params;
    int rc;
    rc = pthread_mutex_lock(&_params->mutex);
    if (rc != 0){
        perror("Error in lock");
        return ((void *)EXIT_FAILURE);
    }
    struct sockaddr_in local;
    // tuning the server
    memset(&local, 0, sizeof(struct sockaddr_in));
    local.sin_family = AF_INET;
    local.sin_port = htons(KNOWN_PORTS[SERVER_ID]);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror ("ошибка вызова socket");
        return ((void *)EXIT_FAILURE);
    }

    // make socket reusable. After `close(sock)` there is a timeout (due to OS)
    // when address becomes available to bind again. Thus, make it reusable to
    // not get "Address already in use" error.
    int enable = 1;
    // https://hea-www.harvard.edu/~fine/Tech/addrinuse.html
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0){
        perror("setsockopt(SO_REUSEADDR) failed");
        safe_leave(sock, &_params->mutex);
        return ((void *)EXIT_FAILURE);
    }

    // make socket non blocking
    rc = fcntl(sock, F_SETFL, O_NONBLOCK);
    if (rc < 0){
        perror("Error in fcntl");
        safe_leave(sock, &_params->mutex);
        return ((void *)EXIT_FAILURE);
    }
    rc = bind(sock, (struct sockaddr *)&local, sizeof(local));
    if (rc < 0) {
        perror("ошибка вызова bind in wrap!");
        safe_leave(sock, &_params->mutex);
        return ((void *)EXIT_FAILURE);
    }

    // setting maximum number of connections
    rc = listen(sock, MAX_CLIENTS);
    if (rc) {
        perror("ошибка вызова listen");
        safe_leave(sock, &_params->mutex);
        return ((void *)EXIT_FAILURE);
    }

    params.sock = sock;
    try_get_rpc(&params);
    safe_leave(sock, &_params->mutex);
    return ((void *)EXIT_SUCCESS);
}

void mysleep(int ms){
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

void apply(entry_t entry){
    // apply entry to state machine
}

void *respond_to_client(void *arg){
    int rc;
    while(client_requests.first != NULL && 
     client_requests.first->entry.index <= commit_index){
        entry_t entry = client_requests.first->entry;

        int port = CLIENT_PORT;
        printf("Updating client with index %d\n", entry.index);
        struct sockaddr_in peer;
        peer.sin_family = AF_INET;
        peer.sin_port = htons(port);
        peer.sin_addr.s_addr = inet_addr(IP_ADDR);

        int sock;
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("ошибка вызова socket");
            safe_leave(sock, NULL);
            return ((void *)EXIT_FAILURE);
        }

        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));
        int rc = connect(sock, (struct sockaddr *)&peer, sizeof(peer)); 
        if (rc != 0) {
            perror("ошибка вызова connect");
            safe_leave(sock, NULL);
            return ((void *)EXIT_FAILURE);        
        }
        printf("Connected %d %d\n", SERVER_ID, port);
        entry_received_t entry_received = {
            .index = entry.index,
            .term = entry.term,
            .commited = 1,
        };

        rc = send(sock, &entry_received, sizeof(entry_received), 0);
        if (rc <= 0) {
            perror("ошибка вызова send");
            safe_leave(sock, NULL);
            return ((void *)EXIT_FAILURE);
        }

        safe_leave(sock, NULL);
        printf("Entry with index %d is sent to client\n", entry.index);
        return ((void *)EXIT_SUCCESS);
    }
}

void *update_client(pthread_mutex_t *client_mutex){
    printf("Update client\n");

    pthread_t thread;
    pthread_mutex_lock(client_mutex);
    pthread_create(&thread, NULL, respond_to_client, NULL);
    pthread_mutex_unlock(client_mutex);
}

int run_server(config_t *config) {
    printf("Running %d\n", config->port);

    last_heartbeat = clock();
    try_get_rpc_t params;
    memset(&params, 0, sizeof(params));

    pthread_mutex_t client_mutex;
    pthread_mutex_init(&params.mutex, NULL);
    pthread_mutex_init(&params.vote_mutex, NULL);
    pthread_mutex_init(&client_mutex, NULL);

    for (current_term;current_term < 15;) {
        printf("Current term %d\n", current_term);

        while (commit_index > last_applied){
            last_applied++;
            apply(log_arr.entries[last_applied]);
        }

        int i = 0;
        int rc;
        pthread_t thread;

        switch (State){
            case Follower:;
                printf("I'm Follower\n");
                timestamp();
                // become_follower(leader_id);
                params.follower = 1;
                printf("Waiting for incoming messages\n");
                wrap_try_get_rpc(&params);
                break;
            case Candidate:;
                // create thread to get possible append_entries rpc 
                params.config = config;
                params.follower = 0;
                pthread_create(&thread, NULL, wrap_try_get_rpc, &params);
                int sleep_time = (int)rand_in_range(0.0, CANDIDATE_TIMEOUT);
                printf("I'm Candidate, sleeping %d\n", sleep_time);
                timestamp();
                mysleep(sleep_time);

                // election, trying to get votes.
                // in case we got rpc to become follower
                // in try_get_rpc thread stop requesting
                send_request_vote_rpc_to_all();
                
                // first outcome in perfect situation 
                // (no other server is claimed as leader)
                if (votes > NUMBER_OF_PORTS / 2 && State == Candidate){
                    become_leader();
                }

                // pthread_kill will free mutex
                pthread_mutex_lock(&params.vote_mutex);
                rc = pthread_kill(thread, NULL);
                if (rc != 0){
                    perror("Error in pthread_kill");
                }
                pthread_mutex_unlock(&params.vote_mutex);
                break;
            case Leader:;
                printf("I'm Leader <<<<<<<<<<<<<<<\n");
                timestamp();
                update_client(&client_mutex);

                // leader actions
                int heartbeat = 0;
                send_append_entries_rpc_to_all();
                flsh();
                
                // simulate incoming message from client (append entry by now)
                entry_t entry = make_entry(gen_command(), current_term, log_arr.last);
                push_back(&log_arr, entry);
                printf("===========\n");
                printf("Generated entry\n");
                print_entry(entry);
                printf("===========\n");
                
                // used for testing
                                
                // If there exists an N such that N > commitIndex, a majority
                // of matchIndex[i] ≥ N, and log[N].term == currentTerm:
                // set commitIndex = N (§5.3, §5.4).

                int new_ci = commit_index;
                for(new_ci; new_ci <= last_log_index; new_ci++){
                    int j;
                    int cnt = 0;
                    for (j = 0; j < NUMBER_OF_PORTS; j++){
                        cnt += (match_index[j] >= new_ci); // && log_arr.entries[new_ci].term == current_term);  // TODO: do we need this condition?
                    }
                    if (cnt > NUMBER_OF_PORTS / 2){
                        commit_index = new_ci;
                        printf("Commit index is updated: %d\n", commit_index);
                    }
                }

                printf("Leader sleeping\n");
                sleep(5);
                break;
            default:
                printf("Unknown state %d", State);
                break;
        }

        flsh();
        current_term++;
    }

    print_log(log_arr);
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
    srand(time(NULL));
    parse_str(&SERVER_ID, argv[1]);
    config_t config = {
        .port = KNOWN_PORTS[SERVER_ID],
    };
    
    log_arr.size = DEFAULT_LOG_SIZE;
    log_arr.last = 1;
    log_arr.entries = (entry_t *)malloc(sizeof(entry_t) * DEFAULT_LOG_SIZE);

    int rc = run_server(&config);
    if (rc != 0){
        printf("Fail?\n");
    }

    return EXIT_SUCCESS;
}
