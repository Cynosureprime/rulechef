#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <getopt.h>
#include "processor.h"
#include "types.h"
#include "buffer.h"
#include "rule_parser.h"
#include "hash_tables.h"
#include "analysis.h"


extern long unigram_count;
extern long transition_count;
extern long bigram_count;
extern long trigram_count;

void show_help(const char *program_name) {
    fprintf(stderr, "%s by CynosurePrime (CsP)\n\n", program_name);
    fprintf(stderr, "Usage: %s <rulefile1> [rulefile2] [options]\n", program_name);
    fprintf(stderr, "Analyses rules and cooks up all possible combinations using markov chains\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "\t-m N, --min-length N       Minimum rule length (operations) (default: 1)\n");
    fprintf(stderr, "\t-M N, --max-length N       Maximum rule length (operations) (default: 6)\n");
    fprintf(stderr, "\t-l N, --limit N            Limit starting chain to TopN (can be used with -p)\n");
    fprintf(stderr, "\t-p X, --probability X      Minimum probability threshold (0.0-1.0) (default: 0.0)\n");
    fprintf(stderr, "\t-v, --verbose              Verbose mode (show analysis and statistics)\n");
    fprintf(stderr, "\t-h, --help                 Show this help message\n\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "\t%s rules.txt --max-length 4\n", program_name);
    fprintf(stderr, "\t%s rules.txt -m 2 -M 5 -p 0.01\n", program_name);
    fprintf(stderr, "\t%s rules.txt -M 3 -p 0.5 -v\n", program_name);
    fprintf(stderr, "\t%s rules.txt -M 5 -l 200 -v\n", program_name);
}

// Global buffer for output
WBuffer output_buffer;
int main(int argc, char *argv[]) {
    if (argc < 2) {
        show_help(argv[0]);
        return 1;
    }

    // Initialize default values
    int max_length = 6;
    int min_length = 1;
    double min_probability = 0.0;
    int verbose = 0;
    int limit_unigrams = 0;


    int c;
    opterr = 0;

    while (1) {
        static struct option long_options[] = {
            {"min-length", required_argument, 0, 'm'},
            {"max-length", required_argument, 0, 'M'},
            {"limit", required_argument, 0, 'l'},
            {"probability", required_argument, 0, 'p'},
            {"verbose", no_argument, 0, 'v'},
            {"help", no_argument, 0, 'h'},
            {0, 0, 0, 0}
        };
        int option_index = 0;

        c = getopt_long(argc, argv, "m:M:l:p:vh", long_options, &option_index);

        if (c == -1)
            break;

        switch (c) {
        case 0:
            if (long_options[option_index].flag != 0)
                break;
            printf("option %s", long_options[option_index].name);
            if (optarg)
                printf(" with arg %s", optarg);
            printf("\n");
            break;
        case 'm':
            min_length = atoi(optarg);
            if (min_length <= 0 || min_length > 10) {
                fprintf(stderr, "Min length must be between 1 and 10\n");
                return 1;
            }
            break;
        case 'M':
            max_length = atoi(optarg);
            if (max_length <= 0 || max_length > 16) {
                fprintf(stderr, "Max length must be between 1 and 16\n");
                return 1;
            }
            break;
        case 'l':
            limit_unigrams = atoi(optarg);
            if (limit_unigrams < 0 || limit_unigrams > 65535) {
                fprintf(stderr, "Limit to top N chains cannot be negative or greater than 65535\n");
                return 1;
            }
            break;
        case 'p':
            min_probability = atof(optarg);
            if (min_probability < 0.0 || min_probability > 1.0) {
                fprintf(stderr, "Probability must be between 0.0 and 1.0\n");
                return 1;
            }
            break;
        case 'v':
            verbose = 1;
            break;
        case 'h':
            show_help(argv[0]);
            return 1;
        case '?':
            fprintf(stderr, "Unknown argument -%c\n", optopt);
            return 1;
        default:
            fprintf(stderr, "Unknown option\n");
            return 1;
        }
    }


    if (optind >= argc) {
        fprintf(stderr, "Error: No rulefile specified\n");
        return 1;
    }

    if (min_length > max_length) {
        fprintf(stderr, "Error: Min length (%d) cannot be greater than max length (%d)\n",
                min_length, max_length);
        return 1;
    }

    if (verbose) {
        printf("Configuration:\n");
        printf("  Min length: %d\n", min_length);
        printf("  Max length: %d\n", max_length);
        printf("  Min probability: %.3f\n", min_probability);
        printf("  Limit unigrams: %d\n", limit_unigrams);
        printf("  Verbose: %s\n", verbose ? "enabled" : "disabled");
        printf("\n");
    }

    // Initialize
    init_buffer(&output_buffer);
    initRuleMaps();
    initHashTables();


    if (verbose) {
        fprintf(stderr,"Rulefiles: ");
        for (int i = optind; i < argc; i++) {
            fprintf(stderr,"%s%s", argv[i], (i < argc - 1) ? ", " : "\n");
        }
        fprintf(stderr, "Rule length range: %d-%d operations\n", min_length, max_length);
        fprintf(stderr, "Minimum probability threshold: %.3f\n", min_probability);
        fprintf(stderr, "Limit chain start TopN: %d\n", limit_unigrams);
        fprintf(stderr, "Output buffer size: %.2f MB\n", (double)WriteBufferSize / (1024 * 1024));
    }

    for (int file_idx = optind; file_idx < argc; file_idx++) {
        const char *rulefile = argv[file_idx];

        if (verbose) {
            fprintf(stderr, "Processing file %d/%d: %s\n",
                    file_idx - optind + 1, argc - optind, rulefile);
        }

        FILE *file = fopen(rulefile, "r");
        if (!file) {
            fprintf(stderr, "Error opening file: %s\n", rulefile);
            continue;
        }

        analyseRuleStream(file, verbose);
        fclose(file);

        if (verbose) {
            fprintf(stderr, "Completed analysis of: %s\n", rulefile);
            fprintf(stderr, "Current totals: %ld unigrams, %ld bigrams, %ld trigrams, %ld transitions\n",
                    unigram_count, bigram_count, trigram_count, transition_count);
        }
    }

    if (verbose) {
        fprintf(stderr, "\n=== Final Statistics (all files combined) ===\n");
        printAllNGramHashTableStats();
        printTopNGramsFromHashTable();
    }
    if (unigram_count == 0) {
        fprintf(stderr, "No valid rules found!\n");
        free_buffer(&output_buffer);
        return 1;
    }

    generateRulesFromHT(max_length, min_length, min_probability, verbose,&output_buffer,limit_unigrams);

    return 0;
}



