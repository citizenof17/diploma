
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "tree_hash_map.h"
#include "rb_tree.h"
#include "protocol.h"
#include "hash.h"

#define tree_t Tree
#define MIN_SIZE (5)

#define PREPARE_IMPL(map) \
    assert(map); assert(map->impl); \
    tree_hash_impl_t* impl = (tree_hash_impl_t *)map->impl;

typedef struct {
    int size;
    tree_t **trees;
    pthread_rwlock_t *rwlock;
} tree_hash_impl_t;

static response_t set(map_t * map, char * key, char * value){
    PREPARE_IMPL(map)

    int index = get_hash(key) % impl->size;
    
    pthread_rwlock_wrlock(&impl->rwlock[index]);
    insert(impl->trees[index], key, value);
    pthread_rwlock_unlock(&impl->rwlock[index]);
    
    response_t response = {"STORED", ""};
    return response;
}

// get an operation and head it to an appropriate tree
static response_t get(map_t * map, char * key){
    PREPARE_IMPL(map)

    response_t response;

    int index = get_hash(key) % impl->size;

    pthread_rwlock_rdlock(&impl->rwlock[index]);    
    getValue(impl->trees[index], key, response.value);
    pthread_rwlock_unlock(&impl->rwlock[index]);

    if (strcmp(response.value, "nil") == 0){  ///???? handle empty node
        strcpy(response.answer, "NOT_FOUND");
    }
    else{
        strcpy(response.answer, "FOUND");
    }
    return response;
}

static response_t rem(map_t * map, char * key){
    PREPARE_IMPL(map)
    int index = get_hash(key) % impl->size;
    pthread_rwlock_wrlock(&impl->rwlock[index]);    
    erase(impl->trees[index], key);
    pthread_rwlock_unlock(&impl->rwlock[index]);    
    
    response_t response = {"DELETED", ""};
    return response;
}

// creating a tree_hash_map
void tree_hash_map_init(map_t * map, int size){
    assert(map);

    map->impl = malloc(sizeof(tree_hash_impl_t));
    tree_hash_impl_t * impl = (tree_hash_impl_t *)map->impl;

    if (size < MIN_SIZE){ size = MIN_SIZE; }

    impl->size = size;
    impl->trees = (tree_t **)malloc(size * sizeof(tree_t *));
    impl->rwlock = (pthread_rwlock_t *)malloc(sizeof(pthread_rwlock_t) * size);

    int i;
    for(i = 0; i < size; i++){
        impl->trees[i] = createTree();
        pthread_rwlock_init(&impl->rwlock[i], NULL);
    }

    map->get = &get;
    map->set = &set;
    map->rem = &rem;
}

// freeing a tree_hash_map
void tree_hash_map_free(map_t *map){
    PREPARE_IMPL(map);
    
    int i;
    for(i = 0; i < impl->size; i++){
        deleteTree(impl->trees[i]);
        free(impl->trees[i]);
    }

    free(impl->trees);
    free(impl->rwlock);    
    free(map->impl);
}

// debugging printing
void tree_hash_map_print(map_t *map){
    PREPARE_IMPL(map);

    int i;
    for(i = 0; i < impl->size; i++){
        printTree(impl->trees[i]->root, 3);
        puts("\n\n");
    }
}