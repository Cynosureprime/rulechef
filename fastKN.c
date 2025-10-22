#include "hashmap.h"
#include "kn.h"
#include "hash_tables.h"
#include "processor.h"
#include "buffer.h"
#include <math.h>
#include "fastKN.h"
#include "priority_queue.h"
#include <string.h>
#include <limits.h>


// Soft expansion cap to prevent explosive growth, not overall hard cap
#define MAX_TRANSITIONS 50000
extern int max_operation_count[MAX_NGRAM_ORDER+1];


// Global variables
static HashMap *transition_freq_map = NULL;
static HashMap *context_freq_map = NULL;
KNModel *g_kn_model = NULL;
static HashMap *context_lookup_map = NULL;
static int lookup_count = 0;
static TransitionLookup *transition_lookups = NULL;
static long skip_gram = 0;
static long backoff =0;

void pushKNContinuationsPQ(RuleCandidate *candidate, PriorityQueue *pq,
                          double min_probability, int verbose) {
    if (!g_kn_model || !transition_lookups) return;

    // Build context from candidate rule
    int context_len = MIN(candidate->length, MAX_NGRAM_ORDER);
    CompleteOperation context[MAX_NGRAM_ORDER];

    // Use the last context_len operations as context
    int start_idx = candidate->length - context_len;
    if (start_idx < 0) start_idx = 0;
    int actual_context_len = candidate->length - start_idx;

    for (int i = 0; i < actual_context_len; i++) {
        context[i] = candidate->ops[start_idx + i];
    }

    CompleteOperation **next_ops = malloc(MAX_TRANSITIONS * sizeof(CompleteOperation *));
    double *next_probs = malloc(MAX_TRANSITIONS * sizeof(double));
    int next_count = 0;

    // Try different context lengths (longest first)
    for (int try_len = MIN(actual_context_len, MAX_NGRAM_ORDER); try_len >= 1; try_len--) {
        int ctx_start = actual_context_len - try_len;
        next_count = 0;

        fast_get_next_kn_sparse(&context[ctx_start], try_len,
                               next_ops, next_probs, &next_count,
                               min_probability, candidate->probability);

        if (next_count > 0) {
            // Found transitions at this context length, use them
            break;
        }
    }

    if (verbose && next_count > 0) {
        static int debug_count = 0;
        if (debug_count++ < 10) {
            char debug_rule[MAX_RULE_LEN] = "";
            for (int i = 0; i < candidate->length; i++) {
                strcat(debug_rule, candidate->ops[i].full_op);
            }
            fprintf(stderr, "%s' -> %d transitions (best: %.6f)\n",
                   debug_rule, next_count, next_count > 0 ? next_probs[0] : 0.0);
        }
    }

    // Process results
    for (int i = 0; i < next_count; i++) {
        if (next_ops[i] == NULL) continue;

        double new_prob = candidate->probability * next_probs[i];

        if (new_prob >= min_probability * 0.1 || min_probability == 0.0) {
            CompleteOperation new_ops[MAX_RULE_LEN];
            for (int j = 0; j < candidate->length; j++) {
                new_ops[j] = candidate->ops[j];
            }
            new_ops[candidate->length] = *next_ops[i];

            RuleCandidate *extended = createCandidate(new_ops, candidate->length + 1, new_prob);
            if (extended) {
                if (!pushPriorityQueue(pq, extended)) {
                    free(extended);
                }
            }
        }
    }

    free(next_ops);
    free(next_probs);
}

// Main generation function using Priority Queue
void generateRulesKNSparsePQ(int max_length, int min_length, double min_probability,
                                        int verbose, WBuffer *output_buffer, long limit_unigrams,
                                        long num_rules) {

    fprintf(stderr, "Building model from analysed data...\n");

    KNModel *model = malloc(sizeof(KNModel));
    model->max_ctx = MAX_NGRAM_ORDER;
    model->max_len = max_length;
    g_kn_model = model;

    // Build KN model

    calc_continuation_based_starters3_parallel(model, verbose);
    calc_global_discountsCG(model, verbose);

    /*
    if (verbose) {
        fprintf(stderr, "Global discounts calculated:\n");
        for (int order = 2; order <= MAX_NGRAM_ORDER; order++) {
            fprintf(stderr, "  %d-gram: D1=%.6f, D2=%.6f, D3=%.6f\n",
                   order, model->global_d1[order], model->global_d2[order], model->global_d3[order]);
        }
    }
    */

    // Build frequency tables
    buildKNFrequencyTablesOptimized(verbose);
    buildSparseLookupFromFrequencies(verbose);

    int sc_count = model->start_count;
    if (limit_unigrams > 0 && limit_unigrams < sc_count) {
        sc_count = limit_unigrams;
    }

    if (verbose) {
        fprintf(stderr, "Using %d start vectors with Priority Queue\n", sc_count);
    }

    // Create Priority Queue with appropriate sizing
    int pq_limit = (num_rules > 0) ? num_rules * 15 : PQ_NO_LIMIT;
    PriorityQueue *pq = createPriorityQueue(sc_count * 10, pq_limit);
    if (!pq) {
        fprintf(stderr, "Error: Failed to create Priority Queue\n");
        return;
    }

    // Seed priority queue with continuation-based starters
    int seeds_added = 0;
    for (int i = 0; i < sc_count; i++) {
        double starter_prob = model->start_vecs[i].kn_probability;

        if (starter_prob >= min_probability || min_probability == 0.0) {
            RuleCandidate *candidate = createCandidate(&model->start_vecs[i].op, 1, starter_prob);
            if (candidate) {
                if (pushPriorityQueue(pq, candidate)) {
                    seeds_added++;
                } else {
                    free(candidate);
                    break;
                }
            }
        }
    }

    if (verbose) {
        fprintf(stderr, "Seeded Priority Queue with %d candidates\n", getPriorityQueueSize(pq));
        printPriorityQueueStats(pq);
    }

    long rules_generated = 0;
    long candidates_processed = 0;
    double last_prob = 1.0;

    fprintf(stderr, "Starting generator...\n");
    // Main generation loop with priority queue
    while (!isPriorityQueueEmpty(pq) && (num_rules == 0 || rules_generated < num_rules)) {
        RuleCandidate *current = popPriorityQueue(pq);
        if (!current) break;

        candidates_processed++;

        // Early termination based on probability
        if (min_probability > 0.0 && current->probability < min_probability) {
            free(current);
            continue;
        }

        // Skip duplicates
        if (isRuleDuplicate(current->ops, current->length)) {
            free(current);
            continue;
        }

        // Output valid rules
        if (current->length >= min_length && current->length <= max_length) {
            char rule_string[MAX_RULE_LEN] = "";
            int total_len = 0;

            for (int i = 0; i < current->length; i++) {
                int op_len = strlen(current->ops[i].full_op);
                if (total_len + op_len >= MAX_RULE_LEN - 1) break;
                strcat(rule_string, current->ops[i].full_op);
                total_len += op_len;
            }

            buffer_string2(output_buffer, rule_string, MAX_RULE_LEN);
            markRuleAsSeen(current->ops, current->length);
            rules_generated++;

            if (verbose && rules_generated % 10000 == 0) {
                fprintf(stderr, "Generated %ld rules (prob: %.6f, queue: %d)\n",
                       rules_generated, current->probability, getPriorityQueueSize(pq));
            }
        }

        // Expand with Priority Queue
        if (current->length < max_length) {
            pushKNContinuationsPQ(current, pq, min_probability,
                                 verbose && candidates_processed <= 20);
        }

        last_prob = current->probability;
        free(current);

        // Periodic buffer flush
        if (rules_generated % 1000 == 0) {
            flush_buffer(output_buffer);
        }
    }

    flush_buffer(output_buffer);

    fprintf(stderr, "Finished generating rules");

    if (verbose) {
        fprintf(stderr, "Rules generated: %ld\n", rules_generated);
        fprintf(stderr, "Candidates processed: %ld\n", candidates_processed);
        fprintf(stderr, "Final prob: %.6f\n", last_prob);
        fprintf(stderr, "Final queue size: %d\n", getPriorityQueueSize(pq));
        fprintf(stderr, "Priority Queue ensured best-first generation order\n");
    }

    // Cleanup
    freePriorityQueue(pq);
    freeKNModelSafe(model);
    g_kn_model = NULL;
}
float SwapAppendRatio = 1;

void buildTransitionKey(CompleteOperation *context, int context_len,
                       CompleteOperation *next_op, char *key_buffer) {
    key_buffer[0] = '\0';

    // Add context operations
    for (int i = 0; i < context_len; i++) {
        if (i > 0) strcat(key_buffer, "|");
        strcat(key_buffer, context[i].full_op);
    }

    // Add separator and next operation
    strcat(key_buffer, "->");
    strcat(key_buffer, next_op->full_op);
}


//Skip Gram

double get_discount_for_frequency(long freq, int context_order) {
    if (freq >= 3) {
        return g_kn_model->global_d3[context_order];
    } else if (freq == 2) {
        return g_kn_model->global_d2[context_order];
    } else if (freq == 1) {
        return g_kn_model->global_d1[context_order];
    }
    return 0.0;
}

double try_context_lookup(CompleteOperation *context, int context_len, CompleteOperation *next_op) {
    if (context_len == 0) return 0.0;

    // Build keys for lookup
    char transition_key[MAX_RULE_LEN * 2];
    char context_key[MAX_RULE_LEN];
    buildTransitionKey(context, context_len, next_op, transition_key);
    buildContextKey(context, context_len, context_key);

    // Get frequencies from precomputed maps
    long *transition_freq = (long*)hashmap_get(transition_freq_map, transition_key);
    long *context_total = (long*)hashmap_get(context_freq_map, context_key);

    long freq = transition_freq ? *transition_freq : 0;
    long total = context_total ? *context_total : 0;

    if (total == 0 || freq == 0) {
        return 0.0; // No data at this context level
    }

    // Calculate KN probability at this specific context level
    double discount = get_discount_for_frequency(freq, context_len);
    double base_prob = ((double)freq - discount) / (double)total;

    return fmax(0.0, base_prob); // Don't return negative probabilities
}

double calculate_position_weight(int skip_pos, int context_len) {

    if (context_len <= 1) return 1.0;

    double weight = 1.0 - 0.7 * ((double)skip_pos / (context_len - 1));

    return weight;
}

double skipgram_backoff(CompleteOperation *context, int context_len, CompleteOperation *next_op) {
    double best_prob = 0.0;
    double total_weight = 0.0;
    double weighted_prob = 0.0;
    int successful_lookups = 0;

    for (int skip_pos = 0; skip_pos < context_len; skip_pos++) {
        CompleteOperation reduced_context[MAX_NGRAM_ORDER];
        int reduced_len = 0;

        for (int i = 0; i < context_len; i++) {
            if (i != skip_pos) {
                reduced_context[reduced_len] = context[i];
                reduced_len++;
            }
        }

        double sub_prob = try_context_lookup(reduced_context, reduced_len, next_op);

        if (sub_prob > 0.0) {
            double position_weight = calculate_position_weight(skip_pos, context_len);

            weighted_prob += sub_prob * position_weight;
            total_weight += position_weight;
            successful_lookups++;

            if (sub_prob > best_prob) {
                best_prob = sub_prob;
            }
        }
    }

    if (successful_lookups > 0) {
        return weighted_prob / total_weight;
    }

    return fast_kn_prob_lookup_skipgram(context + 1, context_len - 1, next_op);
}

double fast_kn_prob_lookup_skipgram(CompleteOperation *context, int context_len,
                                   CompleteOperation *next_op) {
    if (context_len == 0) {
        for (int i = 0; i < g_kn_model->start_count; i++) {
            if (compareCompleteOps(&g_kn_model->start_vecs[i].op, next_op)) {
                return g_kn_model->start_vecs[i].kn_probability;
            }
        }
        return 0.001;
    }


    double prob = try_context_lookup(context, context_len, next_op);
    if (prob > 0.0) {
        return prob;
    }

    if (context_len > 1) {
            skip_gram++;
        return skipgram_backoff(context, context_len, next_op);
    }

    backoff++;
    return fast_kn_prob_lookup_skipgram(NULL, 0, next_op);
}


typedef struct {
    char transition_key[MAX_RULE_LEN * 2];
    char context_key[MAX_RULE_LEN];
    long frequency;
} BatchEntry;

// Static helper function for batch processing
static void flush_batch(BatchEntry *batch, int *batch_count, long *total_transitions_found,
                       HashMap *transition_freq_map, HashMap *context_freq_map) {
    for (int i = 0; i < *batch_count; i++) {
        BatchEntry *entry = &batch[i];

        // Update transition frequency
        long *trans_freq = (long*)hashmap_get(transition_freq_map, entry->transition_key);
        if (trans_freq) {
            *trans_freq += entry->frequency;
        } else {
            long new_freq = entry->frequency;
            hashmap_put(transition_freq_map, entry->transition_key, &new_freq, sizeof(long));
            (*total_transitions_found)++;
        }

        // Update context total frequency
        long *ctx_freq = (long*)hashmap_get(context_freq_map, entry->context_key);
        if (ctx_freq) {
            *ctx_freq += entry->frequency;
        } else {
            long new_freq = entry->frequency;
            hashmap_put(context_freq_map, entry->context_key, &new_freq, sizeof(long));
        }
    }
    *batch_count = 0;
}

// Optimized buildKNFrequencyTablesFast with batch processing and reduced allocations
void buildKNFrequencyTablesOptimized(int verbose) {
    if (verbose) {
        fprintf(stderr, "Building optimized frequency tables\n");
    }

    // Pre-allocate larger maps to reduce rehashing
    size_t estimated_transitions = 0;
    size_t estimated_contexts = 0;

    // Estimate sizes based on n-gram counts
    for (int order = 2; order <= MAX_NGRAM_ORDER; order++) {
        if (ngram_count[order] > 0) {
            estimated_transitions += ngram_count[order] * (order - 1);
            estimated_contexts += ngram_count[order] / 2; // Conservative estimate
        }
    }

    // Create frequency maps with better initial sizing
    transition_freq_map = hashmap_create_with_size(estimated_transitions * 2);
    context_freq_map = hashmap_create_with_size(estimated_contexts * 2);

    if (!transition_freq_map || !context_freq_map) {
        fprintf(stderr, "Failed to create frequency maps\n");
        return;
    }

    if (verbose) {
        fprintf(stderr, "  Estimated transitions: %zu, contexts: %zu\n",
               estimated_transitions, estimated_contexts);
    }

    long total_transitions_found = 0;

    // Batch processing setup
    const int BATCH_SIZE = 10000;
    BatchEntry *batch = malloc(BATCH_SIZE * sizeof(BatchEntry));
    if (!batch) {
        fprintf(stderr, "Failed to allocate batch buffer\n");
        return;
    }

    int batch_count = 0;

    // Process each n-gram order with optimized inner loops
    for (int order = 2; order <= MAX_NGRAM_ORDER; order++) {
        NGramHashNode **hash_table = ngram_hash_table[order];
        if (!hash_table || ngram_count[order] == 0) continue;

        int hash_size = max_operation_count[order];

        if (verbose) {
            fprintf(stderr, "  Processing %d-grams (%ld entries)...\n", order, ngram_count[order]);
        }

        long processed_ngrams = 0;

        // Optimized: Process all contexts for an n-gram at once
        for (int i = 0; i < hash_size; i++) {
            NGramHashNode *node = hash_table[i];
            while (node != NULL) {
                if (node->ngram.op_count == order) {
                    processed_ngrams++;

                    // Generate all context->next transitions for this n-gram in one pass
                    for (int ctx_len = 1; ctx_len < order; ctx_len++) {
                        // Optimized: Use sliding window approach
                        for (int start = 0; start <= order - ctx_len - 1; start++) {
                            if (batch_count >= BATCH_SIZE) {
                                flush_batch(batch, &batch_count, &total_transitions_found,
                                          transition_freq_map, context_freq_map);
                            }

                            BatchEntry *entry = &batch[batch_count];
                            entry->frequency = node->ngram.frequency;

                            // Build context key efficiently
                            entry->context_key[0] = '\0';
                            for (int j = 0; j < ctx_len; j++) {
                                if (j > 0) strcat(entry->context_key, "|");
                                strcat(entry->context_key, node->ngram.ops[start + j].full_op);
                            }

                            // Build transition key efficiently
                            strcpy(entry->transition_key, entry->context_key);
                            strcat(entry->transition_key, "->");
                            strcat(entry->transition_key, node->ngram.ops[start + ctx_len].full_op);

                            batch_count++;
                        }
                    }
                }
                node = node->next;
            }

            // Progress reporting with less frequency
            if (verbose && i % (hash_size / 20) == 0 && hash_size > 20) {
                double progress = (double)i / hash_size * 100.0;
                fprintf(stderr, "    Order %d: %.1f%% complete (%ld n-grams processed)\r",
                       order, progress, processed_ngrams);
                fflush(stderr);
            }
        }

        // Flush remaining batch entries
        if (batch_count > 0) {
            flush_batch(batch, &batch_count, &total_transitions_found,
                       transition_freq_map, context_freq_map);
        }

        if (verbose) {
            fprintf(stderr, "\n    Processed %ld %d-grams\n", processed_ngrams, order);
        }
    }

    free(batch);

    if (verbose) {
        fprintf(stderr, "Optimized frequency collection complete:\n");
        fprintf(stderr, "  Total unique transitions: %ld\n", total_transitions_found);
        fprintf(stderr, "  Total unique contexts: %zu\n", hashmap_size(context_freq_map));

        if (estimated_transitions > 0) {
            fprintf(stderr, "  Transition map load factor: %.2f\n",
                   (double)hashmap_size(transition_freq_map) / estimated_transitions);
        }
        if (estimated_contexts > 0) {
            fprintf(stderr, "  Context map load factor: %.2f\n",
                   (double)hashmap_size(context_freq_map) / estimated_contexts);
        }
    }
}


void buildSparseLookupFromFrequencies(int verbose) {
    if (verbose) {
        fprintf(stderr, "Building sparse lookups from frequency tables...\n");
    }

    if (!transition_freq_map || !context_freq_map) {
        fprintf(stderr, "ERROR: Frequency maps not initialized!\n");
        return;
    }

    size_t total_transitions = hashmap_size(transition_freq_map);
    if (total_transitions == 0) {
        fprintf(stderr, "ERROR: No transitions found in frequency map!\n");
        return;
    }

    if (verbose) {
        fprintf(stderr, "Processing %zu transitions from frequency map...\n", total_transitions);
    }

    // Group transitions by context
    HashMap *context_groups = hashmap_create_with_size(hashmap_size(context_freq_map));
    if (!context_groups) {
        fprintf(stderr, "ERROR: Failed to create context groups map!\n");
        return;
    }

    HashMapIterator *iter = hashmap_iterator(transition_freq_map);
    if (!iter) {
        fprintf(stderr, "ERROR: Failed to create iterator!\n");
        hashmap_free(context_groups);
        return;
    }

    int transitions_processed = 0;
    int parse_errors = 0;
    int prob_calc_errors = 0;

    while (hashmap_iter_has_next(iter)) {
        HashMapEntry *entry = hashmap_iter_next(iter);
        if (!entry || !entry->key || !entry->value) {
            parse_errors++;
            continue;
        }

        char *transition_key = entry->key;
        long *frequency = (long*)entry->value;

        // DEBUG: Print first few keys to see format
        if (verbose && transitions_processed < 5) {
            fprintf(stderr, "  Processing transition: '%s' (freq=%ld)\n", transition_key, *frequency);
        }

        // Find the arrow separator
        char *arrow_pos = strstr(transition_key, "->");
        if (!arrow_pos) {
            if (verbose && parse_errors < 10) {
                fprintf(stderr, "  ERROR: No '->' found in key: '%s'\n", transition_key);
            }
            parse_errors++;
            continue;
        }

        // Extract context part (before "->")
        int context_key_len = arrow_pos - transition_key;
        if (context_key_len <= 0 || context_key_len >= MAX_RULE_LEN) {
            if (verbose && parse_errors < 10) {
                fprintf(stderr, "  ERROR: Invalid context length %d in key: '%s'\n", context_key_len, transition_key);
            }
            parse_errors++;
            continue;
        }

        char context_key[MAX_RULE_LEN];
        strncpy(context_key, transition_key, context_key_len);
        context_key[context_key_len] = '\0';

        // Extract next_op part (after "->")
        char *next_op_str = arrow_pos + 2;
        if (!next_op_str || strlen(next_op_str) == 0) {
            if (verbose && parse_errors < 10) {
                fprintf(stderr, "  ERROR: Empty next_op in key: '%s'\n", transition_key);
            }
            parse_errors++;
            continue;
        }

        // Parse context from context_key
        CompleteOperation context[MAX_NGRAM_ORDER];
        int context_len = 0;

        if (strlen(context_key) > 0) {
            char *context_copy = strdup(context_key);
            if (!context_copy) {
                fprintf(stderr, "ERROR: Failed to allocate memory for context_copy\n");
                parse_errors++;
                continue;
            }

            char *token = strtok(context_copy, "|");
            while (token && context_len < MAX_NGRAM_ORDER) {
                if (strlen(token) >= sizeof(context[context_len].full_op)) {
                    if (verbose && parse_errors < 10) {
                        fprintf(stderr, "  ERROR: Token too long: '%s'\n", token);
                    }
                    break;
                }
                strcpy(context[context_len].full_op, token);
                context[context_len].length = strlen(token);
                context_len++;
                token = strtok(NULL, "|");
            }
            free(context_copy);
        }

        if (context_len == 0) {
            if (verbose && parse_errors < 10) {
                fprintf(stderr, "  ERROR: Failed to parse context from: '%s'\n", context_key);
            }
            parse_errors++;
            continue;
        }

        // Create next_op from string
        CompleteOperation next_op;
        if (strlen(next_op_str) >= sizeof(next_op.full_op)) {
            if (verbose && parse_errors < 10) {
                fprintf(stderr, "  ERROR: next_op too long: '%s'\n", next_op_str);
            }
            parse_errors++;
            continue;
        }
        strcpy(next_op.full_op, next_op_str);
        next_op.length = strlen(next_op_str);

        // Calculate probability with error checking
        double prob = 0.0;
        if (g_kn_model) {
            prob = fast_kn_prob_lookup_skipgram(context, context_len, &next_op);
            if (prob < 0.0 || prob > 1.0) {
                if (verbose && prob_calc_errors < 10) {
                    fprintf(stderr, "  WARNING: Invalid probability %.6f for transition: '%s'\n", prob, transition_key);
                }
                prob = fmax(0.0, fmin(1.0, prob)); // Clamp to valid range
                prob_calc_errors++;
            }
        } else {
            fprintf(stderr, "ERROR: g_kn_model is NULL!\n");
            prob_calc_errors++;
            prob = 0.001; // Default small probability
        }

        // Get or create transition list for this context
        TempTransitionList **trans_list_ptr = (TempTransitionList**)hashmap_get(context_groups, context_key);
        TempTransitionList *trans_list = NULL;

        if (trans_list_ptr) {
            trans_list = *trans_list_ptr;
        } else {
            trans_list = malloc(sizeof(TempTransitionList));
            if (!trans_list) {
                fprintf(stderr, "ERROR: Failed to allocate TempTransitionList\n");
                continue;
            }
            initTempList(trans_list);

            // Store pointer to the list (not the list itself)
            TempTransitionList *list_ptr = trans_list;
            if (!hashmap_put(context_groups, context_key, &list_ptr, sizeof(TempTransitionList*))) {
                fprintf(stderr, "ERROR: Failed to put context group in hashmap\n");
                freeTempList(trans_list);
                free(trans_list);
                continue;
            }
        }

        // Add to temp list with error checking
        if (trans_list) {
            addToTempList(trans_list, next_op, prob);
        }

        transitions_processed++;

        if (verbose && transitions_processed % 10000 == 0) {
            fprintf(stderr, "  Processed %d/%zu transitions...\r", transitions_processed, total_transitions);
            fflush(stderr);
        }
    }

    hashmap_iterator_free(iter);

    if (verbose) {
        fprintf(stderr, "\nTransition processing complete:\n");
        fprintf(stderr, "  Processed: %d transitions\n", transitions_processed);
        fprintf(stderr, "  Parse errors: %d\n", parse_errors);
        fprintf(stderr, "  Probability errors: %d\n", prob_calc_errors);
        fprintf(stderr, "  Context groups: %zu\n", hashmap_size(context_groups));
    }

    if (hashmap_size(context_groups) == 0) {
        fprintf(stderr, "ERROR: No valid context groups created!\n");
        hashmap_free(context_groups);
        return;
    }

    // Build final lookup arrays
    lookup_count = hashmap_size(context_groups);
    transition_lookups = malloc(lookup_count * sizeof(TransitionLookup));
    if (!transition_lookups) {
        fprintf(stderr, "ERROR: Failed to allocate transition_lookups\n");
        hashmap_free(context_groups);
        return;
    }

    context_lookup_map = hashmap_create_with_size(lookup_count * 2);
    if (!context_lookup_map) {
        fprintf(stderr, "ERROR: Failed to create context_lookup_map\n");
        free(transition_lookups);
        transition_lookups = NULL;
        hashmap_free(context_groups);
        return;
    }

    if (verbose) {
        fprintf(stderr, "Building final lookup arrays for %d contexts...\n", lookup_count);
    }

    int lookup_idx = 0;
    iter = hashmap_iterator(context_groups);
    if (!iter) {
        fprintf(stderr, "ERROR: Failed to create context groups iterator\n");
        freeKNTransitionLookups();
        hashmap_free(context_groups);
        return;
    }

    while (hashmap_iter_has_next(iter)) {
        HashMapEntry *entry = hashmap_iter_next(iter);
        if (!entry || !entry->key || !entry->value) continue;

        TempTransitionList **trans_list_ptr = (TempTransitionList**)entry->value;
        if (!trans_list_ptr || !*trans_list_ptr) {
            if (verbose) {
                fprintf(stderr, "  WARNING: NULL transition list for context: '%s'\n", entry->key);
            }
            continue;
        }

        TempTransitionList *trans_list = *trans_list_ptr;
        if (trans_list->count == 0) {
            if (verbose) {
                fprintf(stderr, "  WARNING: Empty transition list for context: '%s'\n", entry->key);
            }
            freeTempList(trans_list);
            free(trans_list);
            continue;
        }

        if (lookup_idx >= lookup_count) {
            fprintf(stderr, "ERROR: lookup_idx (%d) >= lookup_count (%d)\n", lookup_idx, lookup_count);
            break;
        }

        TransitionLookup *lookup = &transition_lookups[lookup_idx];
        memset(lookup, 0, sizeof(TransitionLookup)); // Initialize to zero

        // Parse context from key with error checking
        char *context_copy = strdup(entry->key);
        if (!context_copy) {
            fprintf(stderr, "ERROR: Failed to duplicate context key\n");
            freeTempList(trans_list);
            free(trans_list);
            continue;
        }

        char *token = strtok(context_copy, "|");
        int ctx_idx = 0;

        while (token && ctx_idx < MAX_NGRAM_ORDER) {
            if (strlen(token) >= sizeof(lookup->context[ctx_idx].full_op)) {
                if (verbose) {
                    fprintf(stderr, "  WARNING: Context token too long: '%s'\n", token);
                }
                break;
            }
            strcpy(lookup->context[ctx_idx].full_op, token);
            lookup->context[ctx_idx].length = strlen(token);
            ctx_idx++;
            token = strtok(NULL, "|");
        }
        lookup->context_len = ctx_idx;
        free(context_copy);

        if (lookup->context_len == 0) {
            if (verbose) {
                fprintf(stderr, "  WARNING: Failed to parse context for lookup %d\n", lookup_idx);
            }
            freeTempList(trans_list);
            free(trans_list);
            continue;
        }

        // Copy and sort transitions
        lookup->transition_count = trans_list->count;
        lookup->next_ops = malloc(trans_list->count * sizeof(CompleteOperation));
        lookup->next_probs = malloc(trans_list->count * sizeof(double));

        if (!lookup->next_ops || !lookup->next_probs) {
            fprintf(stderr, "ERROR: Failed to allocate transition arrays for lookup %d\n", lookup_idx);
            if (lookup->next_ops) free(lookup->next_ops);
            if (lookup->next_probs) free(lookup->next_probs);
            freeTempList(trans_list);
            free(trans_list);
            continue;
        }

        for (int i = 0; i < trans_list->count; i++) {
            lookup->next_ops[i] = trans_list->ops[i];
            lookup->next_probs[i] = trans_list->probs[i];
        }

        // Sort by probability (descending)
        qsortTransitions(lookup->next_ops, lookup->next_probs, lookup->transition_count);

        // Add to context lookup map
        if (!hashmap_put(context_lookup_map, entry->key, &lookup_idx, sizeof(int))) {
            fprintf(stderr, "ERROR: Failed to put lookup index in context map\n");
        }

        // Cleanup temp list
        freeTempList(trans_list);
        free(trans_list);
        lookup_idx++;

        if (verbose && lookup_idx % 1000 == 0) {
            fprintf(stderr, "  Built %d/%d lookups...\r", lookup_idx, lookup_count);
            fflush(stderr);
        }
    }

    hashmap_iterator_free(iter);
    hashmap_free(context_groups);

    // Update actual lookup count (may be less due to errors)
    lookup_count = lookup_idx;

    if (verbose) {
        fprintf(stderr, "\nBuilt %d sparse lookups with on-demand KN probabilities\n", lookup_count);

        long total_transitions = 0;
        int min_transitions = INT_MAX;
        int max_transitions = 0;

        for (int i = 0; i < lookup_count; i++) {
            int count = transition_lookups[i].transition_count;
            total_transitions += count;
            if (count > max_transitions) max_transitions = count;
            if (count < min_transitions) min_transitions = count;
        }

        fprintf(stderr, "Total transitions: %ld (avg %.1f per context)\n",
               total_transitions, (double)total_transitions / lookup_count);
        fprintf(stderr, "Min/Max transitions per context: %d/%d\n", min_transitions, max_transitions);
    }
}



void freeKNTransitionLookups() {
    if (transition_lookups) {
        for (int i = 0; i < lookup_count; i++) {
            if (transition_lookups[i].next_ops) {
                free(transition_lookups[i].next_ops);
            }
            if (transition_lookups[i].next_probs) {
                free(transition_lookups[i].next_probs);
            }
        }
        free(transition_lookups);
        transition_lookups = NULL;
    }

    if (context_lookup_map) {
        hashmap_free(context_lookup_map);
        context_lookup_map = NULL;
    }

    lookup_count = 0;
}


void freeKNModelSafe(KNModel *model) {
    if (!model) return;

    // Free start_vecs (allocated in calc_continuation_based_starters)
    if (model->start_vecs) {
        free(model->start_vecs);
        model->start_vecs = NULL;
    }

    // Free the model itself
    free(model);
}



int fast_get_next_kn_sparse(CompleteOperation *context, int context_len,
                                   CompleteOperation **next_ops, double *next_probs,
                                   int *count, double min_prob, double curr_prob) {
    *count = 0;
    if (!context_lookup_map) {
        return 0; // Lookups not built
    }
    // Build context key and find lookup
    char context_key[MAX_RULE_LEN];
    buildContextKey(context, context_len, context_key);
    int *lookup_idx_ptr = (int*)hashmap_get(context_lookup_map, context_key);
    if (!lookup_idx_ptr) {
        return 0; // No transitions found for this context
    }
    int lookup_idx = *lookup_idx_ptr;
    if (lookup_idx < 0 || lookup_idx >= lookup_count) {
        return 0; // Invalid lookup index
    }
    TransitionLookup *lookup = &transition_lookups[lookup_idx];
    if (!lookup->next_ops || lookup->transition_count == 0) {
        return 0; // No transitions
    }
    // Process precomputed transitions with early termination
    double prob_threshold = (min_prob > 0.0) ? min_prob * 0.1 / curr_prob : 0.0;
    for (int i = 0; i < lookup->transition_count && *count < MAX_TRANSITIONS; i++) {
        double transition_prob = lookup->next_probs[i];
        // Early termination - lookups are sorted by probability!
        if (transition_prob < prob_threshold && min_prob > 0.0) {
            break;
        }
        next_ops[*count] = &lookup->next_ops[i];
        next_probs[*count] = transition_prob;
        (*count)++;
    }
    return *count;
}

