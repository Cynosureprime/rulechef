
#include "buffer.h"
#include "rule_parser.h"
char singleR[] = "kKLR:lucCtrdf[]{}qM46Q~E0\"";
char DoubleR[] = "Tp$^DzZ@<>!/()I+-,.RYyL'";
char TripleR[] = "ios=mvSW3*xO";
char QuadR[] = "XF\\";


// Rule operation maps
int RuleOPs[256] = {0};

// Global buffer for output - declared extern in header
extern WBuffer output_buffer;

void initRuleMaps() {
    int i;

    // Initialize rule operation parameter counts
    for (i = 0; i < (int)strlen(singleR); i++) {
        RuleOPs[(int)singleR[i]] = 1;
    }
    for (i = 0; i < (int)strlen(DoubleR); i++) {
        RuleOPs[(int)DoubleR[i]] = 2;
    }
    for (i = 0; i < (int)strlen(TripleR); i++) {
        RuleOPs[(int)TripleR[i]] = 3;
    }
    for (i = 0; i < (int)strlen(QuadR); i++) {
        RuleOPs[(int)QuadR[i]] = 4;
    }
}

// Doesn't fully validate the rule, eg check whether positions are valid etc, or whether operations are valid
int validateRule(char *rule) {
    int rule_len = strlen(rule);
    char pack_rule[MAX_RULE_LEN];
    int write_pos = 0;
    int read_pos = 0;

    while (read_pos < rule_len) {
        char current_char = rule[read_pos];

        if (current_char == ' ') {
            // Skip consecutive spaces between operations
            while (read_pos < rule_len && rule[read_pos] == ' ') {
                read_pos++;
            }
            continue;
        }

        // This is the start of an operation
        char op = current_char;
        int op_length = RuleOPs[(int)op];

        if (op_length == 0) {
            return 0; // Invalid operation
        }

        // Check if we have enough characters left for this operation
        if (read_pos + op_length > rule_len) {
            return 0; // Not enough parameters
        }

        // Copy the entire operation (including any spaces that are parameters)
        for (int i = 0; i < op_length; i++) {
            if (write_pos >= MAX_RULE_LEN - 1) {
                return 0; // Rule too long
            }
            pack_rule[write_pos] = rule[read_pos];
            write_pos++;
            read_pos++;
        }
    }

    pack_rule[write_pos] = '\0';

    // Copy the normalized rule back to the original
    strcpy(rule, pack_rule);

    return 1;
}

int parseRuleIntoOperations(char *rule, ParsedRule *parsed) {
    int rule_len = strlen(rule);
    int i = 0;
    parsed->op_count = 0;
    strcpy(parsed->original_rule, rule);

    while (i < rule_len && parsed->op_count < MAX_RULE_LEN) {
        char op = rule[i];
        int op_length = RuleOPs[(int)op];

        if (op_length == 0) {
            if (output_buffer.buffer == NULL) {
                printf("Invalid operation '%c' at position %d in rule: %s\n", op, i, rule);
            } else {
                fprintf(stderr, "Invalid operation '%c' at position %d in rule: %s\n", op, i, rule);
            }
            return 0;
        }

        if (i + op_length - 1 >= rule_len) {
            if (output_buffer.buffer == NULL) {
                printf("Not enough parameters for operation '%c' in rule: %s\n", op, rule);
            } else {
                fprintf(stderr, "Not enough parameters for operation '%c' in rule: %s\n", op, rule);
            }
            return 0;
        }

        // Create complete operation
        CompleteOperation *curr_op = &parsed->operations[parsed->op_count];
        curr_op->base_op = op;
        curr_op->length = op_length;
        strncpy(curr_op->full_op, rule + i, op_length);
        curr_op->full_op[op_length] = '\0';

        parsed->op_count++;
        i += op_length;
    }

    return 1;
}

int compareCompleteOps(const CompleteOperation *op1, const CompleteOperation *op2) {
    return strcmp(op1->full_op, op2->full_op) == 0;
}



