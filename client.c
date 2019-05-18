#include <unistd.h>
#include "protocol.h"

#define IP_ADDR ("127.0.0.1")
#define RECV_TIMEOUT (2)         // in seconds
#define CLIENT_SELECT_TIMEOUT (300)    // in seconds
int leader_id = 0;
int KNOWN_PORTS[NUMBER_OF_PORTS] = {7500, 7501, 7502};

whom_e State = Client;
log_arr_t log_sent;
log_arr_t log_received;

struct timespec select_timeout = {
    .tv_sec = CLIENT_SELECT_TIMEOUT,
    .tv_nsec = 0,
};

struct timeval recv_timeout = {
    .tv_sec = RECV_TIMEOUT,
    .tv_usec = 0,
};

void push_back(log_arr_t *logg, entry_t entry){
    if (logg->last == logg->size){
        increase_log_size(logg);
    }
    logg->entries[logg->last++] = entry;
}

void *print_response(response_t response){
    printf("value %s, answer %s\n", response.value, response.answer);
}

void *print_entry_committed(entry_committed_t entry){
    printf("----------------\n");
    printf("Entry committed\n");
    printf("Term %d, index %d, committed %d\n", entry.term, entry.index, entry.committed);
    print_response(entry.response);
    printf("----------------\n");
}


int send_prep_message(int sock, type_e type, whom_e whom, int from){
    prepare_message_t prep_message = { 
        .type = type,
        .whom = whom,
        .from = from,
        .term = -1,
    };
    int rc;
    //MSG_NOSIGNAL to ifnore SIGPIPE - 141 - writing to a closed socket
    rc = send(sock, &prep_message, sizeof(prep_message), MSG_NOSIGNAL);
    if (rc <= 0){
        perror("Error in send in send prep message");
        return (EXIT_FAILURE);
    }
    printf("Send success\n");
    prepare_message_client_t prep_response;
    memset(&prep_response, 0, sizeof(prep_response));
    rc = recv(sock, &prep_response, sizeof(prep_response), 0);
    if (rc <= 0){
        perror("Error in recv in send prep message");
        return (EXIT_FAILURE);
    }

    leader_id = prep_response.leader_id;
    if (!prep_response.ready){
        return (EXIT_FAILURE);
    }

    return (EXIT_SUCCESS);
}

void *wait_for_response(){
    struct sockaddr_in local;
    // tuning the server
    memset(&local, 0, sizeof(struct sockaddr_in));
    local.sin_family = AF_INET;
    local.sin_port = htons(CLIENT_PORT);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    
    int rc;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror ("ошибка вызова socket");
        safe_leave(sock, NULL, -1);
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
        safe_leave(sock, NULL, -1);
        return ((void *)EXIT_FAILURE);
    }

    // make socket non blocking to wait no more than %d seconds
    rc = fcntl(sock, F_SETFL, O_NONBLOCK);
    if (rc < 0){
        perror("Error in fcntl");
        safe_leave(sock, NULL, -1);
        return ((void *)EXIT_FAILURE);
    }
    rc = bind(sock, (struct sockaddr *)&local, sizeof(local));
    if (rc < 0) {
        perror("ошибка вызова bind in wrap!");
        safe_leave(sock, NULL, -1);
        return ((void *)EXIT_FAILURE);
    }

    // setting maximum number of connections
    rc = listen(sock, 1);
    if (rc) {
        perror("ошибка вызова listen");
        safe_leave(sock, NULL, -1);
        return ((void *)EXIT_FAILURE);
    }

    fd_set sockets;
    FD_ZERO(&sockets);  //clear set
    FD_SET(sock, &sockets); //add sock to set

    // using pselect instead of select because select may decrease the timeout
    int sel = pselect(sock + 1, &sockets, NULL, NULL, &select_timeout, NULL); 
    printf("Select %d\n", sel);
    if (sel <= 0){
        perror("Ошибка select");
        safe_leave(sock, NULL, -1);
        return ((void *)EXIT_FAILURE);
    }

    if (FD_ISSET(sock, &sockets)){ 
        int fd = accept(sock, NULL, NULL);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));
        printf("accepted\n");

        entry_committed_t committed;
        rc = recv(fd, &committed, sizeof(committed), 0);
        if (rc <= 0){
            perror("Error in recv");
            safe_leave(sock, NULL, -1);
            return ((void *)EXIT_FAILURE);
        }

        print_entry_committed(committed);
    }
    safe_leave(sock, NULL, -1);
    return ((void *)EXIT_SUCCESS);
}

int run_client(){
    while(1){
        //generate new command every %d seconds
        printf("Sleeping\n");
        mysleep(2000);
        // connecting to server
        struct sockaddr_in peer;
        peer.sin_family = AF_INET;
        peer.sin_port = htons(KNOWN_PORTS[leader_id]);
        peer.sin_addr.s_addr = inet_addr(IP_ADDR);

        int sock;
        int rc;
        char buf[1];
        
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("ошибка вызова socket");
            return (EXIT_FAILURE);
        }

        rc = connect(sock, (struct sockaddr *)&peer, sizeof(peer));
        if (rc > 0) {
            perror("ошибка вызова connect");
            return (EXIT_FAILURE);        
        }
        // if connection is successful print it
        printf("Connected\n");
        
        rc = send_prep_message(sock, Operation, State, CLIENT_PORT);
        if (rc != EXIT_SUCCESS || leader_id == -1){
            leader_id = rand() % 3;
            close(sock);
            continue;
        }

        command_t command = gen_command();
        rc = send(sock, &command, sizeof(command), 0);
        if (rc <= 0) {
            perror("ошибка вызова send");
            return (EXIT_FAILURE);
        }
        printf("Sent success\n");

        entry_received_t response;
        rc = recv(sock, &response, sizeof(response), 0);
        if (rc <= 0) {
            perror("ошибка вызова recv");
            return (EXIT_FAILURE);
        }
        
        push_back(&log_sent, make_entry(command, response.term, response.index, -1));
        printf("Recv success term: %d, index: %d\n", response.term, response.index);
        close(sock);

        wait_for_response();
    }
    print_log(log_sent);
    print_log(log_received);
    return (EXIT_SUCCESS);
}

void intHandler(int smt){
    print_log(log_sent);
    print_log(log_received);
    printf("Error code %d\n", smt);
    exit(1);
}



int main(int argc, char * argv[]) {
    srand(time(NULL));
    signal(SIGINT, intHandler);

    int rv = run_client();
    if (rv != 0){
        printf("Failed %d\n", rv);
        return (EXIT_FAILURE);
    }

    return (EXIT_SUCCESS);
}
