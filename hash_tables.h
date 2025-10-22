#ifndef HASH_TABLES_H
#define HASH_TABLES_H

#include "types.h"
extern NGramHashNode **ngram_hash_table[MAX_NGRAM_ORDER + 1];
extern int hash_table_sizes[MAX_NGRAM_ORDER + 1];

// Shims for migrations to array structure
#define starter_hash_table    ngram_hash_table[0]
#define unigram_hash_table    ngram_hash_table[1]
#define bigram_hash_table     ngram_hash_table[2]
#define trigram_hash_table    ngram_hash_table[3]
#define fourgram_hash_table   ngram_hash_table[4]
#define fivegram_hash_table   ngram_hash_table[5]
#define sixgram_hash_table    ngram_hash_table[6]

// Hash table management functions
void initHashTables();
void initNodePool();
void freeHashTables();
void freeNGramHashTable(NGramHashNode **table, int size);
void freeNodePool();
NGramHashNode* allocateNGramNode() ;

// Hash functions
unsigned int hashTransition(CompleteOperation *from_op, CompleteOperation *to_op);
unsigned int hashNGram(CompleteOperation *ops, long op_count, int hash_size);
unsigned int hash(char *str);
void calculateNGramProbabilitiesParallel(int ngram_order);
void addStarterOperationHashed(CompleteOperation *op);
OperationNGram *getSortedStartersFromHashTable(int *count, int limit_unigrams);
NGramHashNode* findNGram(CompleteOperation *ops, long op_count);
void addOperationNGramHashed(CompleteOperation *ops, long op_count, long count);
OperationNGram* extractNGramsFromHashTable(NGramHashNode **hash_table, int hash_size, int ngram_type, int *total_count);
OperationNGram* getSortedUnigramsFromHashTable(int *count, int limit_unigrams);
int compareNGramsByFrequency(const void *a, const void *b);


#endif
