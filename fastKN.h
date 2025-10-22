
#ifndef FASTKC_H
#define FASTKC_H
#include "priority_queue.h"

#include "types.h"


NGramHashNode** getHashTableForOrder(int order);


typedef struct TransitionLookup {
    CompleteOperation context[MAX_NGRAM_ORDER];
    int context_len;
    CompleteOperation *next_ops;
    double *next_probs;
    int transition_count;
} TransitionLookup;


int fast_get_next_kn_sparse(CompleteOperation *context, int context_len,
                                   CompleteOperation **next_ops, double *next_probs,
                                   int *count, double min_prob, double curr_prob);

void freeKNTransitionLookups();
void buildKNFrequencyTablesFast(KNModel *model, int verbose);
void buildSparseLookupFromFrequencies(int verbose);

void generateRulesKNSparsePQ(int max_length, int min_length, double min_probability,
                                        int verbose, WBuffer *output_buffer, long limit_unigrams,
                                        long num_rules);


void pushKNContinuationsPQ(RuleCandidate *candidate, PriorityQueue *pq,
                          double min_probability, int verbose);

void freeKNModelSafe(KNModel *model);
void buildKNFrequencyTablesOptimized(int verbose);
double fast_kn_prob_lookup_skipgram(CompleteOperation *context, int context_len,
                                   CompleteOperation *next_op);



#endif
