#include "protocol.h"

char *OPERATIONS[] = {"Erase", "Set", "Get"};

// generating a query to a server for an operation
int query(int *s, int *rc, command_t *command, response_t *response) {
    if ((*rc = send(*s, command, sizeof(*command), 0)) <= 0) {
        perror("ошибка вызова send");
        return (EXIT_FAILURE);
    }

    if ((*rc = recv(*s, response, sizeof(*response), 0)) <= 0) {
        perror("ошибка вызова recv");
        return (EXIT_FAILURE);
    }

    return (EXIT_SUCCESS);
}

// delete a pair from a structure
int op_erase(int *sock, int *rc, char *key, response_t *response) {
    command_t command = {OP_ERASE, "", ""};
    strcpy(command.key, key);
    return query(sock, rc, &command, response);
}

// add a pair or change it's value in a structure
int op_set(int *sock, int *rc, char *key, char *value, response_t *response) {
    command_t command = {OP_SET, "", ""};
    strcpy(command.key, key);
    strcpy(command.value, value);
    return query(sock, rc, &command, response);
}

// try to find a pair in a structure
int op_get(int *sock, int *rc, char *key, response_t *response) {
    command_t command = {OP_GET, "", ""};
    strcpy(command.key, key);
    return query(sock, rc, &command, response);
}

void print_command(command_t command){
    printf("Operation: %s, key: %s, value: %s\n",
     OPERATIONS[command.operation], command.key, command.value);
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
    // printf("Commitindex: %d\n\n", commit_index);
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

char *gen_str(int size){
    char *res = malloc(sizeof(char) * size + 1);
    for(int i = 0; i < size; i++){
        res[i] = (char)(rand() % 26 + 'a');
    }
    res[size] = '\0';
    return res;
}

command_t gen_command(){
    // generate random key-value pair
    char *key = gen_str(STR_SIZE);
    char *value = gen_str(STR_SIZE);
    // generate a random operation
    int operation = rand() % 3;

    command_t command;
    command.operation = operation;
    strcpy(command.key, key);
    strcpy(command.value, value);
    free(key);
    free(value);
    return command;
}

float rand_in_range(float a, float b){
    return a + (float)(rand())/(float)(RAND_MAX) * (b - a);
}

void mysleep(int ms){
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

void flsh(){
    fflush(stdout);
    fflush(stderr);
}

void timestamp(){
    printf("Timestamp: %d\n",(int)time(NULL));
}

/* return time spent in seconds */
double time_spent_time(time_t start_time){  
    return time(NULL) - start_time;
}

int eq_command(const command_t a, const command_t b){
    return a.operation == b.operation &&
        strcmp(a.key, b.key) == 0 &&
        strcmp(a.value, b.value) == 0;
}

int eq_entries(const entry_t a, const entry_t b){
    return eq_command(a.command, b.command) && 
        a.index == b.index &&
        a.term == b.index;
}

void increase_log_size(log_arr_t *logg){
    logg->entries = 
        (entry_t *)realloc(logg->entries, 2 * logg->size * sizeof(entry_t));
    logg->size *= 2;
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
        printf("Mutex %p is unlocked in safe_leave\n", (void *)mutex);
        if (ind != -1){
            printf("Leaving wrap_try_get_rpc -------------------^ %d\n", ind);
        }
    }
    flsh();
    return (EXIT_SUCCESS);
}

entry_t make_entry(command_t command, int term, int index, int received_by){
    entry_t entry = {
        .term = term,
        .index = index,
        .command = command,
        .received_by = received_by,
    };
    return entry;
}