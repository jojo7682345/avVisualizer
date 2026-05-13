#pragma once

#include "../allocator.h"

// arenaAllocator

typedef struct ArenaAllocator {
    struct Allocator base;
    uint64 size;
    uint64 offset;
    void* mem;
} ArenaAllocator;

AllocationResult arenaAllocatorAllocateRaw(Allocator* self, AllocationRequest request);

bool32 arenaAllocatorCreate(ArenaAllocator* allocator, uint64 size, Allocator* parent, AllocationLifetimePolicy lifetime, uint32 metaDataCount, AllocationMetaData* metaData);

void arenaAllocatorDestroy(ArenaAllocator* allocator, uint32 metaDataCount, AllocationMetaData* metaData);

void* arenaAllocatorAllocate(uint64 size, ArenaAllocator* allocator);

void arenaAllocatorReset(ArenaAllocator* allocator);