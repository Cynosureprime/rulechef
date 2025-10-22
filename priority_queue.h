
#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#include "types.h"

// Priority queue node structure
typedef struct PQNode {
    RuleCandidate *candidate;
    double priority;  // Higher values = higher priority (better candidates)
} PQNode;

// Priority queue structure (max-heap implementation)
typedef struct PriorityQueue {
    PQNode *heap;              // Array-based heap storage
    int size;                  // Current number of elements
    int capacity;              // Allocated capacity
    int max_size_limit;        // Maximum size to maintain (0 = unlimited)
} PriorityQueue;

// Core Priority Queue operations
PriorityQueue* createPriorityQueue(int initial_capacity, int max_size_limit);
void freePriorityQueue(PriorityQueue *pq);

// Primary operations
int pushPriorityQueue(PriorityQueue *pq, RuleCandidate *candidate);
RuleCandidate* popPriorityQueue(PriorityQueue *pq);
RuleCandidate* peekPriorityQueue(PriorityQueue *pq);

// Utility functions
int isPriorityQueueEmpty(PriorityQueue *pq);
int getPriorityQueueSize(PriorityQueue *pq);
void printPriorityQueueStats(PriorityQueue *pq);

// Constants for Priority Queue behavior
#define PQ_DEFAULT_CAPACITY 10000
#define PQ_GROWTH_FACTOR 2
#define PQ_NO_LIMIT 0

#endif
