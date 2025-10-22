
#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "types.h"

#define HASHMAP_INITIAL_SIZE 1024
#define HASHMAP_LOAD_FACTOR 0.75
// Temporary transition list for building lookups
typedef struct TempTransitionList {
    CompleteOperation *ops;
    double *probs;
    int count;
    int capacity;
} TempTransitionList;

// Specialized functions for context operations
typedef struct ContextData {
    CompleteOperation context[MAX_NGRAM_ORDER];
    int context_len;
} ContextData;

typedef struct HashMapEntry {
    char *key;
    void *value;
    size_t value_size;
    struct HashMapEntry *next;
} HashMapEntry;

typedef struct HashMap {
    HashMapEntry **buckets;
    size_t bucket_count;
    size_t size;
    size_t threshold;
} HashMap;

typedef struct HashMapIterator {
    HashMap *map;
    size_t bucket_index;
    HashMapEntry *current_entry;
} HashMapIterator;
void initTempList(TempTransitionList *list) ;
void addToTempList(TempTransitionList *list, CompleteOperation op, double prob);
void freeTempList(TempTransitionList *list);
void qsortTransitions(CompleteOperation *ops, double *probs, int count);
void buildContextKey(CompleteOperation *context, int context_len, char *key_buffer);



// Core HashMap functions
HashMap* hashmap_create(void);
HashMap* hashmap_create_with_size(size_t initial_size);
void hashmap_free(HashMap *map);
int hashmap_put(HashMap *map, const char *key, const void *value, size_t value_size);
void* hashmap_get(HashMap *map, const char *key);
int hashmap_remove(HashMap *map, const char *key);
int hashmap_contains(HashMap *map, const char *key);
size_t hashmap_size(HashMap *map);
void hashmap_clear(HashMap *map);

// Iterator functions
HashMapIterator* hashmap_iterator(HashMap *map);
int hashmap_iter_has_next(HashMapIterator *iter);
HashMapEntry* hashmap_iter_next(HashMapIterator *iter);
void hashmap_iterator_free(HashMapIterator *iter);
int hashmap_put_context(HashMap *map, CompleteOperation *context, int context_len);
// Utility functions
void hashmap_print_stats(HashMap *map);

#endif



