#ifndef RULE_PARSER_H
#define RULE_PARSER_H

#include "types.h"


// Rule operation character arrays
extern char singleR[];
extern char DoubleR[];
extern char TripleR[];
extern char QuadR[];

// Rule operation map
extern int RuleOPs[256];

// Rule parsing and validation functions
void initRuleMaps();
int validateRule(char *rule);
int parseRuleIntoOperations(char *rule, ParsedRule *parsed);
int compareCompleteOps(const CompleteOperation *op1, const CompleteOperation *op2);
int packrules(char *line);
#endif
