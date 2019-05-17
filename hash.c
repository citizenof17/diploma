#include "hash.h"

int64_t A = 1e4 + 7;
int64_t M = 1e9 + 7;

// just an easy hash
int64_t get_hash(char *str){
    int64_t hash = 0;

    int i = 0;
    while(str[i] != '\0'){
        hash = (hash * A + str[i]) % M;
        i++;
    }

    return hash;
}