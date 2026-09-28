#pragma once
#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
typedef struct {
    uint8_t *storage;
    size_t capacity;
    size_t read_index;
    size_t write_index;
    size_t count;
} StaticStreamBuffer_t;
typedef StaticStreamBuffer_t *StreamBufferHandle_t;
StreamBufferHandle_t xStreamBufferCreateStatic(size_t bytes, size_t trigger,
                                              uint8_t *storage,
                                              StaticStreamBuffer_t *control);
size_t xStreamBufferSend(StreamBufferHandle_t stream, const void *data,
                         size_t size, TickType_t timeout);
size_t xStreamBufferReceive(StreamBufferHandle_t stream, void *data,
                            size_t size, TickType_t timeout);
size_t xStreamBufferBytesAvailable(StreamBufferHandle_t stream);
BaseType_t xStreamBufferReset(StreamBufferHandle_t stream);
