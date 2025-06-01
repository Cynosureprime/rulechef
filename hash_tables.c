
#include "hash_tables.h"
#include "rule_parser.h"

// Global hash tables
TransitionHashNode **transition_hash_table = NULL;
NGramHashNode **unigram_hash_table = NULL;
NGramHashNode **bigram_hash_table = NULL;
NGramHashNode **trigram_hash_table = NULL;
HashNode *hash_table[HASH_SIZE] = {NULL};

// Global counters
extern long unigram_count;
extern long bigram_count;
extern long trigram_count;
extern long transition_count;

void initHashTables() {
    transition_hash_table = calloc(TRANSITION_HASH_SIZE, sizeof(TransitionHashNode*));
    unigram_hash_table = calloc(UNIGRAM_HASH_SIZE, sizeof(NGramHashNode*));
    bigram_hash_table = calloc(BIGRAM_HASH_SIZE, sizeof(NGramHashNode*));
    trigram_hash_table = calloc(TRIGRAM_HASH_SIZE, sizeof(NGramHashNode*));

    if (!transition_hash_table || !unigram_hash_table || !bigram_hash_table || !trigram_hash_table) {
        fprintf(stderr, "Failed to allocate hash tables\n");
        exit(1);
    }
}

static void freeNGramHashTable(NGramHashNode **table, int size) {
    if (table) {
        for (int i = 0; i < size; i++) {
            NGramHashNode *node = table[i];
            while (node) {
                NGramHashNode *temp = node;
                node = node->next;
                free(temp);
            }
        }
        free(table);
    }
}

void freeHashTables() {
    // Free transition hash table
    if (transition_hash_table) {
        for (int i = 0; i < TRANSITION_HASH_SIZE; i++) {
            TransitionHashNode *node = transition_hash_table[i];
            while (node) {
                TransitionHashNode *temp = node;
                node = node->next;
                free(temp);
            }
        }
        free(transition_hash_table);
        transition_hash_table = NULL;
    }

    // Free n-gram hash tables using the helper function
    freeNGramHashTable(unigram_hash_table, UNIGRAM_HASH_SIZE);
    freeNGramHashTable(bigram_hash_table, BIGRAM_HASH_SIZE);
    freeNGramHashTable(trigram_hash_table, TRIGRAM_HASH_SIZE);

    // Set pointers to NULL after freeing
    unigram_hash_table = NULL;
    bigram_hash_table = NULL;
    trigram_hash_table = NULL;

    // Free rule deduplication hash table
    for (int i = 0; i < HASH_SIZE; i++) {
        HashNode *node = hash_table[i];
        while (node) {
            HashNode *temp = node;
            node = node->next;
            free(temp);
        }
        hash_table[i] = NULL; // Clear the bucket pointer
    }
}

unsigned int hashTransition(CompleteOperation *from_op, CompleteOperation *to_op) {
    unsigned int hash = 5381;

    // Hash the from_op
    const char *str1 = from_op->full_op;
    while (*str1) {
        hash = ((hash << 5) + hash) + *str1++;
    }

    // Combine with to_op hash
    const char *str2 = to_op->full_op;
    while (*str2) {
        hash = ((hash << 5) + hash) + *str2++;
    }

    return hash % TRANSITION_HASH_SIZE;
}

unsigned int hashNGram(CompleteOperation *ops, long op_count, int hash_size) {
    unsigned int hash = 5381;

    // Hash each operation in the n-gram
    for (long i = 0; i < op_count; i++) {
        const char *str = ops[i].full_op;
        while (*str) {
            hash = ((hash << 5) + hash) + *str++;
        }
        // Add separator to distinguish position
        hash = ((hash << 5) + hash) + '|';
    }

    return hash % hash_size;
}

unsigned int hash(char *str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % HASH_SIZE;
}

// Find existing transition in hash table
TransitionHashNode* findTransition(CompleteOperation *from_op, CompleteOperation *to_op) {
    unsigned int index = hashTransition(from_op, to_op);
    TransitionHashNode *node = transition_hash_table[index];

    while (node != NULL) {
        if (compareCompleteOps(&node->transition.from_op, from_op) &&
            compareCompleteOps(&node->transition.to_op, to_op)) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}

// Add or update transition using hash table
void addOperationTransitionHashed(CompleteOperation *from_op, CompleteOperation *to_op) {
    // Check if transition already exists
    TransitionHashNode *existing = findTransition(from_op, to_op);
    if (existing != NULL) {
        existing->transition.count++;
        return;
    }

    // Create new transition node
    TransitionHashNode *new_node = malloc(sizeof(TransitionHashNode));
    if (new_node == NULL) {
        fprintf(stderr, "Failed to allocate memory for transition hash node\n");
        return;
    }

    // Initialize the transition
    new_node->transition.from_op = *from_op;
    new_node->transition.to_op = *to_op;
    new_node->transition.count = 1;
    new_node->transition.probability = 0.0;

    // Insert at beginning of chain
    unsigned int index = hashTransition(from_op, to_op);
    new_node->next = transition_hash_table[index];
    transition_hash_table[index] = new_node;

    transition_count++;

    if (transition_count >= MAX_TRANSITIONS) {
        fprintf(stderr, "Warning: Approaching maximum transition limit (%d)\n", MAX_TRANSITIONS);
    }
}

// Calculate probabilities directly from hash table
void calculateTransitionProbabilitiesDirectly() {
    // Count totals for each unique from_op
    typedef struct {
        CompleteOperation from_op;
        int total_count;
    } FromOpTotal;

    FromOpTotal *from_totals = malloc(MAX_TRANSITIONS * sizeof(FromOpTotal));
    int from_total_count = 0;

    // Iterate through hash table to calculate totals
    for (int i = 0; i < TRANSITION_HASH_SIZE; i++) {
        TransitionHashNode *node = transition_hash_table[i];
        while (node != NULL) {
            // Find or create total for this from_op
            int found = 0;
            for (int j = 0; j < from_total_count; j++) {
                if (compareCompleteOps(&from_totals[j].from_op, &node->transition.from_op)) {
                    from_totals[j].total_count += node->transition.count;
                    found = 1;
                    break;
                }
            }
            if (!found && from_total_count < MAX_TRANSITIONS) {
                from_totals[from_total_count].from_op = node->transition.from_op;
                from_totals[from_total_count].total_count = node->transition.count;
                from_total_count++;
            }
            node = node->next;
        }
    }

    // Now calculate probabilities
    for (int i = 0; i < TRANSITION_HASH_SIZE; i++) {
        TransitionHashNode *node = transition_hash_table[i];
        while (node != NULL) {
            // Find the total for this from_op
            for (int j = 0; j < from_total_count; j++) {
                if (compareCompleteOps(&from_totals[j].from_op, &node->transition.from_op)) {
                    node->transition.probability = (double)node->transition.count / from_totals[j].total_count;
                    break;
                }
            }
            node = node->next;
        }
    }

    free(from_totals);
}

void printHashTableStats() {
    int used_buckets = 0;
    int max_chain_length = 0;
    int total_nodes = 0;

    for (int i = 0; i < TRANSITION_HASH_SIZE; i++) {
        if (transition_hash_table[i] != NULL) {
            used_buckets++;
            int chain_length = 0;
            TransitionHashNode *node = transition_hash_table[i];
            while (node != NULL) {
                chain_length++;
                total_nodes++;
                node = node->next;
            }
            if (chain_length > max_chain_length) {
                max_chain_length = chain_length;
            }
        }
    }

    fprintf(stderr, "Hash table stats:\n");
    fprintf(stderr, "  Total transitions: %d\n", total_nodes);
    fprintf(stderr, "  Used buckets: %d/%d (%.2f%%)\n",
            used_buckets, TRANSITION_HASH_SIZE,
            100.0 * used_buckets / TRANSITION_HASH_SIZE);
    fprintf(stderr, "  Max chain length: %d\n", max_chain_length);
    fprintf(stderr, "  Average chain length: %.2f\n",
            used_buckets > 0 ? (double)total_nodes / used_buckets : 0.0);
}

NGramHashNode* findNGram(CompleteOperation *ops, long op_count) {
    NGramHashNode **hash_table;
    int hash_size;

    // Select appropriate hash table based on n-gram size
    switch (op_count) {
        case 1:
            hash_table = unigram_hash_table;
            hash_size = UNIGRAM_HASH_SIZE;
            break;
        case 2:
            hash_table = bigram_hash_table;
            hash_size = BIGRAM_HASH_SIZE;
            break;
        case 3:
            hash_table = trigram_hash_table;
            hash_size = TRIGRAM_HASH_SIZE;
            break;
        default:
            return NULL; // Unsupported n-gram size
    }

    unsigned int index = hashNGram(ops, op_count, hash_size);
    NGramHashNode *node = hash_table[index];

    while (node != NULL) {
        if (node->ngram.op_count == op_count) {
            int match = 1;
            for (long j = 0; j < op_count; j++) {
                if (!compareCompleteOps(&node->ngram.ops[j], &ops[j])) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                return node;
            }
        }
        node = node->next;
    }
    return NULL;
}

void addOperationNGramHashed(CompleteOperation *ops, long op_count, long *count, long max_count) {
    // Check if n-gram already exists
    NGramHashNode *existing = findNGram(ops, op_count);
    if (existing != NULL) {
        existing->ngram.frequency++;
        return;
    }

    // Check if we've hit the maximum count
    if (*count >= max_count) {
        return;  // Skip adding new n-grams if at capacity
    }

    // Create new n-gram node
    NGramHashNode *new_node = malloc(sizeof(NGramHashNode));
    if (new_node == NULL) {
        fprintf(stderr, "Failed to allocate memory for n-gram hash node\n");
        return;
    }

    // Initialize the n-gram
    new_node->ngram.op_count = op_count;
    for (long i = 0; i < op_count; i++) {
        new_node->ngram.ops[i] = ops[i];
    }
    new_node->ngram.frequency = 1;

    // Select appropriate hash table and insert
    NGramHashNode **hash_table;
    int hash_size;

    switch (op_count) {
        case 1:
            hash_table = unigram_hash_table;
            hash_size = UNIGRAM_HASH_SIZE;
            break;
        case 2:
            hash_table = bigram_hash_table;
            hash_size = BIGRAM_HASH_SIZE;
            break;
        case 3:
            hash_table = trigram_hash_table;
            hash_size = TRIGRAM_HASH_SIZE;
            break;
        default:
            free(new_node);
            return; // Unsupported n-gram size
    }

    unsigned int index = hashNGram(ops, op_count, hash_size);
    new_node->next = hash_table[index];
    hash_table[index] = new_node;

    (*count)++;
}

void addUnigramHashed(CompleteOperation *op) {
    addOperationNGramHashed(op, 1, &unigram_count, MAX_OPERATIONS);
}

void addBigramHashed(CompleteOperation *ops) {
    addOperationNGramHashed(ops, 2, &bigram_count, MAX_OPERATIONS);
}

void addTrigramHashed(CompleteOperation *ops) {
    addOperationNGramHashed(ops, 3, &trigram_count, MAX_OPERATIONS);
}

int ruleExists(char *rule_string) {
    unsigned int index = hash(rule_string);
    HashNode *node = hash_table[index];

    while (node != NULL) {
        if (strcmp(node->rule_string, rule_string) == 0) {
            return 1;
        }
        node = node->next;
    }
    return 0;
}

void addRuleToHash(char *rule_string) {
    if (ruleExists(rule_string)) return;

    unsigned int index = hash(rule_string);
    HashNode *new_node = malloc(sizeof(HashNode));
    strcpy(new_node->rule_string, rule_string);
    new_node->next = hash_table[index];
    hash_table[index] = new_node;
}

//
