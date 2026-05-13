#include "arenaAllocator.h"

// for use downstream
AllocationResult arenaAllocatorAllocateRaw(Allocator* self, AllocationRequest request){
    ArenaAllocator* arena = (ArenaAllocator*) self->ctx;
    switch(request.op){
        case ALLOCATOR_RESET:
            arena->offset = 0;
            return (AllocationResult){.size = 1};
        case ALLOCATOR_ALLOCATE:{
            uint64 ptr = (uint64)arena->mem + arena->offset;
            uint64 newPtr = alignForward(ptr, request.align);
            uint64 newOffset = (newPtr - (uint64)arena->mem) + request.size;
            if(newOffset > arena->size){
                return (AllocationResult){.ptr=NULL, .size=0};
            }
            arena->offset = newOffset;
            return (AllocationResult){.ptr=(void*)newPtr, .size=request.size};
        }
        default:
            return (AllocationResult){.ptr=NULL, .size=0};
    }
}

bool32 arenaAllocatorCreate(ArenaAllocator* allocator, uint64 size, Allocator* parent, AllocationLifetimePolicy lifetime, uint32 metaDataCount, AllocationMetaData* metaData){
    allocator->base = initAllocatorBase(allocator, arenaAllocatorAllocateRaw, parent, (AllocatorCapabilities){
        .supportsResize = 0,
        .supportsFree = 0,
        .supportsReset = 1,
        .supportsAlignment = 1,
        .alignment = { .max = 0, .min = 0, .preffered = 0 }
    });
    AllocationRequest request = {
        .op = ALLOCATOR_ALLOCATE,
        .align = 0,
        .size = size,
        .growth = ALLOCATION_GROWTH_FIXED,
        .lifetime = lifetime,
        .metaDataCount = metaDataCount,
        .metaData = metaData,
    };
    AllocationResult res = allocator->base.upstream->dispatch(allocator->base.upstream, request);
    if(res.ptr==NULL && res.size == 0) return false;
    allocator->mem = res.ptr;
    allocator->size = res.size;
    allocator->offset = 0;
    return true;
}

void arenaAllocatorDestroy(ArenaAllocator* allocator, uint32 metaDataCount, AllocationMetaData* metaData){
    AllocationRequest request = {
        .op = ALLOCATOR_FREE,
        .oldPtr = allocator->mem,
        .oldSize = allocator->size,
        .metaDataCount = metaDataCount,
        .metaData = metaData,
    };
    allocator->base.dispatch(&allocator->base.upstream, request);
    allocator->base.dispatch = NULL;
    allocator->base.ctx = NULL;
    allocator->base.upstream = NULL;
    allocator->mem = NULL;
    allocator->offset = 0;
    allocator->size = 0;
}

void* arenaAllocatorAllocate(uint64 size, ArenaAllocator* allocator){
    AllocationRequest request = {
        .op = ALLOCATOR_ALLOCATE,
        .size = size,
    };
    return arenaAllocatorAllocateRaw(&allocator->base, request).ptr;
}

void arenaAllocatorReset(ArenaAllocator* allocator){
    AllocationRequest request = {
        .op = ALLOCATOR_RESET,
    };
    return arenaAllocatorAllocateRaw(&allocator->base, request).ptr;
}