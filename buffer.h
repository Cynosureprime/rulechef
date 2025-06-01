#ifndef BUFFER_H
#define BUFFER_H

#include "types.h"

// Buffer management functions
void init_buffer(WBuffer *WStruct);
void free_buffer(WBuffer *WStruct);
void buffer_string2(WBuffer *WStruct, char *string, size_t max);
void flush_buffer(WBuffer *WStruct);

// Utility functions
size_t mystrlen2(const char *string, size_t max);

#endif
