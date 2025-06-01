#ifndef ANALYSIS_H
#define ANALYSIS_H

#include "types.h"

// Analysis functions
void analyseRuleStream(FILE *file, int verbose);

// Comparison functions
int compareOperationNGrams(const void *a, const void *b);
int compareOperationTransitions(const void *a, const void *b);


void printAllNGramHashTableStats();
#endif
