#pragma once

#include "defines.h"

typedef struct AddressAllocatorConfig {
    uint64 addressSpaceSize; // if zero, then default to 32TB
} AddressAllocatorConfig;


#define _1KB (1024)
#define _1MB (1024 * _1KB)
#define _1GB (1024 * _1MB)
#define _1TB (1024 * _1GB)

typedef struct Allocator Allocator;

typedef enum AllocatorOperation{
    ALLOCATOR_ALLOCATE, // allocator results holds new ptr & size
    ALLOCATOR_FREE, // allocator results holds size=1
    ALLOCATOR_RESIZE, // a resizing allocator invalidates all other pointers in the allocator
    ALLOCATOR_RESET, // allocator result holds size=1
}AllocatorOperation;

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
    AllocatorAlignment alignment;
}AllocatorCapabilities;

// ptr == 0 && size == 0 indicates failure
typedef struct AllocationResult {
    void* ptr; 
    union {
        uint64 size;
        bool64 success;
    };
} AllocationResult;

typedef struct AllocationRequest {
    uint64 size;
    uint64 align;
    void* oldPtr;
    uint64 oldSize;
    AllocationMetaData* metaData;
    uint32 metaDataCount;
    AllocatorOperation op;
    AllocationLifetimePolicy lifetime;
    AllocationGrowthPolicy growth;
}AllocationRequest;

typedef struct AllocationMetaData {
    uint32 type;
    void* data;
}AllocationMetaData;

typedef AllocationResult (*AllocatorDispatch)(
    Allocator* self, // the allocator
    AllocationRequest request
);

typedef enum AllocationLifetimePolicy {
    ALLOCATION_LIFETIME_STATIC,      // effectively permanent
    ALLOCATION_LIFETIME_STABLE,      // long-lived but not immortal
    ALLOCATION_LIFETIME_DYNAMIC,     // medium churn
    ALLOCATION_LIFETIME_TRANSIENT,   // high churn / short lived
} AllocationLifetimePolicy;

typedef enum AllocationGrowthPolicy {
    ALLOCATION_GROWTH_FIXED,
    ALLOCATION_GROWTH_IN_PLACE,
    ALLOCATION_GROWTH_RELOCATE,
}AllocationGrowthPolicy;

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

Allocator initAllocatorBase(void* ctx, AllocatorDispatch dispatch, Allocator* upstream, AllocatorCapabilities capabilities);

AV_API bool8 initMemorySystem(AddressAllocatorConfig config);

