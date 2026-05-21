#pragma once

#include "../../allocator.h"

// arenaAllocator
#include <stdatomic.h>

typedef struct VmAllocator {
    Allocator base;
    uint64 allocationGranularity;
    uint64 pageSize;

    _Atomic uint64 reservedMem; // global statistic
    _Atomic uint64 commitedMem; 
} VmAllocator;


bool32 vmAllocatorCreate(VmAllocator* allocator);
void vmAllocatorDestroy(VmAllocator* allocator);
void* vmAllocatorAllocate(uint64* size, uint64 reservedSize, VmAllocator* allocator);
void vmAllocatorFree(void* ptr, VmAllocator* allocator);
uint32 vmAllocatorResize(void* ptr, uint64 size, VmAllocator* allocator);