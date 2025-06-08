#ifndef TYPES_H
#define TYPES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Constants
#define STARTER_HASH_SIZE 1048573
#define K_SMOOTHING_FACTOR 1
#define MAX_RULE_LEN 80

#define MAX_OPERATIONS 512 * 512
#define WriteBufferSize 10240000
#define UNIGRAM_HASH_SIZE 1048573

#define HASH_SIZE 65536
#define POOL_BLOCK_SIZE 65536

// Core data structures
typedef struct {
    char full_op[5]; // Longest rule is 4chars
    int length;
    char base_op;
} CompleteOperation;

typedef struct {
    CompleteOperation ops[3]; // Max is trigrams
    int op_count;
    long frequency;
    double probability;
} OperationNGram;

typedef struct {
    CompleteOperation op;
    long starter_frequency;
    long total_frequency;
    double smoothed_probability;
} StarterOperationWithSmoothing;

typedef struct {
    CompleteOperation from_op;
    CompleteOperation to_op;
    int count;
    double probability;
} OperationTransition;

typedef struct writeBuffer {
    size_t bufferSize;
    size_t bufferUsed;
    char *buffer;
    size_t writeCount;
} WBuffer;

// Rule parsing structure
typedef struct {
    CompleteOperation operations[MAX_RULE_LEN];
    int op_count;
    char original_rule[MAX_RULE_LEN];
} ParsedRule;

// Generated rule structure
typedef struct {
    CompleteOperation ops[MAX_RULE_LEN];
    int length;
    char rule_string[MAX_RULE_LEN];
} GeneratedRule;



typedef struct NGramHashNode {
    OperationNGram ngram;
    struct NGramHashNode *next;
} NGramHashNode;

typedef struct HashNode {
    char rule_string[MAX_RULE_LEN];
    struct HashNode *next;
} HashNode;


typedef struct NodePool {
    NGramHashNode *nodes;
    long used_in_block;
    long block_size;
    struct NodePool *next_block;
} NodePool;

// Optimization structures
typedef struct {
    CompleteOperation *next_op;
    double probability;
    int frequency;
} SortedTransition;

typedef struct {
    CompleteOperation from_op;
    SortedTransition *sorted_transitions;
    int transition_count;
    double max_probability;
    double min_probability;
} FastTransitionLookup;

// Global hash table declarations

extern NGramHashNode **unigram_hash_table;
extern NGramHashNode **bigram_hash_table;
extern NGramHashNode **trigram_hash_table;
extern HashNode *hash_table[HASH_SIZE];
extern NGramHashNode **starter_hash_table;
extern long starter_count;

#endif
