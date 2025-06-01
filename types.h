#ifndef TYPES_H
#define TYPES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Constants
#define MAX_RULE_LEN 80
#define MAX_TRANSITIONS 1048573
#define MAX_OPERATIONS 512 * 512
#define WriteBufferSize 10240000
#define TRANSITION_HASH_SIZE 1048573
#define UNIGRAM_HASH_SIZE 1048573
#define BIGRAM_HASH_SIZE 1048573
#define TRIGRAM_HASH_SIZE 1048573
#define HASH_SIZE 65536

// Core data structures
typedef struct {
    char full_op[5]; // Longest rule is 4chars
    int length;
    char base_op;
} CompleteOperation;

typedef struct {
    CompleteOperation ops[3]; // Max is trigrams
    int op_count;
    int frequency;
} OperationNGram;

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

// Hash table structures
typedef struct TransitionHashNode {
    OperationTransition transition;
    struct TransitionHashNode *next;
} TransitionHashNode;

typedef struct NGramHashNode {
    OperationNGram ngram;
    struct NGramHashNode *next;
} NGramHashNode;

typedef struct HashNode {
    char rule_string[MAX_RULE_LEN];
    struct HashNode *next;
} HashNode;

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
extern TransitionHashNode **transition_hash_table;
extern NGramHashNode **unigram_hash_table;
extern NGramHashNode **bigram_hash_table;
extern NGramHashNode **trigram_hash_table;
extern HashNode *hash_table[HASH_SIZE];

#endif
