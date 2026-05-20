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

// the first page of the allocation is smaller than the others (as it contains the header)
// this function may overcommit some memory due to the header
// resizing may result in a larger than expected allocation due to the header
// requests don't need to be concerned with the header size
AllocationResult vmAllocatorDispatch(Allocator* self, AllocationRequest request);
bool32 vmAllocatorCreate(VmAllocator* allocator);
void vmAllocatorDestroy(VmAllocator* allocator);
void* vmAllocatorAllocate(uint64* size, uint64 reservedSize, VmAllocator* allocator);
void vmAllocatorFree(void* ptr, VmAllocator* allocator);
uint32 vmAllocatorResize(void* ptr, uint64 size, VmAllocator* allocator);