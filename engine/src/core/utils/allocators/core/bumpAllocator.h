#pragma once
#include "../../allocator.h"

#include <stdatomic.h>

typedef struct BumpAllocator {
    struct Allocator base;
    _Atomic uint64 offset;
    MemoryRegion addressSpace;
} BumpAllocator;

typedef struct ResetableBumpAllocator {
    struct Allocator base;
    _Atomic uint64 offset;
    _Atomic uint64 generation;
    MemoryRegion addressSpace;
} ResetableBumpAllocator;

bool32 bumpAllocatorCreate(BumpAllocator* allocator, MemoryRegion region, uint64 pageSize);
bool32 resetableBumpAllocatorCreate(ResetableBumpAllocator* allocator, MemoryRegion region, uint64 pageSize);


void* bumpAllocatorAllocate(uint64 size, BumpAllocator* allocator);
void* resetableBumpAllocatorAllocate(uint64 size, ResetableBumpAllocator* allocator);

void resetableBumpAllocatorReset(ResetableBumpAllocator* allocator);