#include "../allocator.h"
#include "logging.h"
#include "platform.h"

#include <stdatomic.h>

typedef enum AddressSpaceZone {
    ADDRESS_SPACE_ZONE_STATIC,
    ADDRESS_SPACE_ZONE_STABLE,
    ADDRESS_SPACE_ZONE_DYNAMIC,
    ADDRESS_SPACE_ZONE_TRANSIENT,
    ADDRESS_SPACE_ZONE_COUNT
} AddressSpaceZone;

// TODO: change this to something better
typedef struct AddressSpaceZoneInfo {
    uint64 reservedSize;
    uint64 usedSize;

    VirtualMemoryRegion region;
} AddressSpaceZoneInfo;


typedef struct AddressAllocator {
    VirtualMemoryRegion addressSpace;
    uint64 pageSize;

    _Atomic uint64 fixedEnd; // fixed allocation region offset from starting pointer
    _Atomic uint64 dynamicStart; // dynamic allocation region offset down from end of addressSpace
    _Atomic uint64 availableSpace;
} AddressAllocator;


static AllocationResult allocateStatic(AddressAllocator* allocator, uint64 size){
    AllocationResult result = {.ptr=0, .success=0};
    uint64 oldAvailable = atomic_fetch_sub_explicit(&allocator->availableSpace, size, memory_order_relaxed);
    if(oldAvailable < size || oldAvailable > allocator->addressSpace.size){
        atomic_fetch_add_explicit(&allocator->availableSpace, size, memory_order_relaxed);
        avError("Address space collision, likely due to fragmentation");
        return result;
    }

    uint64 offset = atomic_fetch_add_explicit(&allocator->fixedEnd, size, memory_order_relaxed);
    result.ptr = allocator->addressSpace.ptr + offset;
    result.size = size;
    return result;
}

static AllocationResult allocateDynamic(AddressAllocator* allocator, uint64 size, AllocationLifetimePolicy lifetime){

}

/**
 * @brief Allocates address space from the allocator
 * 
 * @param allocator pointer to allocator
 * @param size the maximum memory size, this size is fixed over the lifetime of the allocation
 * @param lifetime hint for address placement to reduce fragmentation
 * @return AllocationResult 
 */
static AllocationResult allocateAddressRegion(AddressAllocator* allocator, uint64 size, AllocationLifetimePolicy lifetime){
    AllocationResult result = {.ptr = 0, .success = 0};
    if((size & (allocator->pageSize-1)) != 0){
        avError("Can only allocate multiple of page size %llu", allocator->pageSize);
        return result;
    }

    switch (lifetime)
    {
    case ALLOCATION_LIFETIME_STATIC:
        return allocateStatic(allocator, size);
    
    default:
        return result;
    }



    return result;
}

static AllocationResult freeAddressRegion(AddressAllocator* allocator, void* oldPtr, uint64 oldSize){
    AllocationResult result = {.ptr = 0, .success = 0};
    uint64 ptr = (__UINTPTR_TYPE__)oldPtr;
    if((ptr & (allocator->pageSize-1)) != 0){
        avError("Invalid pointer");
        return result;
    }
    if((oldSize & (allocator->pageSize-1)) != 0){
        avError("Invalid size");
        return result;
    }




    return result;
}