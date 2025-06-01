#include "buffer.h"

// Fast strlen implementation for bounded strings
size_t mystrlen2(const char *string, size_t max) {
    size_t len = 0;
    while (len < max && string[len] != '\0') {
        len++;
    }
    return len;
}

void buffer_string2(WBuffer *WStruct, char *string, size_t max) {
    size_t len = mystrlen2(string, max);

    if (len >= (WStruct->bufferSize - WStruct->bufferUsed)) {
        // We need to increase the buffer larger than the default size to accommodate the len
        if (len > WriteBufferSize) {
            WStruct->buffer = (char *)realloc(WStruct->buffer, WStruct->bufferSize + (len * 2) + 1);
            if (WStruct->buffer == NULL) {
                fprintf(stderr, "Unable to realloc buffer for writing exiting");
                exit(1);
            } else {
                fprintf(stderr, "Increasing write buffer\n");
                WStruct->bufferSize += (len * 2);
            }
        } else {
            WStruct->buffer = (char *)realloc(WStruct->buffer, WStruct->bufferSize + WriteBufferSize + 1);
            if (WStruct->buffer == NULL) {
                fprintf(stderr, "Unable to realloc buffer for writing exiting");
                exit(1);
            } else {
                WStruct->bufferSize += (WriteBufferSize);
            }
        }
    }

    memcpy(WStruct->buffer + WStruct->bufferUsed, string, len);
    WStruct->buffer[len + WStruct->bufferUsed] = 10; // newline
    WStruct->bufferUsed += (len + 1);
    WStruct->writeCount++;
}

// Flush buffer to stdout
void flush_buffer(WBuffer *WStruct) {
    if (WStruct->bufferUsed > 0) {
        fwrite(WStruct->buffer, 1, WStruct->bufferUsed, stdout);
        fflush(stdout);
        WStruct->bufferUsed = 0;
    }
}

// Initialize buffer
void init_buffer(WBuffer *WStruct) {
    WStruct->bufferSize = WriteBufferSize;
    WStruct->bufferUsed = 0;
    WStruct->writeCount = 0;
    WStruct->buffer = (char *)malloc(WriteBufferSize + 1);
    if (WStruct->buffer == NULL) {
        fprintf(stderr, "Unable to allocate initial write buffer\n");
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
        exit(1);
    }
}

// Free buffer
void free_buffer(WBuffer *WStruct) {
    if (WStruct->buffer != NULL) {
        free(WStruct->buffer);
        WStruct->buffer = NULL;
    }
}
