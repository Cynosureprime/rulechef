#include "kn.h"
#include "hash_tables.h"
#include "processor.h"
#include "buffer.h"
#include <math.h>

#include "hashmap.h"
#include <omp.h>

extern int max_operation_count[MAX_NGRAM_ORDER +1];

static int compareStartersByContProb(const void *a, const void *b) {
    const KNOperation *ka = (const KNOperation*)a;
    const KNOperation *kb = (const KNOperation*)b;

    // Sort by continuation probability descending (higher probabilities first)
    if (kb->kn_probability > ka->kn_probability) return 1;
    if (kb->kn_probability < ka->kn_probability) return -1;
    return 0;
}


void calc_continuation_based_starters3_parallel(KNModel *model, int verbose) {

    int max_operations = 20000;  // Start larger to reduce resize contention
    int resize_count = 0;

    if (verbose) {
        fprintf(stderr, "  Starting with %d operation slots (will resize if needed)\n", max_operations);
    }

    typedef struct {
        char op_name[8];
        double continuation_score;
        long raw_frequency;
        int unique_contexts;
    } OperationContinuationData;

    // Allocate shared data structure (protected by locks)
    OperationContinuationData *op_data = calloc(max_operations, sizeof(OperationContinuationData));
    if (!op_data) {
        fprintf(stderr, "ERROR: Failed to allocate operation data array\n");
        return;
    }

    int op_count = 0;
    double total_continuation_mass = 0.0;
    long total_contexts_processed = 0;
    long contexts_per_order[MAX_NGRAM_ORDER + 1] = {0};

    // Mutex for protecting shared data structures
    omp_lock_t op_data_lock;
    omp_lock_t resize_lock;
    omp_init_lock(&op_data_lock);
    omp_init_lock(&resize_lock);

    // Process each n-gram order in parallel
    for (int order = 2; order <= MAX_NGRAM_ORDER; order++) {
        if (ngram_count[order] == 0) continue;

        NGramHashNode **hash_table = ngram_hash_table[order];
        int hash_size = max_operation_count[order];
        if (!hash_table) continue;

        if (verbose) {
            fprintf(stderr, "  Processing %d-grams\n", order);
        }

        long order_contexts = 0;
        double order_weight = 1.0 / order;

        // Parallel processing of hash buckets
        #pragma omp parallel reduction(+:order_contexts)
        {
            // Thread-local storage for batch processing
            typedef struct {
                char op_name[8];
                double contribution;
                int unique_contexts_delta;
                long raw_frequency_delta;
            } ThreadLocalUpdate;

            ThreadLocalUpdate *local_updates = calloc(1000, sizeof(ThreadLocalUpdate));
            int local_update_count = 0;
            const int MAX_LOCAL_UPDATES = 1000;

            #pragma omp for schedule(dynamic, 16)
            for (int i = 0; i < hash_size; i++) {
                NGramHashNode *node = hash_table[i];
                while (node != NULL) {
                    if (node->ngram.op_count == order) {
                        for (int pos = 1; pos < order; pos++) {
                            char *continuation_op = node->ngram.ops[pos].full_op;

                            double context_len = pos;
                            double position_weight = 1.0 + (context_len - 1) * 0.2;
                            double contribution = node->ngram.frequency * position_weight * order_weight;

                            // Store in thread-local buffer
                            int found_local = 0;
                            for (int j = 0; j < local_update_count; j++) {
                                if (strcmp(local_updates[j].op_name, continuation_op) == 0) {
                                    local_updates[j].contribution += contribution;
                                    local_updates[j].unique_contexts_delta++;
                                    local_updates[j].raw_frequency_delta += node->ngram.frequency;
                                    found_local = 1;
                                    break;
                                }
                            }

                            if (!found_local && local_update_count < MAX_LOCAL_UPDATES) {
                                strcpy(local_updates[local_update_count].op_name, continuation_op);
                                local_updates[local_update_count].contribution = contribution;
                                local_updates[local_update_count].unique_contexts_delta = 1;
                                local_updates[local_update_count].raw_frequency_delta = node->ngram.frequency;
                                local_update_count++;
                            }

                            order_contexts++;

                            // Flush local buffer when full
                            if (local_update_count >= MAX_LOCAL_UPDATES) {
                                omp_set_lock(&op_data_lock);
                                for (int k = 0; k < local_update_count; k++) {
                                    // Find or create operation in global data
                                    int op_idx = -1;
                                    for (int j = 0; j < op_count; j++) {
                                        if (strcmp(op_data[j].op_name, local_updates[k].op_name) == 0) {
                                            op_idx = j;
                                            break;
                                        }
                                    }

                                    if (op_idx == -1) {
                                        // Need to resize? (protected by resize_lock)
                                        if (op_count >= max_operations - 1) {
                                            omp_set_lock(&resize_lock);
                                            if (op_count >= max_operations - 1) { // Double-check
                                                int new_max = max_operations * 2;
                                                if (new_max <= 12000000) {
                                                    OperationContinuationData *new_data = realloc(op_data,
                                                        new_max * sizeof(OperationContinuationData));
                                                    if (new_data) {
                                                        memset(new_data + max_operations, 0,
                                                            max_operations * sizeof(OperationContinuationData));
                                                        op_data = new_data;
                                                        max_operations = new_max;
                                                        resize_count++;
                                                        if (verbose) {
                                                            fprintf(stderr, "  Resized operation array: %d (resize #%d)\n",
                                                                   max_operations, resize_count);
                                                        }
                                                    }
                                                }
                                            }
                                            omp_unset_lock(&resize_lock);
                                        }

                                        if (op_count < max_operations - 1) {
                                            op_idx = op_count++;
                                            strcpy(op_data[op_idx].op_name, local_updates[k].op_name);
                                            op_data[op_idx].continuation_score = 0.0;
                                            op_data[op_idx].unique_contexts = 0;
                                            op_data[op_idx].raw_frequency = 0;
                                        }
                                    }

                                    if (op_idx >= 0) {
                                        op_data[op_idx].continuation_score += local_updates[k].contribution;
                                        op_data[op_idx].unique_contexts += local_updates[k].unique_contexts_delta;
                                        op_data[op_idx].raw_frequency += local_updates[k].raw_frequency_delta;
                                    }
                                }
                                omp_unset_lock(&op_data_lock);

                                local_update_count = 0; // Reset local buffer
                            }
                        }
                    }
                    node = node->next;
                }
            }

            // Flush remaining local updates
            if (local_update_count > 0) {
                omp_set_lock(&op_data_lock);
                for (int k = 0; k < local_update_count; k++) {
                    int op_idx = -1;
                    for (int j = 0; j < op_count; j++) {
                        if (strcmp(op_data[j].op_name, local_updates[k].op_name) == 0) {
                            op_idx = j;
                            break;
                        }
                    }

                    if (op_idx == -1 && op_count < max_operations - 1) {
                        op_idx = op_count++;
                        strcpy(op_data[op_idx].op_name, local_updates[k].op_name);
                        op_data[op_idx].continuation_score = 0.0;
                        op_data[op_idx].unique_contexts = 0;
                        op_data[op_idx].raw_frequency = 0;
                    }

                    if (op_idx >= 0) {
                        op_data[op_idx].continuation_score += local_updates[k].contribution;
                        op_data[op_idx].unique_contexts += local_updates[k].unique_contexts_delta;
                        op_data[op_idx].raw_frequency += local_updates[k].raw_frequency_delta;
                    }
                }
                omp_unset_lock(&op_data_lock);
            }

            free(local_updates);
        } // End parallel section

        contexts_per_order[order] = order_contexts;
        total_contexts_processed += order_contexts;

        if (verbose) {
            fprintf(stderr, "Processed %ld context patterns from %d-grams\n", order_contexts, order);
        }
    }

    omp_destroy_lock(&op_data_lock);
    omp_destroy_lock(&resize_lock);

    for (int i = 0; i < op_count; i++) {
        double diversity_bonus = log(1.0 + op_data[i].unique_contexts);
        op_data[i].continuation_score *= diversity_bonus;
        total_continuation_mass += op_data[i].continuation_score;
    }

    if (total_continuation_mass <= 0.0) {
        fprintf(stderr, "ERROR: No continuation mass calculated\n");
        free(op_data);
        return;
    }

    int unigram_count_local = 0;
    OperationNGram *sorted_unigrams = getSortedUnigramsFromHashTable(&unigram_count_local, 0);

    model->start_count = unigram_count_local;
    model->start_vecs = malloc(model->start_count * sizeof(KNOperation));

    int found_continuations = 0;
    double min_prob = 1.0, max_prob = 0.0;

    for (int i = 0; i < unigram_count_local; i++) {
        CompleteOperation *op = &sorted_unigrams[i].ops[0];

        double continuation_score = 1.0;
        long continuation_contexts = 1;

        for (int j = 0; j < op_count; j++) {
            if (strcmp(op_data[j].op_name, op->full_op) == 0) {
                continuation_score = op_data[j].continuation_score;
                continuation_contexts = op_data[j].unique_contexts;
                found_continuations++;
                break;
            }
        }

        double continuation_prob = continuation_score / total_continuation_mass;

        if (continuation_prob < min_prob) min_prob = continuation_prob;
        if (continuation_prob > max_prob) max_prob = continuation_prob;

        model->start_vecs[i].op = *op;
        model->start_vecs[i].continuation_count = continuation_contexts;
        model->start_vecs[i].kn_probability = continuation_prob;

        if (verbose && i < 10) {
            fprintf(stderr, "[%d]: '%s' score=%.2f contexts=%ld prob=%.6f\n",
                    i, op->full_op, continuation_score, continuation_contexts, continuation_prob);
        }
    }

    qsort(model->start_vecs, model->start_count, sizeof(KNOperation), compareStartersByContProb);

    free(sorted_unigrams);
    free(op_data);

    if (verbose) {
        fprintf(stderr, "\n=== Continuation Calculation Complete ===\n");
        fprintf(stderr, "Built %d starters using %d unique operations \n",
                model->start_count, op_count);
        fprintf(stderr, "Array resized %d times during  execution\n", resize_count);
        fprintf(stderr, "Found continuation data for %d/%d operations (%.1f%%)\n",
                found_continuations, unigram_count_local,
                (double)found_continuations / unigram_count_local * 100.0);
        fprintf(stderr, "Total contexts processed: %ld\n", total_contexts_processed);
        fprintf(stderr, "Probability range: %.6f - %.6f\n", min_prob, max_prob);

        fprintf(stderr, "\nContext contribution by n-gram order:\n");
        for (int order = 2; order <= MAX_NGRAM_ORDER; order++) {
            if (contexts_per_order[order] > 0) {
                double percentage = (double)contexts_per_order[order] / total_contexts_processed * 100.0;
                fprintf(stderr, "  %d-grams: %ld (%.1f%%)\n", order, contexts_per_order[order], percentage);
            }
        }

        fprintf(stderr, "\nTop 10 continuation-based starters:\n");
        for (int i = 0; i < 10 && i < model->start_count; i++) {
            fprintf(stderr, "[%d]: '%s' contexts=%ld prob=%.6f\n",
                    i + 1, model->start_vecs[i].op.full_op,
                    model->start_vecs[i].continuation_count,
                    model->start_vecs[i].kn_probability);
        }
    }
}


//chen goodman discount method
void calc_global_discountsCG(KNModel *model, int verbose) {

    int fof_counts[MAX_NGRAM_ORDER+1][4] = {0};


    for (int j = 0; j<MAX_NGRAM_ORDER+1;j++)
    {
        for (int i = 0; i < max_operation_count[j]; i++) {
            NGramHashNode *node = ngram_hash_table[j][i];
            while (node != NULL) {
                if (node->ngram.op_count == j) {
                    int freq_idx = (node->ngram.frequency > 3) ? 3 : node->ngram.frequency - 1;
                    fof_counts[j][freq_idx]++;
                }
                node = node->next;
            }
        }
    }

    // Calculate global discounts for each order using Chen and Goodman method
    for (int order = 1; order <= MAX_NGRAM_ORDER; order++) {
        int n1 = fof_counts[order][0]; // frequency = 1
        int n2 = fof_counts[order][1]; // frequency = 2
        int n3 = fof_counts[order][2]; // frequency = 3
        int n4 = fof_counts[order][3]; // frequency = 4+

        // Calculate Y parameter (shared across all discounts)
        double Y = (n1 > 0) ? (double)n1 / (n1 + 2.0 * n2) : 0.0;

        if (n1 > 0) {
            model->global_d1[order] = 1.0 - 2.0 * Y * ((double)n2 / n1);
        } else {
            model->global_d1[order] = 0.0;
        }

        if (n2 > 0) {
            model->global_d2[order] = 2.0 - 3.0 * Y * ((double)n3 / n2);
        } else {
            model->global_d2[order] = model->global_d1[order];
        }


        if (n3 > 0) {
            model->global_d3[order] = 3.0 - 4.0 * Y * ((double)n4 / n3);
        } else {
            model->global_d3[order] = model->global_d2[order];
        }

        model->global_d1[order] = fmax(0.0, fmin(1.0, model->global_d1[order]));
        model->global_d2[order] = fmax(0.0, fmin(2.0, model->global_d2[order]));
        model->global_d3[order] = fmax(0.0, fmin(3.0, model->global_d3[order]));

        if (model->global_d2[order] < model->global_d1[order]) {
            model->global_d2[order] = model->global_d1[order];
        }
        if (model->global_d3[order] < model->global_d2[order]) {
            model->global_d3[order] = model->global_d2[order];
        }

        //Fallback values
        if (n1 == 0 && n2 == 0 && n3 == 0 && n4 == 0) {
            // No data - use conservative defaults
            model->global_d1[order] = 0.5;
            model->global_d2[order] = 1.0;
            model->global_d3[order] = 1.5;
        } else if (n1 > 0 && n2 == 0) {
            // Only singletons - use minimal discounting
            model->global_d1[order] = 0.1;
            model->global_d2[order] = 0.1;
            model->global_d3[order] = 0.1;
        }

        if (verbose) {

            fprintf(stderr, "Chen-Goodman %d discounts: D1=%.4f, D2=%.4f, D3=%.4f (Y=%.4f, n1=%d, n2=%d, n3=%d, n4=%d)\n",
                   order,
                   model->global_d1[order], model->global_d2[order], model->global_d3[order],
                   Y, n1, n2, n3, n4);

            // Additional diagnostic information
            if (Y == 0.0 && n1 == 0) {
                fprintf(stderr, "  Warning: No singletons found for %d - may indicate insufficient data\n",
                                               order);
            }
            if (model->global_d1[order] == model->global_d2[order] &&
                model->global_d2[order] == model->global_d3[order]) {
                fprintf(stderr, "  Note: All discounts equal for order %d (limited frequency variation)\n",
                       order);
            }
        }
    }
}

//good turing discount method
 void calc_global_discountsGT(KNModel *model, int verbose) {

    int fof_counts[MAX_NGRAM_ORDER+1][4] = {0};

    for (int i = 0; i < STARTER_HASH_SIZE; i++) {
        NGramHashNode *node = starter_hash_table[i];
        while (node != NULL) {
            if (node->ngram.op_count == 1) {
                int freq_idx = (node->ngram.frequency > 3) ? 3 : node->ngram.frequency - 1;
                fof_counts[1][freq_idx]++;
            }
            node = node->next;
        }
    }

    for (int j = 2; j< MAX_NGRAM_ORDER+1; j++)
    {
        for (int i = 0; i < max_operation_count[2]; i++) {
            NGramHashNode *node = ngram_hash_table[j][i];
            while (node != NULL) {
                if (node->ngram.op_count == j) {
                    int freq_idx = (node->ngram.frequency > 3) ? 3 : node->ngram.frequency - 1;
                    fof_counts[j][freq_idx]++;
                }
                node = node->next;
            }
        }
    }

    for (int order = 1; order <= MAX_NGRAM_ORDER; order++) {
        int n1 = fof_counts[order][0]; // frequency = 1
        int n2 = fof_counts[order][1]; // frequency = 2
        int n3 = fof_counts[order][2]; // frequency = 3
        int n4 = fof_counts[order][3]; // frequency = 4+

        double y = (n1 > 0) ? (double)n1 / (n1 + 2.0 * n2) : 0.0;

        model->global_d1[order] = (n1 > 0) ? fmax(0.0, 1.0 - 2.0 * y * ((double)n2 / n1)) : 0.0;
        model->global_d2[order] = (n2 > 0) ? fmax(0.0, 2.0 - 3.0 * y * ((double)n3 / n2)) : 0.0;
        model->global_d3[order] = (n3 > 0) ? fmax(0.0, 3.0 - 4.0 * y * ((double)n4 / n3)) : 0.0;

        if (verbose) {

            fprintf(stderr, "Global %d discounts (Good turing): D1=%.4f, D2=%.4f, D3=%.10f (n1=%d, n2=%d, n3=%d, n4=%d)\n",
                   order, model->global_d1[order], model->global_d2[order],
                   model->global_d3[order], n1, n2, n3, n4);
        }
    }
}




