#pragma once
#include "defines.h"
#include <stdatomic.h>

typedef struct MpmcQueue {
    uint32 size;   // power of 2
    uint32 mask;

    uint32 stride;

    _Atomic uint32 head;
    _Atomic uint32 tail;

    uint8* data;          // raw storage
    _Atomic uint32* seq;  // per-slot sequence array
} MpmcQueue;

#define MPMC_QUEUE_SEQUENCE_BUFFER_SIZE(elementCount) sizeof(uint32)*(elementCount) // elementCount must be power of 2

bool32 mpmcInit(MpmcQueue* q, uint32 size, uint32 stride, void* backingBuffer, void* seqBuffer);
bool32 mpmcPush(MpmcQueue* q, void* element);
bool32 mpmcPop(MpmcQueue* q, void* outElement);