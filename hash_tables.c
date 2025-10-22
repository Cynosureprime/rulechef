
#include "hash_tables.h"
#include "rule_parser.h"
#include <omp.h>
#include <pthread.h>
#include "hashmap.h"
// Global hash tables
HashNode *hash_table[HASH_SIZE] = {NULL};
NGramHashNode **ngram_hash_table[MAX_NGRAM_ORDER + 1];
int hash_table_sizes[MAX_NGRAM_ORDER + 1];

extern long ngram_count[MAX_NGRAM_ORDER + 1];


long curr_size_idx = 0;
int max_operation_count[MAX_NGRAM_ORDER+1];
long op_prime_idx[MAX_NGRAM_ORDER+1];

//Pre-computed primes which roughly double
static const long prime_sizes[] = {
    524287,
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

static int pool_initialized = 0;
static NodePool *ngram_pool = NULL;



// Contiguous block of nodes for allocation
void initNodePool() {

    if (pool_initialized) {
        return; // Already initialized
    }

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
    pool_initialized = 1;
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
    // Expand operation indices
    op_prime_idx[1] = 0;
    op_prime_idx[0] = 0;

    // Set initial sizes
    max_operation_count[0] = STARTER_HASH_SIZE;
    max_operation_count[1] = UNIGRAM_HASH_SIZE;


    // Allocate hash tables
    hash_table_sizes[0] = STARTER_HASH_SIZE;      // starter
    hash_table_sizes[1] = UNIGRAM_HASH_SIZE;      // unigram
    max_operation_count[0] = STARTER_HASH_SIZE;
    max_operation_count[1] = UNIGRAM_HASH_SIZE;

    // Allocate all hash tables
    for (int i = 0; i < MAX_NGRAM_ORDER+1; i++) {

        if (i > 1){
            op_prime_idx[i] = 0;
                max_operation_count[i] = prime_sizes[op_prime_idx[i]];
            hash_table_sizes[i] = max_operation_count[i]; // bigram+
        }

        ngram_hash_table[i] = calloc(hash_table_sizes[i], sizeof(NGramHashNode*));
        if (!ngram_hash_table[i]) {
            fprintf(stderr, "Failed to allocate hash table %d\n", i);
            exit(1);
        }
    }

    initNodePool();

}

void freeNGramHashTable(NGramHashNode **table, int size) {
    if (table) {
        for (int i = 0; i < size; i++) {
            table[i] = NULL;
        }
        free(table);
    }
}

void freeNodePool() {
    NodePool *current = ngram_pool;
    while (current != NULL) {
        NodePool *temp = current;
        current = current->next_block;
        free(temp->nodes);
        free(temp);
    }
    ngram_pool = NULL;
}
void freeHashTables() {

   for (int i = 0; i < MAX_NGRAM_ORDER+1; i++) {
        if (ngram_hash_table[i]) {
            // Free chains for non-pooled nodes (like starters)
            if (i == 0) {  // starter table uses malloc'd nodes
                for (int j = 0; j < hash_table_sizes[i]; j++) {
                    NGramHashNode *node = ngram_hash_table[i][j];
                    while (node) {
                        NGramHashNode *temp = node;
                        node = node->next;
                        free(temp);
                    }
                }
            }

            free(ngram_hash_table[i]);
            ngram_hash_table[i] = NULL;
        }
    }

    freeNodePool();
    // Reset counters

    for (int i =0; i<= MAX_NGRAM_ORDER; i++)
    {
        ngram_count[i] = 0;
    }

}


unsigned int hashNGram(CompleteOperation *ops, long op_count, int hash_size) {
    unsigned int hash = 5381;

    for (long i = 0; i < op_count; i++) {
        const char *str = ops[i].full_op;
        while (*str) {
            hash = ((hash << 5) + hash) + *str++;
        }
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

    NGramHashNode *existing = findNGram(op, 1);
    if (existing != NULL) {
        unsigned int index = hashNGram(op, 1, STARTER_HASH_SIZE);
        NGramHashNode *node = ngram_hash_table[0][index];

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
    new_node->next = ngram_hash_table[0][index];
    ngram_hash_table[0][index] = new_node;

    ngram_count[0]++;
}

typedef struct {
    CompleteOperation prefix[MAX_NGRAM_ORDER - 1];
    int prefix_len;
    long total_count;
    pthread_mutex_t mutex;
} ThreadSafePrefixTotal;

// OpenMP parallelized version
void calculateNGramProbabilitiesParallel(int ngram_order) {
    if (ngram_order < 0 || ngram_order > MAX_NGRAM_ORDER) {
        fprintf(stderr, "Error: Invalid n-gram order %d\n", ngram_order);
        return;
    }

    NGramHashNode **hash_table = ngram_hash_table[ngram_order];
    int hash_size = max_operation_count[ngram_order];

    if (!hash_table || ngram_count[ngram_order] == 0) {
        return;
    }

    int prefix_len = ngram_order - 1;

    // Use a hash map for thread-safe prefix totals
    HashMap *prefix_totals_map = hashmap_create_with_size(hash_size);
    if (!prefix_totals_map) {
        fprintf(stderr, "Error: Failed to allocate prefix totals map\n");
        return;
    }

    // PHASE 1: Parallel collection of prefix totals
    #pragma omp parallel
    {
        // Thread-local temporary storage
        HashMap *local_prefix_map = hashmap_create_with_size(1000);

        // Parallel loop over hash buckets
        #pragma omp for schedule(dynamic, 16)
        for (int i = 0; i < hash_size; i++) {
            NGramHashNode *node = hash_table[i];
            while (node != NULL) {
                if (node->ngram.op_count == ngram_order) {
                    // Build prefix key
                    char prefix_key[MAX_RULE_LEN];
                    buildContextKey(node->ngram.ops, prefix_len, prefix_key);

                    // Update local prefix total
                    long *local_total = (long*)hashmap_get(local_prefix_map, prefix_key);
                    if (local_total) {
                        *local_total += node->ngram.frequency;
                    } else {
                        long new_total = node->ngram.frequency;
                        hashmap_put(local_prefix_map, prefix_key, &new_total, sizeof(long));
                    }
                }
                node = node->next;
            }
        }

        // Merge local maps into global map (critical section)
        #pragma omp critical
        {
            HashMapIterator *iter = hashmap_iterator(local_prefix_map);
            while (hashmap_iter_has_next(iter)) {
                HashMapEntry *entry = hashmap_iter_next(iter);
                long *local_count = (long*)entry->value;

                long *global_count = (long*)hashmap_get(prefix_totals_map, entry->key);
                if (global_count) {
                    *global_count += *local_count;
                } else {
                    long new_count = *local_count;
                    hashmap_put(prefix_totals_map, entry->key, &new_count, sizeof(long));
                }
            }
            hashmap_iterator_free(iter);
        }

        hashmap_free(local_prefix_map);
    }

    // PHASE 2: Parallel probability calculation
    #pragma omp parallel for schedule(dynamic, 16)
    for (int i = 0; i < hash_size; i++) {
        NGramHashNode *node = hash_table[i];
        while (node != NULL) {
            if (node->ngram.op_count == ngram_order) {
                // Build prefix key
                char prefix_key[MAX_RULE_LEN];
                buildContextKey(node->ngram.ops, prefix_len, prefix_key);

                // Look up prefix total (read-only, no synchronization needed)
                long *prefix_total = (long*)hashmap_get(prefix_totals_map, prefix_key);
                if (prefix_total && *prefix_total > 0) {
                    node->ngram.probability = (double)node->ngram.frequency / *prefix_total;
                }
            }
            node = node->next;
        }
    }

    hashmap_free(prefix_totals_map);
}


NGramHashNode* findNGram(CompleteOperation *ops, long op_count) {
     if (op_count < 1 || op_count > MAX_NGRAM_ORDER) {
            return NULL;
        }

        NGramHashNode **hash_table = ngram_hash_table[op_count];
        int hash_size = hash_table_sizes[op_count];

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

static long resizeHT(long op_count){
    if (op_count < 2 || op_count > MAX_NGRAM_ORDER) {
        fprintf(stderr,"Error: Invalid op_count %ld for resizing\n", op_count);
        return 0;
    }

    long old_size = max_operation_count[op_count];

    if (prime_sizes[op_prime_idx[op_count]+1] == 0) {
        fprintf(stderr,"Warning: Maximum hashtable size reached for %ld-grams... Cannot resize further\n", op_count);
        return 0;
    }

    op_prime_idx[op_count]++;
    long new_size = prime_sizes[op_prime_idx[op_count]];

    NGramHashNode ** new_HashMap = calloc(new_size, sizeof(NGramHashNode*));
    if (!new_HashMap){
        fprintf(stderr,"Error: Failed to allocate memory for %ld-gram hash map\n", op_count);
        // Restore old index
        op_prime_idx[op_count]--;
        return 0;
    }

    max_operation_count[op_count] = new_size;
    hash_table_sizes[op_count] = new_size;

    // Store old hash table and assign new one
    NGramHashNode ** old_HashMap = ngram_hash_table[op_count];
    ngram_hash_table[op_count] = new_HashMap;

    // Rehash all nodes
    long rehash_count = 0;
    for (long i = 0; i < old_size; i++) {
        NGramHashNode * node = old_HashMap[i];
        while(node != NULL){
            NGramHashNode * next = node->next;
            long new_idx = hashNGram(node->ngram.ops,
                               node->ngram.op_count,
                               new_size);  // Use new_size for rehashing
            node->next = new_HashMap[new_idx];
            new_HashMap[new_idx] = node;
            rehash_count++;
            node = next;
        }
    }

    free(old_HashMap);

    fprintf(stderr,"%ld-gram hash map resized: %ld -> %ld buckets, rehashed %ld\n",
            op_count, old_size, new_size, rehash_count);



    return 1;
}

void addOperationNGramHashed(CompleteOperation *ops, long op_count, long count) {
    if (op_count < 1 || op_count > MAX_NGRAM_ORDER) {
        return; // Invalid n-gram size
    }

    // Direct array access - op_count maps directly to array index!
    NGramHashNode **hash_table = ngram_hash_table[op_count];

    // Check if n-gram already exists
    NGramHashNode *existing = findNGram(ops, op_count);
    if (existing != NULL) {
        existing->ngram.frequency++;
        return;
    }

    // Resize check for dynamic tables
    if (op_count > 1 && count >= hash_table_sizes[op_count]) {
        if (!resizeHT(op_count)) {
            fprintf(stderr, "Warning: Hash table resize failed");
        }
        else
        {
            hash_table = ngram_hash_table[op_count];

        }
    }

    // Create new node
    NGramHashNode *new_node = allocateNGramNode();
    new_node->ngram.op_count = op_count;
    for (long i = 0; i < op_count; i++) {
        new_node->ngram.ops[i] = ops[i];
    }
    new_node->ngram.frequency = 1;

    // Insert into hash table
    unsigned long index = hashNGram(ops, op_count, hash_table_sizes[op_count]);
    new_node->next = hash_table[index];
    hash_table[index] = new_node;

    ngram_count[op_count]++;
    //(count)++;
}



//
