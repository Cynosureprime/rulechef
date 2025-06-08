#include "types.h"

#ifndef processor_H
#define processor_H

extern long curr_transition_size;

//void generateRulesFromHT(int max_length, int min_length,double min_probability, int verbose,WBuffer *output_buffer,double limit_unigrams);
void generateRulesFromHT(int max_length, int min_length, double min_probability, int verbose,
                        WBuffer *output_buffer, long limit_unigrams);
OperationNGram *getSortedStarterOperationsFromHT(int *count, double limit_unigrams);
#endif
