#include "hashmap.h"
#include "types.h"

static uint32_t hashmap_hash(const char *key) {
    uint32_t hash = 2166136261u;
    while (*key) {
        hash ^= (uint32_t)*key++;
        hash *= 16777619u;
    }
    return hash;
}

HashMap* hashmap_create(void) {
    return hashmap_create_with_size(HASHMAP_INITIAL_SIZE);
}

HashMap* hashmap_create_with_size(size_t initial_size) {
    HashMap *map = malloc(sizeof(HashMap));
    if (!map) return NULL;

    map->buckets = calloc(initial_size, sizeof(HashMapEntry*));
    if (!map->buckets) {
        free(map);
        return NULL;
    }

    map->bucket_count = initial_size;
    map->size = 0;
    map->threshold = (size_t)(initial_size * HASHMAP_LOAD_FACTOR);

    return map;
}

void hashmap_free(HashMap *map) {
    if (!map) return;

    hashmap_clear(map);
    free(map->buckets);
    free(map);
}

void hashmap_clear(HashMap *map) {
    if (!map) return;

    for (size_t i = 0; i < map->bucket_count; i++) {
        HashMapEntry *entry = map->buckets[i];
        while (entry) {
            HashMapEntry *next = entry->next;
            free(entry->key);
            free(entry->value);
            free(entry);
            entry = next;
        }
        map->buckets[i] = NULL;
    }
    map->size = 0;
}

static void hashmap_resize(HashMap *map) {
    size_t old_bucket_count = map->bucket_count;
    HashMapEntry **old_buckets = map->buckets;

    // Double the size
    map->bucket_count *= 2;
    map->threshold = (size_t)(map->bucket_count * HASHMAP_LOAD_FACTOR);
    map->buckets = calloc(map->bucket_count, sizeof(HashMapEntry*));
    if (!map->buckets) {
        // Restore old state on failure
        map->bucket_count = old_bucket_count;
        map->threshold = (size_t)(old_bucket_count * HASHMAP_LOAD_FACTOR);
        map->buckets = old_buckets;
        return;
    }


    map->size = 0;

    // Rehash all entries
    for (size_t i = 0; i < old_bucket_count; i++) {
        HashMapEntry *entry = old_buckets[i];
        while (entry) {
            HashMapEntry *next = entry->next;

            // Rehash entry
            uint32_t hash = hashmap_hash(entry->key);
            size_t bucket = hash % map->bucket_count;

            entry->next = map->buckets[bucket];
            map->buckets[bucket] = entry;
            map->size++;

            entry = next;
        }
    }

    free(old_buckets);
}

int hashmap_put(HashMap *map, const char *key, const void *value, size_t value_size) {
    if (!map || !key) return 0;

    uint32_t hash = hashmap_hash(key);
    size_t bucket = hash % map->bucket_count;

    // Check if key already exists
    HashMapEntry *entry = map->buckets[bucket];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            // Update existing entry
            void *new_value = malloc(value_size);
            if (!new_value) return 0;

            memcpy(new_value, value, value_size);
            free(entry->value);
            entry->value = new_value;
            entry->value_size = value_size;
            return 1;
        }
        entry = entry->next;
    }

    // Create new entry
    entry = malloc(sizeof(HashMapEntry));
    if (!entry) return 0;

    entry->key = malloc(strlen(key) + 1);
    if (!entry->key) {
        free(entry);
        return 0;
    }
    strcpy(entry->key, key);

    entry->value = malloc(value_size);
    if (!entry->value) {
        free(entry->key);
        free(entry);
        return 0;
    }
    memcpy(entry->value, value, value_size);
    entry->value_size = value_size;

    // Insert at beginning of bucket
    entry->next = map->buckets[bucket];
    map->buckets[bucket] = entry;
    map->size++;

    // Resize if load factor exceeded
    if (map->size >= map->threshold) {
        hashmap_resize(map);
    }

    return 1;
}

void* hashmap_get(HashMap *map, const char *key) {
    if (!map || !key) return NULL;

    uint32_t hash = hashmap_hash(key);
    size_t bucket = hash % map->bucket_count;

    HashMapEntry *entry = map->buckets[bucket];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            return entry->value;
        }
        entry = entry->next;
    }

    return NULL;
}

int hashmap_contains(HashMap *map, const char *key) {
    return hashmap_get(map, key) != NULL;
}

int hashmap_remove(HashMap *map, const char *key) {
    if (!map || !key) return 0;

    uint32_t hash = hashmap_hash(key);
    size_t bucket = hash % map->bucket_count;

    HashMapEntry *entry = map->buckets[bucket];
    HashMapEntry *prev = NULL;

    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            if (prev) {
                prev->next = entry->next;
            } else {
                map->buckets[bucket] = entry->next;
            }

            free(entry->key);
            free(entry->value);
            free(entry);
            map->size--;
            return 1;
        }
        prev = entry;
        entry = entry->next;
    }

    return 0;
}

size_t hashmap_size(HashMap *map) {
    return map ? map->size : 0;
}

// Iterator implementation
HashMapIterator* hashmap_iterator(HashMap *map) {
    if (!map) return NULL;

    HashMapIterator *iter = malloc(sizeof(HashMapIterator));
    if (!iter) return NULL;

    iter->map = map;
    iter->bucket_index = 0;
    iter->current_entry = NULL;

    // Find first non-empty bucket
    while (iter->bucket_index < map->bucket_count &&
           map->buckets[iter->bucket_index] == NULL) {
        iter->bucket_index++;
    }

    if (iter->bucket_index < map->bucket_count) {
        iter->current_entry = map->buckets[iter->bucket_index];
    }

    return iter;
}

int hashmap_iter_has_next(HashMapIterator *iter) {
    return iter && iter->current_entry != NULL;
}

HashMapEntry* hashmap_iter_next(HashMapIterator *iter) {
    if (!iter || !iter->current_entry) return NULL;

    HashMapEntry *result = iter->current_entry;

    // Move to next entry
    iter->current_entry = iter->current_entry->next;

    // If no more entries in current bucket, find next non-empty bucket
    if (!iter->current_entry) {
        iter->bucket_index++;
        while (iter->bucket_index < iter->map->bucket_count &&
               iter->map->buckets[iter->bucket_index] == NULL) {
            iter->bucket_index++;
        }

        if (iter->bucket_index < iter->map->bucket_count) {
            iter->current_entry = iter->map->buckets[iter->bucket_index];
        }
    }

    return result;
}

void hashmap_iterator_free(HashMapIterator *iter) {
    if (iter) {
        free(iter);
    }
}

// Utility functions
void hashmap_print_stats(HashMap *map) {
    if (!map) return;

    printf("HashMap Stats:\n");
    printf("  Size: %zu\n", map->size);
    printf("  Bucket count: %zu\n", map->bucket_count);
    printf("  Load factor: %.2f\n", (double)map->size / map->bucket_count);

    // Calculate distribution stats
    size_t empty_buckets = 0;
    size_t max_chain_length = 0;
    size_t total_chain_length = 0;

    for (size_t i = 0; i < map->bucket_count; i++) {
        if (map->buckets[i] == NULL) {
            empty_buckets++;
        } else {
            size_t chain_length = 0;
            HashMapEntry *entry = map->buckets[i];
            while (entry) {
                chain_length++;
                entry = entry->next;
            }
            total_chain_length += chain_length;
            if (chain_length > max_chain_length) {
                max_chain_length = chain_length;
            }
        }
    }

    printf("  Empty buckets: %zu (%.1f%%)\n", empty_buckets,
           100.0 * empty_buckets / map->bucket_count);
    printf("  Max chain length: %zu\n", max_chain_length);
    printf("  Avg chain length: %.2f\n",
           map->size > 0 ? (double)total_chain_length / (map->bucket_count - empty_buckets) : 0.0);
}



// Helper function to build context key for hashing
void buildContextKey(CompleteOperation *context, int context_len, char *key_buffer) {
    key_buffer[0] = '\0';

    for (int i = 0; i < context_len; i++) {
        if (i > 0) strcat(key_buffer, "|");
        strcat(key_buffer, context[i].full_op);
    }
}

// Specialized context hashmap functions
int hashmap_put_context(HashMap *map, CompleteOperation *context, int context_len) {
    char key[MAX_RULE_LEN];
    buildContextKey(context, context_len, key);

    ContextData data;
    for (int i = 0; i < context_len; i++) {
        data.context[i] = context[i];
    }
    data.context_len = context_len;

    return hashmap_put(map, key, &data, sizeof(ContextData));
}

ContextData* hashmap_get_context(HashMap *map, CompleteOperation *context, int context_len) {
    char key[MAX_RULE_LEN];
    buildContextKey(context, context_len, key);

    return (ContextData*)hashmap_get(map, key);
}


void initTempList(TempTransitionList *list) {
    list->capacity = 64;
    list->ops = malloc(list->capacity * sizeof(CompleteOperation));
    list->probs = malloc(list->capacity * sizeof(double));
    list->count = 0;
}

void addToTempList(TempTransitionList *list, CompleteOperation op, double prob) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->ops = realloc(list->ops, list->capacity * sizeof(CompleteOperation));
        list->probs = realloc(list->probs, list->capacity * sizeof(double));
    }

    list->ops[list->count] = op;
    list->probs[list->count] = prob;
    list->count++;
}

void freeTempList(TempTransitionList *list) {
    if (list->ops) free(list->ops);
    if (list->probs) free(list->probs);
    list->ops = NULL;
    list->probs = NULL;
    list->count = 0;
    list->capacity = 0;
}

// Sorting function for transitions by probability (descending)
static int compareTransitionsByProb(const void *a, const void *b) {
    const double *prob_a = (const double*)a;
    const double *prob_b = (const double*)b;

    if (*prob_b > *prob_a) return 1;
    if (*prob_b < *prob_a) return -1;
    return 0;
}

void qsortTransitions(CompleteOperation *ops, double *probs, int count) {
    // Create array of indices for indirect sorting
    typedef struct {
        int index;
        double prob;
    } IndexProb;

    IndexProb *pairs = malloc(count * sizeof(IndexProb));
    for (int i = 0; i < count; i++) {
        pairs[i].index = i;
        pairs[i].prob = probs[i];
    }

    // Sort by probability descending
    qsort(pairs, count, sizeof(IndexProb), compareTransitionsByProb);

    // Rearrange original arrays based on sorted indices
    CompleteOperation *temp_ops = malloc(count * sizeof(CompleteOperation));
    double *temp_probs = malloc(count * sizeof(double));

    for (int i = 0; i < count; i++) {
        temp_ops[i] = ops[pairs[i].index];
        temp_probs[i] = probs[pairs[i].index];
    }

    // Copy back
    memcpy(ops, temp_ops, count * sizeof(CompleteOperation));
    memcpy(probs, temp_probs, count * sizeof(double));

    free(pairs);
    free(temp_ops);
    free(temp_probs);
}
