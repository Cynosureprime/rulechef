#ifndef HASH_TABLES_H
#define HASH_TABLES_H

#include "types.h"

// Hash table management functions
void initHashTables();
void freeHashTables();

// Hash functions
unsigned int hashTransition(CompleteOperation *from_op, CompleteOperation *to_op);
unsigned int hashNGram(CompleteOperation *ops, long op_count, int hash_size);
unsigned int hash(char *str);

// Transition hash table functions
void addStarterOperationHashed(CompleteOperation *op);


// N-gram hash table functions
NGramHashNode* findNGram(CompleteOperation *ops, long op_count);
void addOperationNGramHashed(CompleteOperation *ops, long op_count, long *count, long max_count);
void addUnigramHashed(CompleteOperation *op);
void addBigramHashed(CompleteOperation *ops);
void addTrigramHashed(CompleteOperation *ops);

// Extraction functions
OperationNGram* extractNGramsFromHashTable(NGramHashNode **hash_table, int hash_size, int ngram_type, int *total_count);
OperationNGram* getSortedUnigramsFromHashTable(int *count, int limit_unigrams);



// Count functions
long getUnigramCountFromHashTable();
long getBigramCountFromHashTable();
long getTrigramCountFromHashTable();
long getTransitionCountFromHashTable();

// Statistics and debugging
void printTopNGramsFromHashTable();
void printAllNGramHashTableStats();
void calculateBigramProbabilities();
// Comparison functions
int compareNGramsByFrequency(const void *a, const void *b);

#endif
