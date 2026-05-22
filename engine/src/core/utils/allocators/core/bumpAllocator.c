#include "../../allocator.h"
#include "logging.h"
#include "core/platform.h"
#include "bumpAllocator.h"


AllocationResult bumpAllocatorDispatch(Allocator* self, AllocationRequest request){
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

AllocationResult resetableBumpAllocatorDispatch(Allocator* self, AllocationRequest request){
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

bool32 bumpAllocatorCreate(BumpAllocator* allocator, MemoryRegion region, uint64 pageSize){
    allocator->base = (Allocator){
        .capabilities = (AllocatorCapabilities){
            .supportsResize = 0,
            .supportsFree = 0,
            .supportsReset = 0,
            .supportsAlignment = pageSize,
            .alignment = { .max = pageSize, .min = pageSize, .preffered = pageSize }
        },
        .ctx = allocator,
        .dispatch = bumpAllocatorDispatch,
        .upstream = NULL,
    };
    atomic_store_explicit(&allocator->offset, 0, memory_order_release);
    return true;
}

bool32 resetableBumpAllocatorCreate(ResetableBumpAllocator* allocator, MemoryRegion region, uint64 pageSize){
    allocator->base = (Allocator){
        .capabilities = (AllocatorCapabilities){
            .supportsResize = 0,
            .supportsFree = 0,
            .supportsReset = true,
            .supportsAlignment = pageSize,
            .alignment = { .max = pageSize, .min = pageSize, .preffered = pageSize }
        },
        .ctx = allocator,
        .dispatch = resetableBumpAllocatorDispatch,
        .upstream = NULL,
    };
    allocator->offset = 0;
    allocator->generation = 0;
    atomic_thread_fence(memory_order_release);
    return true;
}

void* bumpAllocatorAllocate(uint64 size, BumpAllocator* allocator){
    AllocationRequest request = {
        .size = size,
        .op = ALLOCATOR_ALLOCATE,
    };


}
void* resetableBumpAllocatorAllocate(uint64 size, ResetableBumpAllocator* allocator);

void resetableBumpAllocatorReset(ResetableBumpAllocator* allocator);