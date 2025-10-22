
#include "priority_queue.h"
#include <stdio.h>
#include <stdlib.h>

// Helper functions for heap operations
static int getParent(int index) {
    return (index - 1) / 2;
}

static int getLeftChild(int index) {
    return 2 * index + 1;
}

static int getRightChild(int index) {
    return 2 * index + 2;
}

static int hasParent(int index) {
    return index > 0;
}

static int hasLeftChild(PriorityQueue *pq, int index) {
    return getLeftChild(index) < pq->size;
}

static int hasRightChild(PriorityQueue *pq, int index) {
    return getRightChild(index) < pq->size;
}

// Swap two nodes in the heap
static void swap(PriorityQueue *pq, int i, int j) {
    PQNode temp = pq->heap[i];
    pq->heap[i] = pq->heap[j];
    pq->heap[j] = temp;
}

// Bubble up (heapify up) - for max-heap, higher priority goes up
static void bubbleUp(PriorityQueue *pq, int index) {
    if (!hasParent(index)) return;

    int parent = getParent(index);

    // Max-heap: if current node has higher priority than parent, swap up
    if (pq->heap[index].priority > pq->heap[parent].priority) {
        swap(pq, index, parent);
        bubbleUp(pq, parent);
    }
}

// Bubble down (heapify down) - maintain max-heap property
static void bubbleDown(PriorityQueue *pq, int index) {
    int largest = index;
    int left = getLeftChild(index);
    int right = getRightChild(index);

    // Find the largest among current, left child, and right child
    if (hasLeftChild(pq, index) &&
        pq->heap[left].priority > pq->heap[largest].priority) {
        largest = left;
    }

    if (hasRightChild(pq, index) &&
        pq->heap[right].priority > pq->heap[largest].priority) {
        largest = right;
    }

    // If largest is not the current index, swap and continue
    if (largest != index) {
        swap(pq, index, largest);
        bubbleDown(pq, largest);
    }
}

// Create and initialize Priority Queue
PriorityQueue* createPriorityQueue(int initial_capacity, int max_size_limit) {
    if (initial_capacity <= 0) {
        initial_capacity = PQ_DEFAULT_CAPACITY;
    }

    PriorityQueue *pq = malloc(sizeof(PriorityQueue));
    if (!pq) return NULL;

    pq->heap = malloc(initial_capacity * sizeof(PQNode));
    if (!pq->heap) {
        free(pq);
        return NULL;
    }

    pq->size = 0;
    pq->capacity = initial_capacity;
    pq->max_size_limit = max_size_limit;

    return pq;
}

// Free Priority Queue and all its contents
void freePriorityQueue(PriorityQueue *pq) {
    if (!pq) return;

    // Free remaining candidates
    for (int i = 0; i < pq->size; i++) {
        if (pq->heap[i].candidate) {
            free(pq->heap[i].candidate);
        }
    }

    free(pq->heap);
    free(pq);
}

// Push candidate into priority queue
int pushPriorityQueue(PriorityQueue *pq, RuleCandidate *candidate) {
    if (!pq || !candidate) return 0;

    // Check if we need to enforce size limit
    if (pq->size >= pq->max_size_limit && pq->max_size_limit > 0) {
        // If queue is full and new candidate has lower priority than lowest, reject
        if (pq->size > 0) {
            // Find the minimum priority in the heap (worst candidate)
            double min_priority = pq->heap[0].priority;
            int min_index = 0;

            // In a max-heap, minimum is not guaranteed to be at a specific position
            // but it's guaranteed to be in the bottom half of the heap
            int start_search = pq->size / 2;
            for (int i = start_search; i < pq->size; i++) {
                if (pq->heap[i].priority < min_priority) {
                    min_priority = pq->heap[i].priority;
                    min_index = i;
                }
            }

            if (candidate->probability > min_priority) {
                // Remove the worst candidate and insert the new one
                if (pq->heap[min_index].candidate) {
                    free(pq->heap[min_index].candidate);
                }
                // Replace the worst with new candidate
                pq->heap[min_index].candidate = candidate;
                pq->heap[min_index].priority = candidate->probability;

                // Restore heap property (could go up or down)
                bubbleUp(pq, min_index);
                bubbleDown(pq, min_index);
                return 1;
            } else {
                // New candidate is worse than worst in queue, reject it
                return 0;
            }
        }
    }

    // Resize if needed (and no size limit or under limit)
    if (pq->size >= pq->capacity) {
        int new_capacity = pq->capacity * PQ_GROWTH_FACTOR;
        PQNode *new_heap = realloc(pq->heap, new_capacity * sizeof(PQNode));
        if (!new_heap) return 0;

        pq->heap = new_heap;
        pq->capacity = new_capacity;
    }

    // Add new node at the end and bubble up
    pq->heap[pq->size].candidate = candidate;
    pq->heap[pq->size].priority = candidate->probability;

    bubbleUp(pq, pq->size);
    pq->size++;

    return 1;
}

// Pop the highest priority candidate
RuleCandidate* popPriorityQueue(PriorityQueue *pq) {
    if (!pq || pq->size == 0) return NULL;

    // Save the root (highest priority)
    RuleCandidate *best_candidate = pq->heap[0].candidate;

    // Move last element to root and decrease size
    pq->size--;
    if (pq->size > 0) {
        pq->heap[0] = pq->heap[pq->size];
        bubbleDown(pq, 0);
    }

    return best_candidate;
}

// Peek at the highest priority candidate without removing it
RuleCandidate* peekPriorityQueue(PriorityQueue *pq) {
    if (!pq || pq->size == 0) return NULL;
    return pq->heap[0].candidate;
}

// Check if priority queue is empty
int isPriorityQueueEmpty(PriorityQueue *pq) {
    return !pq || pq->size == 0;
}

// Get current size of priority queue
int getPriorityQueueSize(PriorityQueue *pq) {
    return pq ? pq->size : 0;
}

// Print priority queue statistics
void printPriorityQueueStats(PriorityQueue *pq) {
    if (!pq) return;

    fprintf(stderr, "PriorityQueue Stats:\n");
    fprintf(stderr, "  Size: %d/%d\n", pq->size, pq->capacity);
    fprintf(stderr, "  Size Limit: %d\n", pq->max_size_limit);

    if (pq->size > 0) {
        fprintf(stderr, "  Highest Priority (BEST): %.6f\n", pq->heap[0].priority);

        // Find minimum priority for comparison
        if (pq->size > 1) {
            double min_priority = pq->heap[0].priority;
            for (int i = pq->size / 2; i < pq->size; i++) {
                if (pq->heap[i].priority < min_priority) {
                    min_priority = pq->heap[i].priority;
                }
            }
            fprintf(stderr, "  Lowest Priority (WORST): %.6f\n", min_priority);
        }
    }
}
