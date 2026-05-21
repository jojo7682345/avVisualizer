#pragma once

#include "defines.h"

typedef struct MemorySystemConfig {
    uint64 addressSpaceSize; // if zero, then default to 32TB
} MemorySystemConfig;


#define _1KB (1024ULL)
#define _1MB (1024ULL * _1KB)
#define _1GB (1024ULL * _1MB)
#define _1TB (1024ULL * _1GB)

typedef struct Allocator Allocator;

typedef enum AllocatorOperation{
    ALLOCATOR_ALLOCATE, // allocator results holds new ptr & size
    ALLOCATOR_FREE, // allocator results holds size=1
    ALLOCATOR_RESIZE, // a resizing allocator invalidates all other pointers in the allocator
    ALLOCATOR_RESET, // allocator result holds size=1
}AllocatorOperation;

// alignments must be power of 2
typedef struct AllocatorAlignment {
    uint32 max; // max alignment of 0 means no maximum
    uint32 min; // min of 0 means no alignment minimum
    uint32 preffered; // preffered alignment of 0 means no preffered alignment
} AllocatorAlignment;

typedef struct AllocatorCapabilities {
    uint32 supportsResize : 1;
    uint32 resizeInplace : 1;
    uint32 supportsFree : 1;
    uint32 supportsReset : 1;
    uint32 supportsAlignment : 1;
    uint32 supportsReservation : 1; // means it can track allocations and grow them
    AllocatorAlignment alignment;
    uint64 minSize;
}AllocatorCapabilities;

// ptr == 0 && size == 0 indicates failure
typedef struct AllocationResult {
    void* ptr; 
    union {
        uint64 size;
        bool64 success;
    };
} AllocationResult;


typedef enum AllocationLifetimePolicy {
    ALLOCATION_LIFETIME_PERMANENT,      // effectively permanent
    ALLOCATION_LIFETIME_LONG_LIVED,      // long-lived but not immortal
    ALLOCATION_LIFETIME_SHORT_LIVED,     // medium churn
    ALLOCATION_LIFETIME_FRAME_LIVED,   // high churn / short lived
} AllocationLifetimePolicy;

typedef enum AllocationGrowthPolicy {
    ALLOCATION_GROWTH_FIXED,
    ALLOCATION_GROWTH_IN_PLACE,
    ALLOCATION_GROWTH_RELOCATE,
}AllocationGrowthPolicy;

typedef struct AllocationMetaData {
    uint32 type;
    void* data;
}AllocationMetaData;

typedef struct AllocationRequest {
    AllocatorOperation op;
    uint32 align;
    uint32 metaDataCount;
    AllocationGrowthPolicy growth;
    AllocationLifetimePolicy lifetime;
    uint64 size;
    void* oldPtr;
    union {
        uint64 oldSize;
        uint64 reservedSize;
    };
    AllocationMetaData* metaData;
}AllocationRequest;


typedef AllocationResult (*AllocatorDispatch)(
    Allocator* self, // the allocator
    AllocationRequest request
);

struct Allocator {
    AllocatorDispatch dispatch;
    Allocator* upstream;
    void* ctx;
    AllocatorCapabilities capabilities;
};

static inline uint64 alignForward(uint64 ptr, uint64 align) {
    if(align==0) return ptr;
    uint64 p = ptr;
    uint64 modulo = p & (align - 1);

    if(modulo != 0)
        p += align - modulo;

    return p;
}

// (uint32)-1 indicates requested alignment falls outside alignment min/max
static inline uint32 checkAlignment(uint64 requestedAlignment, AllocatorAlignment alignment){
    uint32 result = (uint32)requestedAlignment;
    if(result == 0) result = alignment.preffered;
    if(alignment.min != 0 && result < alignment.min) return (uint32)-1;
    if(alignment.max != 0 && result > alignment.max) return (uint32)-1;
    if(result != 0 && (result & (result - 1)) != 0) return (uint32)-1;
    return result;
}

static inline bool32 isAligned(uint64 ptr, uint64 align){
    return ((uint64)ptr & (align - 1)) == 0;
}

Allocator initAllocatorBase(void* ctx, AllocatorDispatch dispatch, Allocator* upstream, AllocatorCapabilities capabilities);

extern Allocator* pageAllocator;

AV_API bool8 initMemorySystem(MemorySystemConfig config);

