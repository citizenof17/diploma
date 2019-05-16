#define _GNU_SOURCE

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>

#define DEFAULT_PORT (7500)
#define CLIENT_PORT (8000)
#define DEFAULT_LOG_SIZE (3)
#define BAD_PORT (1)
#define MAX_PORT (65535)
#define IP_ADDR ("127.0.0.1")
#define MAX_CLIENTS (5)
#define NUMBER_OF_PORTS (3)
#define TERM_TIMEOUT (15) //in seconds
#define APPEND_ENTRIES_TIMEOUT (3000) // in milliseconds
#define SELECT_TIMEOUT (10)    // in seconds
#define CONNECT_TIMEOUT (1) // in seconds
#define ELECTION_TIMEOUT (5.0)  // in seconds
#define CANDIDATE_TIMEOUT (5000) // in milliseconds
#define RECV_TIMEOUT (2)         // in seconds
#define min(x, y) (((x) < (y)) ? (x) : (y))
#define max(x, y) (((x) > (y)) ? (x) : (y))

// rv = retval = return value
// rc = return code

// TODO: improve leader election efficiency

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

struct timespec select_timeout = {
    .tv_sec = SELECT_TIMEOUT,
    .tv_nsec = 0,
};

struct timespec connect_timeout = {
    .tv_sec = CONNECT_TIMEOUT,
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
    int term;
} prepare_message_t;

typedef struct prepare_message_response_t {
    int ready;
    int term;
} prepare_message_response_t;

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
    int keep_connection;
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
    int vote_granted;
    int term;
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

typedef struct mutex_id_t {
    pthread_mutex_t *mutex;
    pthread_mutex_t *mtx;
    int id;
} mutex_id_t;

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
int KNOWN_PORTS[NUMBER_OF_PORTS] = {7500, 7501, 7502};
int leader = 0;
deque_t client_requests;  // needed to store all unresponded commands from client
                          // it's stored until not commited, after commit - send
                          // client a notification that this entry is commited
                          // but this doubles memory used for entries
int leader_id;

int last_log_index = 0; // this sohuld be equal to log_arr.last - 1
                        // index of last written log entry
int last_log_term = 0;  // term of last entry in log

int current_term = 1;
int voted_term = 0;
int voted_for = 0;
log_arr_t log_arr;
int votes = 0;

int commit_index = 0;
int last_applied = 0;

int next_index[NUMBER_OF_PORTS] = {1, 1, 1};
int match_index[NUMBER_OF_PORTS] = {0, 0, 0};

// auxiliary, = 1 if we should send more append entries to server
int keep_connection[NUMBER_OF_PORTS] = {1, 1};

clock_t last_heartbeat;
time_t last_heartbeat_time;

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

void become_follower(int new_leader_id, char *buf){
    printf("Become follower in %s\n", buf);

    time(&last_heartbeat_time);
    State = Follower;
    if (voted_term != current_term){
        voted_for = 0;
    }
    votes = 0;
    if (new_leader_id != -1){
        leader_id = new_leader_id;
    }

    clear_deque(&client_requests);
}

int update_self_to_follower(int new_term, int new_leader_id){
    if (new_term > current_term){
        printf("Updating self to follower, new_term: %d, old term %d; ", new_term, current_term);
        become_follower(new_leader_id, "update self to follower");
        current_term = new_term;
        return 1; //updated
    }
    return 0; //not updated
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
    for (i = 0; i < logg.last; i++){
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

void mysleep(int ms){
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    last_heartbeat -= (clock_t)((double)ms / 1000.0 * CLOCKS_PER_SEC);
    nanosleep(&ts, NULL);
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

/* return time spent in seconds */
double time_spent_time(time_t start_time){  
    return time(NULL) - start_time;
}

int eq_command(const command_t a, const command_t b){
    return a.val == b.val;
}

int eq_entries(const entry_t a, const entry_t b){
    return eq_command(a.command, b.command) && 
        a.index == b.index &&
        a.term == b.index;
}

int safe_leave(int sock, pthread_mutex_t *mutex, int ind){
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
        printf("Mutex is unlocked in safe_leave\n");
        if (ind != -1){
            printf("Leaving wrap_try_get_rpc -------------------^ %d\n", ind);
        }
    }
    flsh();
    return (EXIT_SUCCESS);
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

void initiate_election(){ //become_candidate
    printf("Initiate election\n");
    current_term++;
    State = Candidate;
    voted_for = SERVER_ID;
    voted_term = current_term;
    votes = 1;
    //reset election timer? 
}

int send_prep_message(int sock, type_e type, whom_e whom, int from){
    prepare_message_t prep_message = { 
        .type = type,
        .whom = whom,
        .from = from,
        .term = current_term,
    };
    int rc;
    rc = send(sock, &prep_message, sizeof(prep_message), 0);
    if (rc <= 0){
        perror("Error in send in send prep message");
        return (EXIT_FAILURE);
    }

    prepare_message_response_t prep_response;
    memset(&prep_response, 0, sizeof(prep_response));
    rc = recv(sock, &prep_response, sizeof(prep_response), 0);
    if (rc <= 0){
        // if 'Resource temporarily unavailable':
        // The socket is marked nonblocking and the receive operation
        // would block, or a receive timeout had been set and the timeout
        // expired before data was received.  POSIX.1 allows either error
        // to be returned for this case, and does not require these
        // constants to have the same value, so a portable application
        // should check for both possibilities.
        // http://man7.org/linux/man-pages/man2/recvmsg.2.html
        perror("Error in recv in send prep message");
        return (EXIT_FAILURE);
    }

    if (!prep_response.ready){
        return (EXIT_FAILURE);
    }

    return (EXIT_SUCCESS);
}

int connect_with_timeout(int sock, struct sockaddr_in *address){
    fcntl(sock, F_SETFL, O_NONBLOCK);
    connect(sock, (struct sockaddr *)address, sizeof(*address));
    
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(sock, &fdset);

    int rc = pselect(sock + 1, NULL, &fdset, NULL, &connect_timeout, NULL);
    if (rc <= 0){
        return (EXIT_FAILURE);
    }

    // set back to blocking 
    int opts = fcntl(sock, F_GETFL);
    if (opts < 0){
        perror("Error in getting opts");
        return (EXIT_FAILURE);
    }
    opts &= (~O_NONBLOCK);
    rc = fcntl(sock, F_SETFL, opts);
    if (rc < 0){
        perror("Error in setting back to blocking");
        return (EXIT_FAILURE);
    }
    
    if (FD_ISSET(sock, &fdset)){
        return (EXIT_SUCCESS);
    }
    else{
        return (EXIT_FAILURE);
    }
}

int send_request_vote_rpc(int receiver_id){
    int port = KNOWN_PORTS[receiver_id];
    printf("Request vote rpc %d\n", port);
    struct sockaddr_in peer;
    peer.sin_family = AF_INET;
    peer.sin_port = htons(port);
    peer.sin_addr.s_addr = inet_addr(IP_ADDR);

    int sock;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("ошибка вызова socket");
        safe_leave(sock, NULL, -1);
        return (EXIT_FAILURE);
    }

    int rc = connect_with_timeout(sock, &peer);
    // int rc = connect(sock, (struct sockaddr *)&peer, sizeof(peer)); 
    if (rc != EXIT_SUCCESS) {
        perror("ошибка вызова connect with timeout");
        safe_leave(sock, NULL, -1);
        return (EXIT_FAILURE);        
    }
    printf("Connected to %d\n", port);
    rc = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));
    if (rc != 0){
        perror("Error in setsockopt in send request vote");
        safe_leave(sock, NULL, -1);
        return (EXIT_FAILURE);
    }
    rc = send_prep_message(sock, RequestVote, State, SERVER_ID);
    if (rc != EXIT_SUCCESS){
        perror("Error in prep message in request vote");
        safe_leave(sock, NULL, -1);
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
        safe_leave(sock, NULL, -1);
        return (EXIT_FAILURE);
    }

    rv_rpc_response_t response;
    memset(&response, 0, sizeof(response));
    rc = recv(sock, &response, sizeof(response), 0);
    if (rc <= 0) {
        perror("ошибка вызова recv");
        safe_leave(sock, NULL, -1);        
        return (EXIT_FAILURE);
    }
    printf("In request vote rpc recieved %d %d\n", response.term, response.vote_granted);
    if(!update_self_to_follower(response.term, -1)){
        votes += response.vote_granted;
    }

    safe_leave(sock, NULL, -1);
    timestamp();
    return (EXIT_SUCCESS);
}

void *handle_request_vote_rpc(int sock){
    printf("handle_request_vote_rpc\n");
    int rc;

    rv_rpc_message_t message;
    rc = recv(sock, &message, sizeof(message), 0);
    if (rc <= 0) {
        perror("Error in handle_request_vote_rpc recv");
        // memset(&message, 0, sizeof(message));
        return ((void *)EXIT_FAILURE);
    }

    rv_rpc_response_t response;
    response.term = current_term;
    response.vote_granted = 0;
    current_term = max(current_term, message.term);    

    if (message.term < current_term){
        //1. Reply false if term < currentTerm (§5.1)
    }
    else {
        // 2. If votedFor is null or candidateId, and candidate’s log is at
        // least as up-to-date as receiver’s log, grant vote (§5.2, §5.4)
        if ((!voted_for || voted_for == message.candidate_id || 
        voted_term != current_term) && 
        message.last_log_index >= last_log_index && 
        message.last_log_term >= last_log_term){
            response.vote_granted = 1;
            voted_for = message.candidate_id;
            voted_term = current_term;
            become_follower(-1, "handle_request_vote_rpc");
        }
    }
    
    printf("Sending data to Candidate with term %d, my term %d, vote_granted: %d\n",
     message.term, current_term, response.vote_granted);
    rc = send(sock, &response, sizeof(response), 0);
    if (rc <= 0){
        perror("Error in handle_request_vote_rpc send");
        return ((void *)EXIT_FAILURE);
    }
    timestamp();

    return ((void *) EXIT_SUCCESS);
}

void *handle_append_entries_rpc(int sock){
    printf("Handling append entries rpc\n");
    int rc;
    int keep_conn = 1;

    while (keep_conn){
        ae_rpc_message_t message;
        rc = recv(sock, &message, sizeof(message), 0);
        if (rc <= 0) {
            perror("Error in handle_append_entries_rpc recv");
            memset(&message, 0, sizeof(message));
            // safe_leave(sock, NULL);
            return ((void *)EXIT_FAILURE);
        }

        keep_conn = message.keep_connection;

        ae_rpc_response_t response = {
            .term = current_term,
            .success = 0,
        };

        int entry_index = message.entries.index;

        if (message.term < current_term){
            // Reply false if term < currentTerm
            printf("handle_append_entries_rpc: message.term %d < current_term %d\n", message.term, current_term);
            keep_conn = 0;
        }
        else if (log_arr.last <= message.prev_log_index || (log_arr.last > message.prev_log_index &&
                log_arr.entries[message.prev_log_index].term != message.prev_log_term)){
            // 2. Reply false if log doesn’t contain an entry at prevLogIndex
            // whose term matches prevLogTerm (§5.3)
        }
        else {
            // 3. If an existing entry conflicts with a new one (same index
            // but different terms), delete the existing entry and all that
            // follow it (§5.3)

            if (entry_index != -1) { // lets differentiate heartbeats by this
                // this is real entry 
                while (entry_index >= log_arr.size){
                    increase_log_size(&log_arr);
                }
                if (log_arr.last > entry_index && 
                !eq_entries(log_arr.entries[entry_index], message.entries)){
                    printf("MEMSETTING ENTRIES\n");
                    memset(&(log_arr.entries[entry_index]), 0,
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
            else{
                //this is hearbeat
                printf("==========\n");
                printf("Heartbeat\n");
                printf("==========\n");
                // if it was successful  heartbeat, we should stop receiving messages
                keep_conn = 0;
            }

            response.success = 1;

            // 5. If leaderCommit > commitIndex, set commitIndex =
            // min(leaderCommit, index of last new entry)
            if (message.leader_commit > commit_index){
                commit_index = min(message.leader_commit, last_log_index);
            }
            become_follower(message.leader_id, "handle_append_entries_rpc");
        }

        printf("Handling in append_entries_rpc is done, return\n");
        timestamp();
        rc = send(sock, &response, sizeof(response), 0);
        if (rc <= 0){
            perror("Error in handle_append_entries_rpc send");
            // safe_leave(sock, NULL);
            return ((void *)EXIT_FAILURE);
        }
        printf("Keep connection: %d\n", keep_conn);
    }

    return ((void *) EXIT_SUCCESS);
}

void *handle_client(int sock){

    // MOCK. Not tested

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

void *handle_rpc(int fd) {
    prepare_message_t message;
    int rc;
    // get prepare message
    rc = recv(fd, &message, sizeof(message), 0);
    if (rc <= 0) {
        printf("Failed\n");
        return ((void *)EXIT_FAILURE);
    }

    update_self_to_follower(message.term, -1);

    prepare_message_response_t response = {
        .ready = 1,
        .term = current_term,
    };

    if (message.term < current_term){
        response.ready = 0;
        rc = send(fd, &response, sizeof(response), 0);
        if (rc <= 0){
            perror("Ошибка вызова send в handle rpc (prep mess)");
            return ((void *)EXIT_FAILURE);
        }
        return ((void *) EXIT_SUCCESS);
    }

    rc = send(fd, &response, sizeof(response), 0);
    if (rc <= 0){
        perror("Ошибка вызова send в handle rpc (prep mess)");
        return ((void *)EXIT_FAILURE);
    }

    printf("Received prep message from %d, whom %d, type %d, term %d\n",
     message.from, message.whom, message.type, message.term);
    // Differentiate between clients and leader/other servers
    switch (message.whom){
        case Client:;
            handle_client(fd);
            break;
        case Follower:;  // Follower can only redirect clients (?)
            printf("Got message from follower!!!\n");  // Follower shouldn't initiate any rpc
            // this might happen due to race condidtion:
            // e.g. Candidate starts try_get_rpc, 
            // get message with higher term -> set himself to follower
            // after that start sending request_vote_rpc.
            response.ready = 0;
            send(fd, &response, sizeof(response), 0);
            break;
        case Candidate:;  // Candidate can only send RequestVote (?)
            handle_request_vote_rpc(fd);
            break;
        case Leader:;
            handle_append_entries_rpc(fd);
            break;
        default:;
            perror("Unknown peer");
    }
    return ((void *)EXIT_SUCCESS);
}

void *send_append_entries_rpc(void *arg){
    mutex_id_t *_mutex_id = (mutex_id_t *)arg;
    int follower_id = _mutex_id->id;
    pthread_mutex_t *mutex = _mutex_id->mutex;
    pthread_mutex_unlock(_mutex_id->mtx);

    printf("Send append entries to %d\n", follower_id);
    int port = KNOWN_PORTS[follower_id];

    struct sockaddr_in peer;
    peer.sin_family = AF_INET;
    peer.sin_port = htons(port);
    peer.sin_addr.s_addr = inet_addr(IP_ADDR);

    int sock;
    int rc;
    int hearbeat = 0;
        
    int i;
    // send as much entries as possible, but atleast 1 for heartbeat
    printf("last log index %d, next index %d\n", last_log_index, next_index[follower_id]);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("ошибка вызова socket");
        return ((void *)EXIT_FAILURE);
    }

    // setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));
    rc = connect(sock, (struct sockaddr *)&peer, sizeof(peer)); 
    if (rc != 0) {
        perror("ошибка вызова connect");
        safe_leave(sock, NULL, -1);
        return ((void *)EXIT_FAILURE);
    }

    pthread_mutex_lock(mutex);
    // send notificiation message about incoming append entries:
    rc = send_prep_message(sock, AppendEntries, State, SERVER_ID);
    if (rc != (EXIT_SUCCESS)){
        perror("Error in send prep message");
        safe_leave(sock, mutex, -1);
        return ((void *)EXIT_FAILURE);
    }

    // send atleast once for heartbeat
    keep_connection[follower_id] = 1;
    while(keep_connection[follower_id]){
        printf("New round of append entries to %d\n", follower_id);
        int prev_log_index = next_index[follower_id] - 1;

        // this might happen when all entries are sent, but we still want to heartbeat
        if (next_index[follower_id] >= log_arr.size){
            increase_log_size(&log_arr);
        }

        keep_connection[follower_id] &= State == Leader;
        ae_rpc_message_t message = {
            .term = current_term,
            .leader_id = SERVER_ID,
            .prev_log_index = prev_log_index,
            .prev_log_term = log_arr.entries[prev_log_index].term,
            .entries = log_arr.entries[next_index[follower_id]],
            .leader_commit = commit_index,
            .keep_connection = keep_connection[follower_id],     
        };

        if (next_index[follower_id] > last_log_index || (State != Leader)){  //heartbeat
            message.entries.index = -1;
            hearbeat = 1;
        }

        rc = send(sock, &message, sizeof(message), 0);
        if (rc <= 0) {
            perror("ошибка вызова send");
            safe_leave(sock, mutex, -1);
            return ((void *)EXIT_FAILURE);
        }
        printf("Send success\n");

        ae_rpc_response_t response;
        rc = recv(sock, &response, sizeof(response), 0);
        if (rc <= 0) {
            perror("ошибка вызова recv");
            safe_leave(sock, mutex, -1);
            return ((void *)EXIT_FAILURE);
        }

        printf("Received in send ae success: %d  term: %d\n", response.success, response.term);
        if (!update_self_to_follower(response.term, -1)){
            if (response.success) {
                if (hearbeat){
                    // message is successful and it was a heartbeat, means
                    // that last log entries match, stop sending messages
                    // same should be verified on the other side
                    keep_connection[follower_id] = 0;
                    printf("Sent heartbeat \n");
                }
                else{
                    match_index[follower_id] = next_index[follower_id];
                    next_index[follower_id]++;
                }
            }
            else {
                next_index[follower_id] = max(next_index[follower_id] - 1, 1);
                keep_connection[follower_id] = 1;
            }
        }
        else {
            break;
        }
    }

    flsh();
    safe_leave(sock, mutex, -1);
    return ((void *)EXIT_SUCCESS);
}

void *send_append_entries_rpc_to_all(){
    int i;
    int rc;
    pthread_t threads[NUMBER_OF_PORTS];
    pthread_mutex_t mutexes[NUMBER_OF_PORTS];
    pthread_mutex_t mtx;
    pthread_mutex_init(&mtx, NULL);
    pthread_mutex_lock(&mtx);

    for (i = 0; i < NUMBER_OF_PORTS; i++){
        if (i == SERVER_ID){ continue; }
        mutex_id_t mutex_id = {
            .mutex = &mutexes[i],
            .mtx = &mtx,
            .id = i,
        };
        printf("Creating thread send_append_entries_rpc for server %d\n", i);
        rc = pthread_create(&threads[i], NULL, send_append_entries_rpc, &mutex_id);
        if (rc != 0){
            perror("Error in creating thread in send_append_entries_rpc_to_all");
        }
        pthread_mutex_lock(&mtx);
    }
    for (i = 0; i < NUMBER_OF_PORTS; i++){
        if (i == SERVER_ID){ continue; }
        pthread_join(threads[i], NULL);
    }
}

void *send_request_vote_rpc_to_all(){
    // TODO: Make it parallel
    int i;
    int rc;
    for (i = 0; i < NUMBER_OF_PORTS && State == Candidate; i++){
        if (i == SERVER_ID){ continue; }
        // retry
        // int j;
        // for (j = 0; j < 2; j++){
        rc = send_request_vote_rpc(i);
            // if (rc == EXIT_SUCCESS){
            //     break;
            // }
        // }
    }
}

typedef struct try_get_rpc_t {
    int sock;
    int result;
    int follower;
    int ind;
    config_t * config;
    pthread_mutex_t mutex;
    pthread_mutex_t vote_mutex;
} try_get_rpc_t;

void *try_get_rpc(void *arg){
    try_get_rpc_t *_params = arg;
    try_get_rpc_t params = *_params;
    pthread_mutex_t *vote_mutex = &_params->vote_mutex;
    int sock = params.sock;

    // wait for incoming messages
    fd_set sockets;
    FD_ZERO(&sockets);  //clear set
    FD_SET(sock, &sockets); //add sock to set

    // using pselect instead of select because select may decrease the timeout
    int sel = pselect(sock + 1, &sockets, NULL, NULL, &select_timeout, NULL); 
    printf("Select %d\n", sel);
    if (sel < 0){
        perror("Ошибка select");
        return ((void *)EXIT_FAILURE);
    }

    if (State == Follower && sel == 0){
        initiate_election();
        return ((void *)EXIT_SUCCESS);
    }

    if (FD_ISSET(sock, &sockets)){ 
        // struct sockaddr_in client_name;
        // socklen_t client_name_len;
        // int fd = accept(sock, &client_name, &client_name_len);
        int rc;
        printf("Locking vote mutex in handle_rpc\n");
        pthread_mutex_lock(vote_mutex);

        int fd = accept(sock, NULL, NULL);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &recv_timeout, sizeof(recv_timeout));
        printf("accepted, got called in try_get_rpc\n");

        if (fd <= 0){
            perror("Error in accept");
            // return ((void *)EXIT_FAILURE);
        }
        else{
            handle_rpc(fd);
        }
        // In case we are not receiving heartbeats from leader,
        // we still want to initiate election 
        // (this case is possible, if we get the client message)

        // TODO: probably, some validation should be added to deal with
        // running election, when we've been set to follower
        if (State == Follower && time_spent_time(last_heartbeat_time) > ELECTION_TIMEOUT){
            printf("Initiate election because of timeout\n");
            initiate_election();
        }
        printf("Unlocking vote mutex in handle_rpc\n");
        safe_leave(fd, vote_mutex, -1);
    }

    return ((void *)EXIT_SUCCESS);
}


void *wrap_try_get_rpc(void *arg){
    try_get_rpc_t *_params = arg;
    try_get_rpc_t params = *_params;
    printf("wrap try get rpc -------------------v %d\n", params.ind);
    pthread_mutex_t *mutex = &_params->mutex;
    int rc;
    printf("Locking mutex in wrap_try_get_rpc\n");
    pthread_mutex_lock(mutex);
    printf("Mutex is locked\n");

    struct sockaddr_in local;
    // tuning the server
    memset(&local, 0, sizeof(struct sockaddr_in));
    local.sin_family = AF_INET;
    local.sin_port = htons(KNOWN_PORTS[SERVER_ID]);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror ("ошибка вызова socket");
        safe_leave(sock, mutex, params.ind);
        return ((void *)EXIT_FAILURE);
    }

    // make socket reusable. After `close(sock)` there is a timeout (due to OS)
    // when address becomes available to bind again. Thus, make it reusable to
    // not get "Address already in use" error.
    // https://hea-www.harvard.edu/~fine/Tech/addrinuse.html
    int enable = 1;
    rc = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    if (rc < 0){
        perror("setsockopt(SO_REUSEADDR) failed");
        safe_leave(sock, mutex, params.ind);
        return ((void *)EXIT_FAILURE);
    }

    // make socket non blocking
    rc = fcntl(sock, F_SETFL, O_NONBLOCK);
    if (rc < 0){
        perror("Error in fcntl");
        safe_leave(sock, mutex, params.ind);
        return ((void *)EXIT_FAILURE);
    }
    rc = bind(sock, (struct sockaddr *)&local, sizeof(local));
    if (rc < 0) {
        perror("ошибка вызова bind in wrap!");
        safe_leave(sock, mutex, params.ind);
        return ((void *)EXIT_FAILURE);
    }

    // setting maximum number of connections
    rc = listen(sock, MAX_CLIENTS);
    if (rc) {
        perror("ошибка вызова listen");
        safe_leave(sock, mutex, params.ind);
        return ((void *)EXIT_FAILURE);
    }

    _params->sock = sock;
    try_get_rpc(_params);
    safe_leave(sock, mutex, params.ind);
    return ((void *)EXIT_SUCCESS);
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
            safe_leave(sock, NULL, -1);
            return ((void *)EXIT_FAILURE);
        }

        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));
        int rc = connect(sock, (struct sockaddr *)&peer, sizeof(peer)); 
        if (rc != 0) {
            perror("ошибка вызова connect");
            safe_leave(sock, NULL, -1);
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
            safe_leave(sock, NULL, -1);
            return ((void *)EXIT_FAILURE);
        }

        safe_leave(sock, NULL, -1);
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
    time(&last_heartbeat_time);
    try_get_rpc_t params;
    memset(&params, 0, sizeof(params));

    pthread_mutex_t client_mutex;
    pthread_mutex_init(&params.mutex, NULL);
    pthread_mutex_init(&params.vote_mutex, NULL);
    pthread_mutex_init(&client_mutex, NULL);

    int j = 0;
    time_t run_start = time(NULL);

    for (current_term;current_term < 15 && time_spent_time(run_start) < 120;) {
        printf("--------------------------------------------------\n");
        printf("Current term %d\n", current_term);
        time_t term_start;
        time(&term_start);

        while(time_spent_time(term_start) < TERM_TIMEOUT){
            j++;
            printf("------------------------------\n");
            printf("New round in term %d\n", current_term);
            printf("Commit index is: %d\n", commit_index);
            timestamp();
            while (commit_index > last_applied){
                last_applied++;
                apply(log_arr.entries[last_applied]);
            }

            int i = 0;
            int rc;
            pthread_t thread;
            params.ind = j;

            switch (State){
                case Follower:;
                    printf("I'm Follower\n");
                    timestamp();
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
                    mysleep(sleep_time);
                    timestamp();

                    if (State != Candidate){
                        break;
                    }

                    // election, trying to get votes.
                    // in case we got rpc to become follower
                    // in try_get_rpc thread stop requesting
                    send_request_vote_rpc_to_all();
                    
                    // first outcome in perfect situation 
                    // (no other server is claimed as leader)

                    printf("Locks in Candidate\n");
                    pthread_mutex_lock(&params.mutex);
                    pthread_mutex_lock(&params.vote_mutex);
                    printf("Canceling wrap try get rpc ----X\n");
                    rc = pthread_cancel(thread);
                    if (rc != 0){
                        perror("Error in pthread_cancel");
                    }
                    pthread_mutex_unlock(&params.mutex);
                    pthread_mutex_unlock(&params.vote_mutex);
                    printf("Unlocks in Candidate\n");

                    if (votes > NUMBER_OF_PORTS / 2 && State == Candidate){
                        become_leader();
                    }
                    break;
                case Leader:;

                    int new_ci;
                    for(new_ci = commit_index + 1; new_ci <= last_log_index; new_ci++){
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

                    printf("I'm Leader <<<<<<<<<<<<<<< LEADER\n");
                    // create thread to get possible append_entries rpc 
                    params.config = config;
                    params.follower = 0;
                    pthread_create(&thread, NULL, wrap_try_get_rpc, &params);

                    timestamp();
                    
                    if (State != Leader){ break; }
                    update_client(&client_mutex);
                    
                    // simulate incoming message from client (append entry by now)
                    if (State != Leader){ break; }
                    entry_t entry = make_entry(gen_command(), current_term, log_arr.last);
                    push_back(&log_arr, entry);
                    printf("===========\n");
                    printf("Generated entry\n");
                    print_entry(entry);
                    printf("===========\n");
                    
                    // leader actions
                    send_append_entries_rpc_to_all();
                    flsh();
                    
                    // used for testing
                                    
                    // If there exists an N such that N > commitIndex, a majority
                    // of matchIndex[i] ≥ N, and log[N].term == currentTerm:
                    // set commitIndex = N (§5.3, §5.4).

                    if (State == Leader){
                        printf("Leader sleeping\n");
                        mysleep(2000);
                    }
                    printf("Locking mutex in leader\n");
                    pthread_mutex_lock(&params.mutex);
                    printf("Locking vote mutex in leader\n");
                    pthread_mutex_lock(&params.vote_mutex);
                    printf("vote mutex in leader is locked\n");

                    rc = pthread_cancel(thread);
                    if (rc != 0){
                        perror("Error in pthread_cancel");
                    }
                    pthread_mutex_unlock(&params.mutex);
                    pthread_mutex_unlock(&params.vote_mutex);
                    printf("Unlocking mutexes in leader\n");
                    break;
                default:
                    printf("Unknown state %d", State);
                    break;
            }
        }
        flsh();
        printf("Time spent %f\n", time_spent_time(run_start));
        // current_term++;
    }

    print_log(log_arr);
    return (EXIT_SUCCESS);
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

void intHandler(int smt){
    print_log(log_arr);
    printf("Error code %d\n", smt);
    exit(1);
}

int main(int argc, char * argv[]){
    srand(time(NULL));

    signal(SIGINT, intHandler);

    parse_str(&SERVER_ID, argv[1]);
    config_t config = {
        .port = KNOWN_PORTS[SERVER_ID],
    };
    
    log_arr.size = DEFAULT_LOG_SIZE;
    log_arr.last = 1;
    log_arr.entries = (entry_t *)malloc(sizeof(entry_t) * DEFAULT_LOG_SIZE);

    int rc = run_server(&config);
    if (rc != (EXIT_SUCCESS)){
        printf("Fail?\n");
    }

    return EXIT_SUCCESS;
}
