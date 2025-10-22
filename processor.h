#include "types.h"

#ifndef processor_H
#define processor_H

extern long curr_transition_size;

void generateRulesFromHT(int max_length, int min_length, double min_probability, int verbose,
                        WBuffer *output_buffer, long limit_unigrams, long num_rules);
OperationNGram *getSortedStarterOperationsFromHT(int *count, double limit_unigrams);
void buildLookupTable(int verbose);
int getNextOperations(CompleteOperation *current_op, CompleteOperation **next_ops,
                     double *next_probs, int *count, double min_probability,
                     double current_rule_probability);
void generateRulesFromHTOriginal(int max_length, int min_length, double min_probability, int verbose,
WBuffer *output_buffer, long limit_unigrams, long num_rules);
void freeLookupTable(void);
int compareCompleteOps(const CompleteOperation *op1, const CompleteOperation *op2);

OperationNGram* getSortedTrigramsFromHashTable(int *count, int limit_trigrams, int verbose);
RuleCandidate* createCandidate(CompleteOperation *ops, int length, double probability);
int isRuleDuplicate(CompleteOperation *ops, int length);
void markRuleAsSeen(CompleteOperation *ops, int length);


#endif
