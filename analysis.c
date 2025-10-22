// Improved analysis.c - Array-based n-gram processing

#define _FILE_OFFSET_BITS 64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE

#include "analysis.h"
#include "rule_parser.h"
#include "hash_tables.h"
#include <stdio.h>
#include "types.h"
#include <fcntl.h>

long ngram_count[MAX_NGRAM_ORDER + 1] = {0};

extern int max_operation_count[MAX_NGRAM_ORDER + 1];


static inline void extractAndAddNGrams(ParsedRule *parsed) {
    int max_possible_order = (parsed->op_count < MAX_NGRAM_ORDER) ?
                            parsed->op_count : MAX_NGRAM_ORDER;


    for (int order = MIN_NGRAM_ORDER; order <= max_possible_order; order++) {
        int max_start_pos = parsed->op_count - order;
        for (int start = 0; start <= max_start_pos; start++) {
            addOperationNGramHashed(&parsed->operations[start], order,
                                   ngram_count[order]);
        }
    }
    if (parsed->op_count > 0) {
        addStarterOperationHashed(&parsed->operations[0]);
    }

}

void analyseRuleStream(FILE *file, int verbose) {

    char line[MAX_RULE_LEN];
    long rule_count = 0;
    long invalid_rules = 0;
    long parse_failures = 0;

    fprintf(stderr, "Performing n-gram analysis (%d orders)\n", MAX_NGRAM_ORDER);

    // Get file size for progress tracking w
    fseeko(file, 0, SEEK_END);
    size_t f_size = ftello(file);
    fseeko(file, 0, SEEK_SET);
    size_t f_readbytes = 0;

    while (fgets(line, sizeof(line), file)) {
        f_readbytes += strlen(line);

        // Clean line endings
        char *p = line + strlen(line) - 1;
        if (*p == '\n') *p = '\0';
        if ((p != line) && (*--p == '\r')) *p = '\0';

        if (strlen(line) == 0) continue;

        // Validate and normalize rule
        if (!validateRule(line)) {
            invalid_rules++;
            if (verbose && invalid_rules <= 10) {
                fprintf(stderr, "Invalid rule skipped: %s\n", line);
            }
            continue;
        }

        // Parse rule into operations
        ParsedRule parsed;
        if (!parseRuleIntoOperations(line, &parsed)) {
            parse_failures++;
            if (verbose && parse_failures <= 10) {
                fprintf(stderr, "Failed to parse rule: %s\n", line);
            }
            continue;
        }

        rule_count++;
        extractAndAddNGrams(&parsed);

        if (verbose && rule_count % 50000 == 0) {

            fprintf(stderr, "  N-grams: ");
            for (int order = 1; order <= MAX_NGRAM_ORDER; order++) {
                if (ngram_count[order]  > 0) {
                    fprintf(stderr, "%d-gram=%ld ", order, ngram_count[order]);
                }
            }
            fprintf(stderr, "\n");

        }

        if (rule_count % 10000 == 0) {
                double progress = ((double)f_readbytes / f_size) * 100;
                fprintf(stderr, "\rProcessing: %.1f%% (%ld rules)   ", progress, rule_count);
                fflush(stderr);
        }
    }



    fprintf(stderr, "\nRules processed: %ld\n", rule_count);
    fprintf(stderr, "Invalid rules: %ld\n", invalid_rules);
    fprintf(stderr, "Parse failures: %ld\n", parse_failures);

    fprintf(stderr, "Calculating n-gram probabilities...\n");

    for (int order = 1; order <= MAX_NGRAM_ORDER; order++) {
        if (ngram_count[order] > 0) {
            calculateNGramProbabilitiesParallel(order);}
            if (verbose) {
                fprintf(stderr, "%d-gram probabilities: %ld n-grams processed\n",
                       order, ngram_count[order]);
            }
        }


}




