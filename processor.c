
#include "processor.h"
#include "types.h"

#include "hash_tables.h"
#include "buffer.h"

// Global optimised lookup table
static FastTransitionLookup *fast_lookup = NULL;
static int fast_lookup_count = 0;

static int compareCompleteOps(const CompleteOperation *op1, const CompleteOperation *op2)
{
    return strcmp(op1->full_op, op2->full_op) == 0;
}

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



int compareNGramsByFrequency(const void *a, const void *b)
{
    OperationNGram *ngram_a = (OperationNGram *)a;
    OperationNGram *ngram_b = (OperationNGram *)b;
    return ngram_b->frequency - ngram_a->frequency; // Descending order
}

// Build optimised lookup table after analysis is complete
void buildLookupTable(OperationTransition *transitions, int transition_count, int verbose)
{

    if (verbose)
    {
        fprintf(stderr, "Building transition lookup table...\n");
    }

    CompleteOperation *unique_ops = malloc(MAX_TRANSITIONS * sizeof(CompleteOperation));

    long unique_from_count = 0;

    for (long i = 0; i < transition_count; i++)
    {
        long found = 0;
        for (long j = 0; j < unique_from_count; j++)
        {
            if (compareCompleteOps(&unique_ops[j], &transitions[i].from_op))
            {
                found = 1;
                break;
            }
        }
        if (!found && unique_from_count < MAX_TRANSITIONS)
        {
            unique_ops[unique_from_count] = transitions[i].from_op;
            unique_from_count++;
        }
    }

    if (verbose)
    {
        fprintf(stderr, "Found %ld unique 'from' operations\n", unique_from_count);
    }

    // Allocate lookup table
    fast_lookup = malloc(unique_from_count * sizeof(FastTransitionLookup));
    if (fast_lookup == NULL)
    {
        fprintf(stderr, "ERROR: Failed to allocate fast lookup table\n");
        return;
    }

    fast_lookup_count = 0;

    // Build lookup table for each unique 'from' operation
    for (long i = 0; i < unique_from_count; i++)
    {
        CompleteOperation *current_from_op = &unique_ops[i];

        // Count transitions for this 'from' operation
        long trans_count = 0;
        for (long j = 0; j < transition_count; j++)
        {
            if (compareCompleteOps(&transitions[j].from_op, current_from_op))
            {
                trans_count++;
            }
        }

        if (trans_count == 0)
            continue; // Skip if no transitions

        // Allocate sorted transitions array
        FastTransitionLookup *lookup = &fast_lookup[fast_lookup_count];
        lookup->from_op = *current_from_op;
        lookup->sorted_transitions = malloc(trans_count * sizeof(SortedTransition));
        lookup->transition_count = trans_count;
        lookup->max_probability = 0.0;
        lookup->min_probability = 1.0;

        if (lookup->sorted_transitions == NULL)
        {
            fprintf(stderr, "ERROR: Failed to allocate sorted transitions\n");
            continue;
        }

        // Populate sorted transitions
        int sorted_index = 0;
        for (int j = 0; j < transition_count; j++)
        {
            if (compareCompleteOps(&transitions[j].from_op, current_from_op))
            {
                lookup->sorted_transitions[sorted_index].next_op = &transitions[j].to_op;
                lookup->sorted_transitions[sorted_index].probability = transitions[j].probability;
                lookup->sorted_transitions[sorted_index].frequency = transitions[j].count;

                // Track min/max probabilities
                if (transitions[j].probability > lookup->max_probability)
                {
                    lookup->max_probability = transitions[j].probability;
                }
                if (transitions[j].probability < lookup->min_probability)
                {
                    lookup->min_probability = transitions[j].probability;
                }

                sorted_index++;
            }
        }

        // Sort transitions by probability (descending)
        qsort(lookup->sorted_transitions, lookup->transition_count,
              sizeof(SortedTransition), compareTransitionsByProbability);

        fast_lookup_count++;

        if (verbose && i < 10)
        {
            fprintf(stderr, "  Operation '%s': %d transitions, prob range: %.4f - %.4f\n",
                    lookup->from_op.full_op, lookup->transition_count,
                    lookup->min_probability, lookup->max_probability);
        }
    }

    free(unique_ops);

    if (verbose)
    {
        fprintf(stderr, "Lookup table built: %d unique operations\n", fast_lookup_count);
    }
}

// Fast lookup with early probability pruning
int getNextOperations(CompleteOperation *current_op, CompleteOperation **next_ops,
                               double *next_probs, int *count, double min_probability,
                               double current_rule_probability)
{
    *count = 0;

    if (current_op == NULL || fast_lookup == NULL)
    {
        return 0;
    }

    // Find the lookup entry for this operation
    FastTransitionLookup *lookup = NULL;
    for (int i = 0; i < fast_lookup_count; i++)
    {
        if (compareCompleteOps(&fast_lookup[i].from_op, current_op))
        {
            lookup = &fast_lookup[i];
            break;
        }
    }

    if (lookup == NULL || lookup->sorted_transitions == NULL)
    {
        return 0; // No transitions found
    }

    // Early pruning: check if even the best transition would meet the threshold
    double best_possible_probability = current_rule_probability * lookup->max_probability;
    if (best_possible_probability < min_probability && min_probability > 0.0)
    {
        return 0; // Early pruning
    }

    // Iterate through sorted transitions (highest probability first)
    for (int i = 0; i < lookup->transition_count && *count < MAX_TRANSITIONS; i++)
    {
        double transition_prob = lookup->sorted_transitions[i].probability;
        double new_rule_probability = current_rule_probability * transition_prob;

        // Early termination: if this transition doesn't meet threshold,
        // no subsequent ones will either (since they're sorted by probability)
        if (new_rule_probability < min_probability && min_probability > 0.0)
        {
            break;
        }

        if (lookup->sorted_transitions[i].next_op == NULL)
        {
            continue;
        }

        // Add valid transition
        next_ops[*count] = lookup->sorted_transitions[i].next_op;
        next_probs[*count] = transition_prob;
        (*count)++;
    }

    return *count;
}

// Sort unigrams by frequency for optimal starting order
void sortUnigramsByFrequency(OperationNGram *unigrams, int unigram_count)
{
    qsort(unigrams, unigram_count, sizeof(OperationNGram), compareNGramsByFrequency);
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

    // Check if rule already exists
    if (ruleExists(rule_string))
    {
        return;
    }

    // Add to hash table and output
    addRuleToHash(rule_string);
    buffer_string2(output_buffer, rule_string, MAX_RULE_LEN);

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
    if (limit_unigrams > 0 && limit_unigrams < *count)
    {
        *count = limit_unigrams;
    }

    return unigrams;
}

// Get all transitions from hash table (for buildOptimisedLookupTable)
OperationTransition *getAllTransitionsFromHashTable(long *count)
{
    // Count total transitions
    *count = 0;
    for (int i = 0; i < TRANSITION_HASH_SIZE; i++)
    {
        TransitionHashNode *node = transition_hash_table[i];
        while (node != NULL)
        {
            (*count)++;
            node = node->next;
        }
    }

    if (*count == 0)
        return NULL;

    // Allocate and fill array
    OperationTransition *transitions_array = malloc(*count * sizeof(OperationTransition));
    if (transitions_array == NULL)
        return NULL;

    int index = 0;
    for (int i = 0; i < TRANSITION_HASH_SIZE; i++)
    {
        TransitionHashNode *node = transition_hash_table[i];
        while (node != NULL && index < *count)
        {
            transitions_array[index] = node->transition;
            index++;
            node = node->next;
        }
    }

    return transitions_array;
}

// Recursive function
void generateRules(CompleteOperation *current_sequence, int current_length,
                            int target_length, int max_depth, int min_length,
                            double min_probability, double current_probability, int verbose,
                            OperationNGram *unigrams, int unigram_count,
                            WBuffer *output_buffer,
                            long limit_unigrams)
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

        if (limit_unigrams != 0)
            max_unigrams = limit_unigrams;

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

    CompleteOperation **next_ops = malloc(MAX_TRANSITIONS * sizeof(CompleteOperation *));
    double *next_probs = malloc(MAX_TRANSITIONS * sizeof(double));

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


void generateRulesFromHT(int max_length, int min_length,double min_probability, int verbose,
                                                WBuffer *output_buffer,long limit_unigrams)
{

    if (verbose)
    {
        fprintf(stderr, "\n=== Rule Generation (length: %d-%d, min probability: %.3f) ===\n",
                min_length, max_length, min_probability);
    }

    // Get sorted unigrams directly from hash table
    int unigram_count = 0;
    OperationNGram *sorted_unigrams = getSortedUnigramsFromHashTable(&unigram_count, limit_unigrams);

    if (sorted_unigrams == NULL || unigram_count == 0)
    {
        fprintf(stderr, "Error: No unigrams found - cannot generate rules!\n");
        return;
    }

    // Get transitions from hash table
    long transition_count = 0;

    OperationTransition *transitions_array = getAllTransitionsFromHashTable(&transition_count);

    if (transitions_array == NULL || transition_count == 0)
    {
        fprintf(stderr, "Warning: No transitions found - only single-operation rules possible\n");
    }

    buildLookupTable(transitions_array, transition_count, verbose);

    if (verbose)
    {
        fprintf(stderr, "Starting generation with probability pruning...\n");
        fprintf(stderr, "Processing %d unigrams with %ld transitions\n", unigram_count, transition_count);
    }

    CompleteOperation *sequence = malloc(MAX_RULE_LEN * sizeof(CompleteOperation));
    if (sequence == NULL)
    {
        fprintf(stderr, "ERROR: Failed to allocate memory for sequence\n");
        free(sorted_unigrams);
        if (transitions_array)
            free(transitions_array);
        return;
    }

    // Generate rules of each length
    long last_write = 0;
    for (int target_length = min_length; target_length <= max_length; target_length++)
    {
        if (verbose)
        {
            fprintf(stderr, "Processing rules of length %d...\n", target_length);
        }

        generateRules(sequence, 0, target_length, target_length,
                               min_length, min_probability, 1.0, verbose,
                               sorted_unigrams, unigram_count, output_buffer,
                                limit_unigrams);

        flush_buffer(output_buffer);

        if (verbose)
        {
            fprintf(stderr, "Completed length %d %zu|%zu\n",
                    target_length, output_buffer->writeCount-last_write,output_buffer->writeCount);
            last_write = output_buffer->writeCount;
        }
    }

    // Cleanup
    free(sequence);
    free(sorted_unigrams);
    if (transitions_array)
        free(transitions_array);

    if (verbose)
    {
        fprintf(stderr, "Generation complete. Total rules: %zu\n",
                output_buffer->writeCount);
    }
}

// Count functions for compatibility with existing code
long getUnigramCountFromHashTable()
{
    long count = 0;
    for (int i = 0; i < UNIGRAM_HASH_SIZE; i++)
    {
        NGramHashNode *node = unigram_hash_table[i];
        while (node != NULL)
        {
            if (node->ngram.op_count == 1)
            {
                count++;
            }
            node = node->next;
        }
    }
    return count;
}

long getBigramCountFromHashTable()
{
    long count = 0;
    for (int i = 0; i < BIGRAM_HASH_SIZE; i++)
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

long getTrigramCountFromHashTable()
{
    long count = 0;
    for (int i = 0; i < TRIGRAM_HASH_SIZE; i++)
    {
        NGramHashNode *node = trigram_hash_table[i];
        while (node != NULL)
        {
            if (node->ngram.op_count == 3)
            {
                count++;
            }
            node = node->next;
        }
    }
    return count;
}

long getTransitionCountFromHashTable()
{
    long count = 0;
    for (int i = 0; i < TRANSITION_HASH_SIZE; i++)
    {
        TransitionHashNode *node = transition_hash_table[i];
        while (node != NULL)
        {
            count++;
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
