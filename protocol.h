#pragma once

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <ctype.h>

#define CLIENT_PORT (8000)
#define KEY_SIZE 20
#define VALUE_SIZE 20
#define ANSWER_SIZE 15
#define NUMBER_OF_PORTS (3)
#define STR_SIZE (10)
#define protocol_t command_t

typedef enum {
    OP_ERASE,
    OP_SET,
    OP_GET,
} operation_e;

typedef struct command_t {
    operation_e operation;
    char key[KEY_SIZE];
    char value[VALUE_SIZE];
} command_t;

typedef struct response_t {
    char answer[ANSWER_SIZE];
    char value[VALUE_SIZE];
} response_t;

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

typedef struct entry_received_t {
    int term;
    int index;
} entry_received_t;

typedef struct entry_committed_t {
    int term;
    int index;
    int committed;
    response_t response;
} entry_committed_t;

typedef struct entry_t {
    int term;
    int index;
    command_t command;
    response_t response;
} entry_t;

typedef struct log_arr_t {
    int size;
    int last; // position to write, inclusive, starting from 1
    entry_t *entries;
} log_arr_t;

log_arr_t log_arr;

int send_prep_message(int sock, type_e type, whom_e whom, int from);

int query(int *sock, int *rc, command_t *protocol, response_t *response);
int op_erase(int *sock, int *rc, char *key, response_t *response);
int op_set(int *sock, int *rc, char *key, char *value, response_t *response);
int op_get(int *sock, int *rc, char *key, response_t *response);

void intHandler(int smt);
void print_command(command_t command);
void print_entry(entry_t entry);
void print_log(log_arr_t logg);

char *gen_str(int size);
command_t gen_command();