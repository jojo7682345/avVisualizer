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

typedef struct BumpAllocator {
    struct Allocator base;
    _Atomic uint64 offset;
    VirtualMemoryRegion addressSpace;
} BumpAllocator;

typedef struct ResetableBumpAllocator {
    struct Allocator base;
    _Atomic uint64 offset;
    _Atomic uint64 generation;
    VirtualMemoryRegion addressSpace;
} ResetableBumpAllocator;

typedef struct Node {
    uint64 maxFreeRun;
    uint64 prefixFreeRun;
    uint64 suffixFreeRun;
} Node;
typedef uint64 Leaf;



typedef struct AddressAllocator {
    VirtualMemoryRegion addressSpace;
    uint64 pageSize;

    BumpAllocator fixedAllocator;
    ResetableBumpAllocator transientAllocator;
} AddressAllocator;




AllocationResult fixedAllocatorAllocate(Allocator* self, AllocationRequest request){
    AllocationResult result = {.ptr = 0, .success = 0};
    BumpAllocator* allocator = (BumpAllocator*)self->ctx;
    if(checkAlignment(request.align, self->capabilities.alignment)==(uint32)-1){
        return result;
    }
    if(!isAligned(request.size, self->capabilities.alignment.preffered)){
        return result;
    }
    switch(request.op){
        default:
            avError("Unsupported allocator operation");
            return result;
        case ALLOCATOR_ALLOCATE:{
            uint64 offset = atomic_fetch_add_explicit(&allocator->offset, request.size, memory_order_relaxed);
            if(offset + request.size >= allocator->addressSpace.size){
                avError("Ran out of address space in fixed allocations");
                atomic_fetch_sub_explicit(&allocator->offset, request.size, memory_order_relaxed);
                return result;
            }
            result.ptr = allocator->addressSpace.ptr + offset;
            result.size = request.size;
            return result;
        }
    }
}

AllocationResult scratchAllocatorAllocate(Allocator* self, AllocationRequest request){
    AllocationResult result = {.ptr = 0, .success = 0};
    ResetableBumpAllocator* allocator = (ResetableBumpAllocator*)self->ctx;
    if(checkAlignment(request.align, self->capabilities.alignment)==(uint32)-1){
        return result;
    }
    if(!isAligned(request.size, self->capabilities.alignment.preffered)){
        return result;
    }
    switch(request.op){
        default:
            avError("Unsupported allocator operation");
            return result;
        case ALLOCATOR_ALLOCATE:{
            while(true){
                bool8 generation = atomic_load_explicit(&allocator->generation, memory_order_acquire);
                uint64 offset = atomic_fetch_add_explicit(&allocator->offset, request.size, memory_order_relaxed);
                if(offset + request.size >= allocator->addressSpace.size){
                    avError("Ran out of address space in transient allocations");
                    return result;
                }
                if(atomic_load_explicit(&allocator->generation, memory_order_acquire) != generation) {
                    continue; //allocation happend during reset, retry
                }
                result.ptr = allocator->addressSpace.ptr + offset;
                result.size = request.size;
                return result;
            }
        }
        case ALLOCATOR_RESET:{
            // if a thread reads the generation just before a reset, and after the offset is set to 0, 
            // then that space will be wasted this frame, but its fine I guess, as it will be fixed next frame
            atomic_fetch_add_explicit(&allocator->generation, 1, memory_order_acq_rel);
            atomic_store_explicit(&allocator->offset, 0, memory_order_release);
        }
    }
}

bool32 fixedAllocatorCreate(BumpAllocator* allocator, VirtualMemoryRegion region, uint64 pageSize){
    allocator->base = (Allocator){
        .capabilities = (AllocatorCapabilities){
            .supportsResize = 0,
            .supportsFree = 0,
            .supportsReset = 0,
            .supportsAlignment = pageSize,
            .alignment = { .max = pageSize, .min = pageSize, .preffered = pageSize }
        },
        .ctx = allocator,
        .dispatch = fixedAllocatorAllocate,
        .upstream = NULL,
    };
    atomic_store_explicit(&allocator->offset, 0, memory_order_release);
    return true;
}

bool32 scratchAllocatorCreate(ResetableBumpAllocator* allocator, VirtualMemoryRegion region, uint64 pageSize){
    allocator->base = (Allocator){
        .capabilities = (AllocatorCapabilities){
            .supportsResize = 0,
            .supportsFree = 0,
            .supportsReset = true,
            .supportsAlignment = pageSize,
            .alignment = { .max = pageSize, .min = pageSize, .preffered = pageSize }
        },
        .ctx = allocator,
        .dispatch = scratchAllocatorAllocate,
        .upstream = NULL,
    };
    allocator->offset = 0;
    allocator->generation = 0;
    atomic_thread_fence(memory_order_release);
    return true;
}

static uint64 highestPowerOfTwo(uint64 n) {
    if (n == 0) return 0;

    n |= n >> 1ULL;
    n |= n >> 2ULL;
    n |= n >> 4ULL;
    n |= n >> 8ULL;
    n |= n >> 16ULL;
    n |= n >> 32ULL;

    return n - (n >> 1ULL);
}


void initAddressAllocator(){

    AddressAllocator allocator = {
        // TODO: fill
    };

    VirtualMemoryRegion addressSpace; // receive from somewhere
    uint64 pageSize;

    
    
    uint64 regionSize = highestPowerOfTwo(addressSpace.size / ADDRESS_SPACE_ZONE_COUNT);
    fixedAllocatorCreate(&allocator.fixedAllocator, (VirtualMemoryRegion){.ptr = (uint64)addressSpace.ptr + regionSize * 0, .size = regionSize}, pageSize);
    scratchAllocatorCreate(&allocator.transientAllocator, (VirtualMemoryRegion){.ptr = (uint64)addressSpace.ptr + regionSize * 1, .size = regionSize}, pageSize);



}