
#include "hash_tables.h"
#include "rule_parser.h"

// Global hash tables
NGramHashNode **unigram_hash_table = NULL;
NGramHashNode **bigram_hash_table = NULL;
NGramHashNode **trigram_hash_table = NULL;
HashNode *hash_table[HASH_SIZE] = {NULL};
NGramHashNode **starter_hash_table = NULL;

static double hm_threshold = 0.90; // Resize on 90% capcacity
int curr_size_idx = 0;
int max_operation_count[4];
int op_prime_idx[4];

//Pre-computed primes which roughly double
static const int prime_sizes[] = {
    1048573,
    2097143,
    4194301,
    8388593,
    16777213,
    33554393,
    67108859,
    134217689,
    268435399,
    536870909,
    1073741827,
    0
};

static NodePool *ngram_pool = NULL;

// Global counters
extern long unigram_count;
extern long bigram_count;
extern long trigram_count;
extern long transition_count;
extern long start_counter;

// Contiguous block of nodes for allocation
void initNodePool() {
    ngram_pool = malloc(sizeof(NodePool));
    if (!ngram_pool) {
        fprintf(stderr, "Failed to allocate node pool\n");
        exit(1);
    }

    ngram_pool->nodes = malloc(POOL_BLOCK_SIZE * sizeof(NGramHashNode));
    if (!ngram_pool->nodes) {
        fprintf(stderr, "Failed to allocate node pool block\n");
        exit(1);
    }

    ngram_pool->used_in_block = 0;
    ngram_pool->block_size = POOL_BLOCK_SIZE;
    ngram_pool->next_block = NULL;
}

NGramHashNode* allocateNGramNode() {
    // Check if we need a new block
    //fprintf(stderr,"used %ld size %ld\n",ngram_pool->used_in_block,ngram_pool->block_size);
    if (ngram_pool->used_in_block >= ngram_pool->block_size) {
        // Allocate a new block

        NodePool *new_block = malloc(sizeof(NodePool));
        if (!new_block) {
            fprintf(stderr, "Failed to allocate new node pool block\n");
            return NULL;
        }

        new_block->nodes = malloc(POOL_BLOCK_SIZE * sizeof(NGramHashNode));
        if (!new_block->nodes) {
            fprintf(stderr, "Failed to allocate new node pool block memory\n");
            free(new_block);
            return NULL;
        }

        new_block->used_in_block = 0;
        new_block->block_size = POOL_BLOCK_SIZE;
        new_block->next_block = ngram_pool;
        ngram_pool = new_block;
    }

    // Return next available node
    NGramHashNode *node = &ngram_pool->nodes[ngram_pool->used_in_block];
    ngram_pool->used_in_block++;
    return node;
}

void initHashTables() {

    // Possibly use a loop for larger tables, for now it's fine.
    op_prime_idx[1] = 0;
    op_prime_idx[2] = 0;
    op_prime_idx[3] = 0;
    max_operation_count[1] = UNIGRAM_HASH_SIZE;
    max_operation_count[2] = prime_sizes[op_prime_idx[2]];
    max_operation_count[3] = prime_sizes[op_prime_idx[3]];

    unigram_hash_table = calloc(UNIGRAM_HASH_SIZE, sizeof(NGramHashNode*)); // Uneeded to be dynamic, not expected to hit cap
    bigram_hash_table = calloc(max_operation_count[2], sizeof(NGramHashNode*));
    trigram_hash_table = calloc(max_operation_count[3], sizeof(NGramHashNode*)); // Unused
    starter_hash_table = calloc(STARTER_HASH_SIZE, sizeof(NGramHashNode*)); // Uneeded to be dynamic, not expected to hit cap

    if (!unigram_hash_table || !bigram_hash_table ||
        !trigram_hash_table || !starter_hash_table) {
        fprintf(stderr, "Failed to allocate hash tables\n");
        exit(1);
    }
    initNodePool();
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


    curr_size_idx = 0;

    // Free n-gram hash tables using the helper function
    freeNGramHashTable(unigram_hash_table, UNIGRAM_HASH_SIZE);
    freeNGramHashTable(bigram_hash_table, max_operation_count[2]);
    freeNGramHashTable(trigram_hash_table, max_operation_count[3]);
    freeNGramHashTable(starter_hash_table, STARTER_HASH_SIZE);

    // Set pointers to NULL after freeing
    unigram_hash_table = NULL;
    bigram_hash_table = NULL;
    trigram_hash_table = NULL;
    starter_hash_table = NULL;

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



void addStarterOperationHashed(CompleteOperation *op) {

    NGramHashNode *existing = findNGram(op, 1); // Reuse existing function
    if (existing != NULL) {
        unsigned int index = hashNGram(op, 1, STARTER_HASH_SIZE);
        NGramHashNode *node = starter_hash_table[index];

        while (node != NULL) {
            if (node->ngram.op_count == 1 &&
                compareCompleteOps(&node->ngram.ops[0], op)) {
                node->ngram.frequency++;
                return;
            }
            node = node->next;
        }
    }

    NGramHashNode *new_node = malloc(sizeof(NGramHashNode));
    if (new_node == NULL) {
        fprintf(stderr, "Failed to allocate memory for starter hash node\n");
        return;
    }

    new_node->ngram.op_count = 1;
    new_node->ngram.ops[0] = *op;
    new_node->ngram.frequency = 1;

    unsigned int index = hashNGram(op, 1, STARTER_HASH_SIZE);
    new_node->next = starter_hash_table[index];
    starter_hash_table[index] = new_node;

    starter_count++;
}

void calculateBigramProbabilities() {
    // Count totals for each unique from_op (first operation in bigram)
    typedef struct {
        CompleteOperation from_op;
        int total_count;
    } FromOpTotal;

    FromOpTotal *from_totals = malloc(max_operation_count[2] * sizeof(FromOpTotal));
    int from_total_count = 0;

    // First pass: Calculate totals for each unique from_op
    for (int i = 0; i < max_operation_count[2]; i++) {
        NGramHashNode *node = bigram_hash_table[i];
        while (node != NULL) {
            // Only process bigrams (op_count == 2)
            if (node->ngram.op_count == 2) {
                // Find or create total for this from_op (ops[0])
                int found = 0;
                for (int j = 0; j < from_total_count; j++) {
                    if (compareCompleteOps(&from_totals[j].from_op, &node->ngram.ops[0])) {
                        from_totals[j].total_count += node->ngram.frequency;
                        found = 1;
                        break;
                    }
                }
                if (!found && from_total_count < max_operation_count[2]) {
                    from_totals[from_total_count].from_op = node->ngram.ops[0];
                    from_totals[from_total_count].total_count = node->ngram.frequency;
                    from_total_count++;
                }
            }
            node = node->next;
        }
    }

    // Second pass: Calculate probabilities for each bigram
    for (int i = 0; i < max_operation_count[2]; i++) {
        NGramHashNode *node = bigram_hash_table[i];
        while (node != NULL) {
            if (node->ngram.op_count == 2) {
                // Find the total for this from_op (ops[0])
                for (int j = 0; j < from_total_count; j++) {
                    if (compareCompleteOps(&from_totals[j].from_op, &node->ngram.ops[0])) {
                        node->ngram.probability = (double)node->ngram.frequency / from_totals[j].total_count;
                        break;
                    }
                }
            }
            node = node->next;
        }
    }

    free(from_totals);
}


NGramHashNode* findNGram(CompleteOperation *ops, long op_count) {
    NGramHashNode **hash_table;
    int hash_size;

    // Select appropriate hash table based on n-gram size
    switch (op_count) {
        case 1:
            hash_table = unigram_hash_table;
            hash_size = max_operation_count[1];
            break;
        case 2:
            hash_table = bigram_hash_table;
            hash_size = max_operation_count[2];
            break;
        case 3:
            hash_table = trigram_hash_table;
            hash_size = max_operation_count[3];
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

static int resizeHT(int op_count){
    int old_size = max_operation_count[op_count];

    if (prime_sizes[op_prime_idx[op_count]+1] == 0)
    {
        fprintf(stderr,"Warning: Maximum hashtable size reached... Exiting\n");
        return 0;
    }

    op_prime_idx[op_count]++;
    int new_size = prime_sizes[op_prime_idx[op_count]];

    // Alloc new table
    NGramHashNode ** new_HashMap = calloc(new_size, sizeof(NGramHashNode*));
    if (!new_HashMap){
        fprintf(stderr,"Error: Failed to allocate memory for hash map");
        return 0;
    }

    max_operation_count[op_count] = new_size;
    NGramHashNode ** old_HashMap;
    if (op_count == 2){
        old_HashMap = bigram_hash_table;
        bigram_hash_table = new_HashMap;
    }
    else if(op_count == 3){
         old_HashMap = trigram_hash_table;
         trigram_hash_table = new_HashMap;
    }
    else{
        fprintf(stderr,"Error, something when wrong resizing hash map with %d ops",op_count);
        free(new_HashMap);
        return 0;
    }

    //Rehash

    int rehash_count = 0;
    for (int i=0; i<old_size; i++)
    {
        NGramHashNode * node = old_HashMap[i];
        while(node != NULL){
            NGramHashNode * next = node->next;
            unsigned int new_idx = hashNGram(node->ngram.ops,
                               node->ngram.op_count,
                               new_size);
            node->next = new_HashMap[new_idx];
            new_HashMap[new_idx] = node;
            rehash_count++;
            node=next;

        }
    }

    free(old_HashMap);
    fprintf(stderr,"Hash map resized: %d -> %d buckets, rehashed %d\n",old_size,new_size,rehash_count);

    return 1;
}

void addOperationNGramHashed(CompleteOperation *ops, long op_count, long *count, long max_count) {
    // Check if n-gram already exists
    NGramHashNode *existing = findNGram(ops, op_count);
    if (existing != NULL) {
        existing->ngram.frequency++;
        return;
    }

    if (op_count > 1)
    {
        if (*count >= (max_operation_count[op_count]  * 0.8)) {
            if(!resizeHT(op_count)){
                fprintf(stderr,"Warning: Hash table resize failed");
            }
        }
    }

    NGramHashNode *new_node = allocateNGramNode(); //Request from pool of nodes
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
            hash_size = max_operation_count[op_count];
            break;
        case 3:
            hash_table = trigram_hash_table;
            hash_size = max_operation_count[op_count];
            break;
        default:
            //free(new_node);
            return; // Unsupported n-gram size
    }

    unsigned int index = hashNGram(ops, op_count, hash_size);
    new_node->next = hash_table[index];
    hash_table[index] = new_node;

    (*count)++;
}

void addUnigramHashed(CompleteOperation *op) {
    addOperationNGramHashed(op, 1, &unigram_count, max_operation_count[1]);
}

void addBigramHashed(CompleteOperation *ops) {
    addOperationNGramHashed(ops, 2, &bigram_count, max_operation_count[2]);
}

void addTrigramHashed(CompleteOperation *ops) {
    addOperationNGramHashed(ops, 3, &trigram_count, max_operation_count[3]);
}



//
