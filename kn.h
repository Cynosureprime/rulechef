#ifndef KNESER_NEY_H
#define KNESER_NEY_H

#include "types.h"
#include "hashmap.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif


typedef struct {
    CompleteOperation op;
    long continuation_count;
    double kn_probability;
    double freq_probability;
} KNOperation;


typedef struct {
    int max_ctx;
    int max_len;

    KNOperation *start_vecs;
    int start_count;

    CompleteOperation *vocab;
    int vocab_size;
    double *uni_probs;
    double global_d1[MAX_NGRAM_ORDER+1];
    double global_d2[MAX_NGRAM_ORDER+1];
    double global_d3[MAX_NGRAM_ORDER+1];

} KNModel;

extern KNModel *g_kn_model;


void calc_continuation_based_starters3_parallel(KNModel *model, int verbose);


void calc_global_discountsCG(KNModel *model, int verbose);
void buildTransitionKey(CompleteOperation *context, int context_len,
                       CompleteOperation *next_op, char *key_buffer);



#endif
