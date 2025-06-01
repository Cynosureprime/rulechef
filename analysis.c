#define _FILE_OFFSET_BITS 64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE

#include "analysis.h"
#include "rule_parser.h"
#include "hash_tables.h"
#include <stdio.h>

// Global counters
long unigram_count = 0;
long bigram_count = 0;
long trigram_count = 0;
long transition_count = 0;

void analyseRuleStream(FILE *file, int verbose) {
    char line[MAX_RULE_LEN];
    long rule_count = 0;

    if (verbose) {
        fprintf(stderr, "Starting analysis...\n");
    }

    //Get filesize
    fseeko(file, 0, SEEK_END);
    size_t f_size = ftello(file);
    fseeko(file, 0, SEEK_SET);
    size_t f_readbytes = 0;

    while (fgets(line, sizeof(line), file)) {

        f_readbytes += strlen(line);
        char *p = line + strlen(line) - 1;
        if (*p == '\n') *p = '\0';
        if ((p != line) && (*--p == '\r')) *p = '\0';

        if (strlen(line) == 0) continue;

        // Validate and normalize rule
        //if (packrules(line)) {
        if (!validateRule(line)) {
            if (verbose) {
                fprintf(stderr, "Invalid rule skipped: %s\n", line);
            }
            continue;
        }

        // Parse rule into operations
        ParsedRule parsed;
        if (!parseRuleIntoOperations(line, &parsed)) {
            if (verbose) {
                fprintf(stderr, "Failed to parse rule: %s\n", line);
            }
            continue;
        }

        rule_count++;

        if (verbose && rule_count % 10000 == 0) {
            fprintf(stderr, "Processed %ld rules...\n", rule_count);
        }

        // Incrementally build statistics using hash tables
        for (int j = 0; j < parsed.op_count; j++) {
            addUnigramHashed(&parsed.operations[j]);
        }

        for (int j = 0; j < parsed.op_count - 1; j++) {
            CompleteOperation bigram_ops[2] = {parsed.operations[j], parsed.operations[j+1]};
            addBigramHashed(bigram_ops);
            addOperationTransitionHashed(&parsed.operations[j], &parsed.operations[j+1]);
        }


        for (int j = 0; j < parsed.op_count - 2; j++) {
            CompleteOperation trigram_ops[3] = {
                parsed.operations[j],
                parsed.operations[j+1],
                parsed.operations[j+2]
            };
            addTrigramHashed(trigram_ops);
        }


        if (rule_count % 50000 == 0) {
            calculateTransitionProbabilitiesDirectly();
            if (verbose) {
                double progress = ((double)f_readbytes/f_size)*100;
                fprintf(stderr, "Current stats: %ld unigrams, %ld bigrams, %ld trigrams, %ld transitions, %.2f%%\n",
                        unigram_count, bigram_count, trigram_count, transition_count,progress);
            }
        }
    }

    calculateTransitionProbabilitiesDirectly();
    if (verbose) {
        fprintf(stderr, "Analysis complete. Processed %ld rules\n", rule_count);
        fprintf(stderr, "Final stats: %ld unigrams, %ld bigrams, %ld trigrams, %ld transitions\n",
                unigram_count, bigram_count, trigram_count, transition_count);
    }
}


// Print hash table statistics for all n-gram types
static void printHashTableStatsForType(NGramHashNode **hash_table, int hash_size, const char *type) {
    int used_buckets = 0;
    int max_chain_length = 0;
    int total_nodes = 0;

    for (int i = 0; i < hash_size; i++) {
        if (hash_table[i] != NULL) {
            used_buckets++;
            int chain_length = 0;
            NGramHashNode *node = hash_table[i];
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

    fprintf(stderr, "%s hash table stats:\n", type);
    fprintf(stderr, "  Total %ss: %d\n", type, total_nodes);
    fprintf(stderr, "  Used buckets: %d/%d (%.2f%%)\n",
            used_buckets, hash_size,
            100.0 * used_buckets / hash_size);
    fprintf(stderr, "  Max chain length: %d\n", max_chain_length);
    fprintf(stderr, "  Average chain length: %.2f\n",
            used_buckets > 0 ? (double)total_nodes / used_buckets : 0.0);
    fprintf(stderr, "\n");
}

void printAllNGramHashTableStats() {
    printHashTableStatsForType(unigram_hash_table, UNIGRAM_HASH_SIZE, "Unigram");
    printHashTableStatsForType(bigram_hash_table, BIGRAM_HASH_SIZE, "Bigram");
    printHashTableStatsForType(trigram_hash_table, TRIGRAM_HASH_SIZE, "Trigram");
}


void printTopNGramsFromHashTable() {
    fprintf(stderr, "\n=== Rule Analysis Statistics (from Hash Tables) ===\n");

    // Print top unigrams
    int unigram_total = 0;
    OperationNGram *extracted_unigrams = extractNGramsFromHashTable(
        unigram_hash_table, UNIGRAM_HASH_SIZE, 1, &unigram_total);

    if (extracted_unigrams != NULL && unigram_total > 0) {
        qsort(extracted_unigrams, unigram_total, sizeof(OperationNGram), compareNGramsByFrequency);

        fprintf(stderr, "\nTop Complete Operations (Unigrams):\n");
        int show_count = unigram_total < 20 ? unigram_total : 20;
        for (int i = 0; i < show_count; i++) {
            fprintf(stderr, "'%s': %d occurrences\n",
                   extracted_unigrams[i].ops[0].full_op, extracted_unigrams[i].frequency);
        }
        free(extracted_unigrams);
    }

    // Print top bigrams
    int bigram_total = 0;
    OperationNGram *extracted_bigrams = extractNGramsFromHashTable(
        bigram_hash_table, BIGRAM_HASH_SIZE, 2, &bigram_total);

    if (extracted_bigrams != NULL && bigram_total > 0) {
        qsort(extracted_bigrams, bigram_total, sizeof(OperationNGram), compareNGramsByFrequency);

        fprintf(stderr, "\nTop Operation Pairs (Bigrams):\n");
        int show_count = bigram_total < 15 ? bigram_total : 15;
        for (int i = 0; i < show_count; i++) {
            fprintf(stderr, "'%s' -> '%s': %d occurrences\n",
                   extracted_bigrams[i].ops[0].full_op, extracted_bigrams[i].ops[1].full_op,
                   extracted_bigrams[i].frequency);
        }
        free(extracted_bigrams);
    }

    // Print top trigrams
    int trigram_total = 0;
    OperationNGram *extracted_trigrams = extractNGramsFromHashTable(
        trigram_hash_table, TRIGRAM_HASH_SIZE, 3, &trigram_total);

    if (extracted_trigrams != NULL && trigram_total > 0) {
        qsort(extracted_trigrams, trigram_total, sizeof(OperationNGram), compareNGramsByFrequency);

        fprintf(stderr, "\nTop Operation Triplets (Trigrams):\n");
        int show_count = trigram_total < 10 ? trigram_total : 10;
        for (int i = 0; i < show_count; i++) {
            fprintf(stderr, "'%s' -> '%s' -> '%s': %d occurrences\n",
                   extracted_trigrams[i].ops[0].full_op, extracted_trigrams[i].ops[1].full_op,
                   extracted_trigrams[i].ops[2].full_op, extracted_trigrams[i].frequency);
        }
        free(extracted_trigrams);
    }
}


OperationNGram* extractNGramsFromHashTable(NGramHashNode **hash_table, int hash_size, int ngram_type, int *total_count) {
    // First pass: count total n-grams
    *total_count = 0;
    for (int i = 0; i < hash_size; i++) {
        NGramHashNode *node = hash_table[i];
        while (node != NULL) {
            if (node->ngram.op_count == ngram_type) {
                (*total_count)++;
            }
            node = node->next;
        }
    }

    if (*total_count == 0) return NULL;

    // Allocate array for n-grams
    OperationNGram *ngrams = malloc(*total_count * sizeof(OperationNGram));
    if (ngrams == NULL) {
        fprintf(stderr, "Failed to allocate memory for n-gram extraction\n");
        *total_count = 0;
        return NULL;
    }

    // Second pass: copy n-grams to array
    int index = 0;
    for (int i = 0; i < hash_size; i++) {
        NGramHashNode *node = hash_table[i];
        while (node != NULL && index < *total_count) {
            if (node->ngram.op_count == ngram_type) {
                ngrams[index] = node->ngram;
                index++;
            }
            node = node->next;
        }
    }

    return ngrams;
}

void calculateTransitionProbabilities() {
    // Use the hash table version instead
    calculateTransitionProbabilitiesDirectly();
}

