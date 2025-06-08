#include <Judy.h>
#include "processor.h"
#include "types.h"

#include "hash_tables.h"
#include "buffer.h"

#define MAXLINE 1000000

int counter = 0;
static FastTransitionLookup *fast_lookup = NULL;
static int fast_lookup_count = 0;


Pvoid_t   PJArray = (PWord_t)NULL;
PWord_t   PValue;
Word_t    Bytes;
uint8_t   Index[MAXLINE];

extern int max_operation_count[4];
// Compare chains
int compareTransitionsByProbability(const void *a, const void *b)
{
    SortedTransition *trans_a = (SortedTransition *)a;
    SortedTransition *trans_b = (SortedTransition *)b;

    // Sort by probability descending, then by frequency descending
    if (trans_b->probability != trans_a->probability)
    {
        return (trans_b->probability > trans_a->probability) ? 1 : -1;
    }
    return trans_b->frequency - trans_a->frequency;
}


int compareCompleteOps(const CompleteOperation *op1, const CompleteOperation *op2) {
    return strcmp(op1->full_op, op2->full_op) == 0;
}


int compareNGramsByFrequency(const void *a, const void *b)
{
    OperationNGram *ngram_a = (OperationNGram *)a;
    OperationNGram *ngram_b = (OperationNGram *)b;
    return ngram_b->frequency - ngram_a->frequency; // Descending order
}

// Build optimised lookup table after analysis is complete
void buildLookupTable(int verbose) {
    if (verbose) {
        fprintf(stderr, "Building transition lookup table from bigrams...\n");
    }

    CompleteOperation *unique_ops = malloc(max_operation_count[2] * sizeof(CompleteOperation));
    long unique_from_count = 0;

    // Find all unique from_ops (first operation in bigrams)
    for (int i = 0; i < max_operation_count[2]; i++) {
        NGramHashNode *node = bigram_hash_table[i];
        while (node != NULL) {
            if (node->ngram.op_count == 2) {
                long found = 0;
                for (long j = 0; j < unique_from_count; j++) {
                    if (compareCompleteOps(&unique_ops[j], &node->ngram.ops[0])) {
                        found = 1;
                        break;
                    }
                }
                if (!found && unique_from_count < max_operation_count[2]) {
                    unique_ops[unique_from_count] = node->ngram.ops[0];
                    unique_from_count++;
                }
            }
            node = node->next;
        }
    }

    if (verbose) {
        fprintf(stderr, "Found %ld unique 'from' operations\n", unique_from_count);
    }

    // Allocate lookup table
    fast_lookup = malloc(unique_from_count * sizeof(FastTransitionLookup));

    if (fast_lookup == NULL) {
        fprintf(stderr, "ERROR: Failed to allocate fast lookup table\n");
        free(unique_ops);
        return;
    }

    fast_lookup_count = 0;

    // Build lookup table for each unique 'from' operation
    for (long i = 0; i < unique_from_count; i++) {
        CompleteOperation *current_from_op = &unique_ops[i];

        // Count bigrams for this 'from' operation
        long trans_count = 0;
        for (int j = 0; j < max_operation_count[2]; j++) {
            NGramHashNode *node = bigram_hash_table[j];
            while (node != NULL) {
                if (node->ngram.op_count == 2 &&
                    compareCompleteOps(&node->ngram.ops[0], current_from_op)) {
                    trans_count++;
                }
                node = node->next;
            }
        }

        if (trans_count == 0) continue; // Skip if no transitions

        // Allocate sorted transitions array
        FastTransitionLookup *lookup = &fast_lookup[fast_lookup_count];
        lookup->from_op = *current_from_op;
        lookup->sorted_transitions = malloc(trans_count * sizeof(SortedTransition));
        lookup->transition_count = trans_count;
        lookup->max_probability = 0.0;
        lookup->min_probability = 1.0;

        if (lookup->sorted_transitions == NULL) {
            fprintf(stderr, "ERROR: Failed to allocate sorted transitions\n");
            continue;
        }

        // Populate sorted transitions from bigrams
        int sorted_index = 0;
        for (int j = 0; j < max_operation_count[2]; j++) {
            NGramHashNode *node = bigram_hash_table[j];
            while (node != NULL && sorted_index < trans_count) {
                if (node->ngram.op_count == 2 &&
                    compareCompleteOps(&node->ngram.ops[0], current_from_op)) {

                    lookup->sorted_transitions[sorted_index].next_op = &node->ngram.ops[1];
                    lookup->sorted_transitions[sorted_index].probability = node->ngram.probability;
                    lookup->sorted_transitions[sorted_index].frequency = node->ngram.frequency;

                    // Track min/max probabilities
                    if (node->ngram.probability > lookup->max_probability) {
                        lookup->max_probability = node->ngram.probability;
                    }
                    if (node->ngram.probability < lookup->min_probability) {
                        lookup->min_probability = node->ngram.probability;
                    }

                    sorted_index++;
                }
                node = node->next;
            }
        }

        // Sort transitions by probability (descending)
        qsort(lookup->sorted_transitions, lookup->transition_count,
              sizeof(SortedTransition), compareTransitionsByProbability);

        fast_lookup_count++;

        if (verbose && i < 10) {
            fprintf(stderr, "  Operation '%s': %d transitions, prob range: %.4f - %.4f\n",
                    lookup->from_op.full_op, lookup->transition_count,
                    lookup->min_probability, lookup->max_probability);
        }
    }

    free(unique_ops);

    if (verbose) {
        fprintf(stderr, "Lookup table built: %d unique operations\n", fast_lookup_count);
    }
}
// Fast lookup with early probability pruning
int getNextOperations(CompleteOperation *current_op, CompleteOperation **next_ops,
                     double *next_probs, int *count, double min_probability,
                     double current_rule_probability) {
    *count = 0;

    if (current_op == NULL || fast_lookup == NULL) {
        return 0;
    }

    // Find the lookup entry for this operation - O(log n) or O(1) with hash
    FastTransitionLookup *lookup = NULL;
    for (int i = 0; i < fast_lookup_count; i++) {
        if (compareCompleteOps(&fast_lookup[i].from_op, current_op)) {
            lookup = &fast_lookup[i];
            break;
        }
    }

    if (lookup == NULL || lookup->sorted_transitions == NULL) {
        return 0; // No transitions found
    }

    // Early pruning: check if even the best transition would meet the threshold
    double best_possible_probability = current_rule_probability * lookup->max_probability;
    if (best_possible_probability < min_probability && min_probability > 0.0) {
        return 0; // Early pruning
    }

    // Iterate through PRE-SORTED transitions (highest probability first)
    for (int i = 0; i < lookup->transition_count && *count < max_operation_count[2]; i++) {
        double transition_prob = lookup->sorted_transitions[i].probability;
        double new_rule_probability = current_rule_probability * transition_prob;

        // Early termination: since transitions are sorted by probability,
        // if this one doesn't meet threshold, none of the remaining ones will
        if (new_rule_probability < min_probability && min_probability > 0.0) {
            break; // This is the key optimization!
        }

        if (lookup->sorted_transitions[i].next_op == NULL) {
            continue;
        }

        // Add valid transition
        next_ops[*count] = lookup->sorted_transitions[i].next_op;
        next_probs[*count] = transition_prob;
        (*count)++;
    }

    return *count;
}

// Optimised output function that doesn't recalculate probability
void outputRule(CompleteOperation *ops, int length, int min_length,
                         double min_probability, double rule_probability,
                         WBuffer *output_buffer)
{

    if (length < min_length || (min_probability > 0.0 && rule_probability < min_probability))
    {
        return;
    }

    if (ops == NULL || length <= 0 || length > MAX_RULE_LEN)
    {
        return;
    }

    // Build rule string with bounds checking
    char rule_string[MAX_RULE_LEN] = "";
    int total_len = 0;

    for (int i = 0; i < length; i++)
    {
        int op_len = strlen(ops[i].full_op);
        if (total_len + op_len >= MAX_RULE_LEN - 1)
        {
            break; // Rule would be too long, truncate
        }
        strcat(rule_string, ops[i].full_op);
        total_len += op_len;
    }


    JSLG(PValue,PJArray,rule_string)
    if (PValue != NULL)
    {
        return;
    }

    buffer_string2(output_buffer, rule_string, MAX_RULE_LEN);
    JSLI(PValue, PJArray, rule_string);
    ++(*PValue);
    // Periodic buffer flush
    if (output_buffer->writeCount % 1000 == 0)
    {
        flush_buffer(output_buffer);
    }
}

OperationNGram *getSortedUnigramsFromHashTable(int *count, int limit_unigrams)
{
    // Extract unigrams from hash table
    *count = 0;
    for (int i = 0; i < UNIGRAM_HASH_SIZE; i++)
    {
        NGramHashNode *node = unigram_hash_table[i];
        while (node != NULL)
        {
            if (node->ngram.op_count == 1)
            {
                (*count)++;
            }
            node = node->next;
        }
    }

    if (*count == 0)
        return NULL;

    // Allocate and fill array
    OperationNGram *unigrams = malloc(*count * sizeof(OperationNGram));
    if (unigrams == NULL)
        return NULL;

    int index = 0;
    for (int i = 0; i < UNIGRAM_HASH_SIZE; i++)
    {
        NGramHashNode *node = unigram_hash_table[i];
        while (node != NULL && index < *count)
        {
            if (node->ngram.op_count == 1)
            {
                unigrams[index] = node->ngram;
                index++;
            }
            node = node->next;
        }
    }

    // Sort by frequency (descending)
    qsort(unigrams, *count, sizeof(OperationNGram), compareNGramsByFrequency);

    // Apply limit if specified
    if (limit_unigrams >= 1 && limit_unigrams < *count)
    {
        *count = limit_unigrams;
    }

    return unigrams;
}

// Recursive function
void generateRules(CompleteOperation *current_sequence, int current_length,
                            int target_length, int max_depth, int min_length,
                            double min_probability, double current_probability, int verbose,
                            OperationNGram *unigrams, int unigram_count,
                            WBuffer *output_buffer,
                            double limit_unigrams)
{

    // Prevent infinite recursion
    if (current_length > max_depth || current_length > MAX_RULE_LEN)
    {
        return;
    }

    // Early pruning: cull subtree if below threshold (works since we are sorted)
    if (min_probability > 0.0 && current_probability < min_probability)
    {
        return;
    }

    // Output current sequence if it meets criteria
    if (current_length >= min_length && current_length <= target_length)
    {
        outputRule(current_sequence, current_length, min_length,
                            min_probability, current_probability, output_buffer);
    }

    // Stop if we've reached the target length
    if (current_length >= target_length)
    {
        return;
    }

    // First operation: start with highest frequency unigrams
    if (current_length == 0)
    {

        int max_unigrams = unigram_count;

        if (limit_unigrams >= 1)
            max_unigrams = limit_unigrams;
        else if(limit_unigrams < 1 && limit_unigrams > 0)
        {
            max_unigrams = (int)(limit_unigrams * max_unigrams);
        }


        for (int i = 0; i < max_unigrams; i++)
        {
            current_sequence[0] = unigrams[i].ops[0];
            generateRules(current_sequence, 1, target_length, max_depth,
                                   min_length, min_probability, 1.0, verbose,
                                   unigrams, unigram_count, output_buffer,
                                   limit_unigrams);
        }
        return;
    }

    CompleteOperation **next_ops = malloc(max_operation_count[2] * sizeof(CompleteOperation *));
    double *next_probs = malloc(max_operation_count[2] * sizeof(double));

    int next_count = 0;
    getNextOperations(&current_sequence[current_length - 1],
                                            next_ops, next_probs, &next_count,
                                            min_probability, current_probability);

    for (int i = 0; i < next_count; i++)
    {
        double new_probability = current_probability * next_probs[i];
        // Skip if probability is too low
        if (min_probability > 0.0 && new_probability < min_probability)
        {
            break; // Remaining transitions will have even lower probability
        }
        if (next_ops[i] == NULL)
        {
            continue;
        }
        current_sequence[current_length] = *next_ops[i];
        generateRules(current_sequence, current_length + 1, target_length, max_depth,
                               min_length, min_probability, new_probability, verbose,
                               unigrams, unigram_count, output_buffer,
                                limit_unigrams);
    }

    free(next_ops);
    free(next_probs);
}

void generateRulesFromHT(int max_length, int min_length, double min_probability, int verbose,
                        WBuffer *output_buffer, long limit_unigrams) {

  int starter_count_local = 0;

    OperationNGram *sorted_starters = getSortedStarterOperationsFromHT(&starter_count_local, limit_unigrams);

    // Fall back to regular unigrams if no starters found
    if (sorted_starters == NULL || starter_count_local == 0) {
        if (verbose) {
            fprintf(stderr, "Warning: No starter operations found, falling back to regular unigrams\n");
        }
        sorted_starters = getSortedUnigramsFromHashTable(&starter_count_local, limit_unigrams);
    }

    if (sorted_starters == NULL || starter_count_local == 0) {
        fprintf(stderr, "Error: No starting operations found - cannot generate rules!\n");
        return;
    }

    if (verbose)
    {
        fprintf(stderr, "\n=== Rule Generation (length: %d-%d, min probability: %.3f) ===\n",
                min_length, max_length, min_probability);
    }


    if (sorted_starters == NULL || starter_count_local == 0)
    {
        fprintf(stderr, "Error: No unigrams found - cannot generate rules!\n");
        return;
    }

    // Get bigram count instead of transition count
    long bigram_transition_count = getBigramCountFromHashTable();

    if (bigram_transition_count == 0) {
        fprintf(stderr, "Warning: No bigrams found - only single-operation rules possible\n");
    }

    // Build lookup table from bigrams (no longer needs transitions array)
    buildLookupTable(verbose);

    if (verbose) {
        fprintf(stderr, "Starting generation with probability pruning...\n");
        fprintf(stderr, "Processing %d unigrams with %ld bigrams\n", starter_count_local, bigram_transition_count);
    }

    CompleteOperation *sequence = malloc(MAX_RULE_LEN * sizeof(CompleteOperation));
    if (sequence == NULL) {
        fprintf(stderr, "ERROR: Failed to allocate memory for sequence\n");
        free(sorted_starters);
        return;
    }

    // Generate rules of each length
    long last_write = 0;
    for (int target_length = min_length; target_length <= max_length; target_length++) {
        if (verbose) {
            fprintf(stderr, "Processing rules of length %d...\n", target_length);
        }

        generateRules(sequence, 0, target_length, target_length,
                     min_length, min_probability, 1.0, verbose,
                     sorted_starters, starter_count_local, output_buffer,
                     limit_unigrams);

        flush_buffer(output_buffer);

        if (verbose) {
            fprintf(stderr, "Completed length %d %zu|%zu\n",
                    target_length, output_buffer->writeCount-last_write, output_buffer->writeCount);
            last_write = output_buffer->writeCount;
        }
    }

    // Cleanup
    free(sequence);
    free(sorted_starters);

    if (verbose) {
        fprintf(stderr, "Generation complete. Total rules: %zu\n",
                output_buffer->writeCount);
    }
}

OperationNGram *getSortedStarterOperationsFromHT(int *count, double limit_unigrams) {
    long total_starter_freq = 0;
    long full_NGramWidth = 0;

    for (int i = 0; i < STARTER_HASH_SIZE; i++) {
        NGramHashNode *node = starter_hash_table[i];
        while (node != NULL) {
            if (node->ngram.op_count == 1) {
                total_starter_freq += node->ngram.frequency;
            }
            node = node->next;
        }
    }

    for (int i = 0; i < UNIGRAM_HASH_SIZE; i++) {
        NGramHashNode *node = unigram_hash_table[i];
        while (node != NULL) {
            if (node->ngram.op_count == 1) {
                full_NGramWidth++;
            }
            node = node->next;
        }
    }

    if (full_NGramWidth == 0) return NULL;

    OperationNGram *starters = malloc(full_NGramWidth * sizeof(OperationNGram));
    if (starters == NULL) return NULL;

    int index = 0;

        for (int i = 0; i < UNIGRAM_HASH_SIZE; i++) {
        NGramHashNode *unigram_node = unigram_hash_table[i];
        while (unigram_node != NULL && index < full_NGramWidth) {
            if (unigram_node->ngram.op_count == 1) {

                int starter_freq = 0;
                unsigned int hash_index = hashNGram(&unigram_node->ngram.ops[0], 1, STARTER_HASH_SIZE);
                NGramHashNode *starter_node = starter_hash_table[hash_index];

                while (starter_node != NULL) {
                    if (starter_node->ngram.op_count == 1 &&
                        compareCompleteOps(&starter_node->ngram.ops[0],
                                         &unigram_node->ngram.ops[0])) {
                        starter_freq = starter_node->ngram.frequency;
                        break;
                    }
                    starter_node = starter_node->next;
                }

                double smoothed_prob = (double)(starter_freq + K_SMOOTHING_FACTOR) /
                                     (total_starter_freq + K_SMOOTHING_FACTOR * full_NGramWidth);

                starters[index] = unigram_node->ngram;
                starters[index].frequency = (int)(smoothed_prob * 1000000);

                index++;
            }
            unigram_node = unigram_node->next;
        }
    }

    qsort(starters, index, sizeof(OperationNGram), compareNGramsByFrequency);

    *count = index;
    if (limit_unigrams > 1 && limit_unigrams < *count) {
        *count = limit_unigrams;
    }

    return starters;
}

long getBigramCountFromHashTable()
{
    long count = 0;
    for (int i = 0; i < max_operation_count[2]; i++)
    {
        NGramHashNode *node = bigram_hash_table[i];
        while (node != NULL)
        {
            if (node->ngram.op_count == 2)
            {
                count++;
            }
            node = node->next;
        }
    }
    return count;
}

void freeLookupTable(void)
{
    if (fast_lookup != NULL)
    {
        for (int i = 0; i < fast_lookup_count; i++)
        {
            if (fast_lookup[i].sorted_transitions != NULL)
            {
                free(fast_lookup[i].sorted_transitions);
            }
        }
        free(fast_lookup);
        fast_lookup = NULL;
        fast_lookup_count = 0;
    }
}
