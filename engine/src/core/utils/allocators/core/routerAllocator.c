#include "../../allocator.h"
#include "vmAllocator.h"

#include "core/platform.h"
#include "core/utils/math.h"

#define ROUTER_SIZE_CLASSES 20 // from 1 - 512KB
#define ROUTER_LIFETIME_LEVELS 4 //permanent - long-lived - short-lived - frame


typedef struct RouterAllocator {
    Allocator base;

    Allocator* allocators[ROUTER_LIFETIME_LEVELS][ROUTER_SIZE_CLASSES];
    VmAllocator* vmAllocator;
} RouterAllocator;


static inline uint8 calculateSizeClass(uint64 size){
    if(size==0) return (uint8) -1;
    uint8 msb = getMsb(size);
    if((size & (size - 1)) != 0) msb += 1;
    return msb;
}

typedef struct RouteHeader {
    uint8 sizeClass;
    uint8 lifetimeClass;
} RouteHeader;

static inline Allocator* findSuitableAllocator(RouterAllocator* allocator, AllocationRequest* request, uint8* sizeClass){
    uint64 sizeClassSize = 0;
    if(request->reservedSize == 0){
        switch(request->growth){
            case ALLOCATION_GROWTH_FIXED:
            case ALLOCATION_GROWTH_RELOCATE:
                sizeClassSize = request->size + sizeof(RouteHeader);
                break;
            case ALLOCATION_GROWTH_IN_PLACE:{
                uint64 reserveTarget;
                if (__builtin_mul_overflow(request->size, 8ULL, &reserveTarget)) return NULL;
                if (__builtin_add_overflow(reserveTarget, sizeof(RouteHeader), &reserveTarget)) return NULL;
                sizeClassSize = 1ULL << calculateSizeClass(reserveTarget);
                break;
            }
        }
    }else{
        sizeClassSize = request->reservedSize;
    }
    if(sizeClassSize == 0) return NULL;
    uint64 allocSize = request->size + sizeof(RouteHeader);
    if(request->reservedSize < allocSize){
        request->reservedSize = allocSize;
    }
    *sizeClass = calculateSizeClass(sizeClassSize);

    Allocator* alloc = &allocator->vmAllocator->base;

    if(*sizeClass < ROUTER_SIZE_CLASSES) {
        alloc = allocator->allocators[request->lifetime][*sizeClass];
    }
    return alloc;
}

AllocationResult routerAllocatorDispatch(Allocator* self, AllocationRequest request){
    AllocationResult result = {.ptr=0, .success=0};
    RouterAllocator* allocator = (RouterAllocator*)self->ctx;
    switch(request.op){
        case ALLOCATOR_ALLOCATE:{
            uint8 sizeClass = 0;
            Allocator* alloc = findSuitableAllocator(allocator, &request, &sizeClass);     
            result = alloc->dispatch(alloc, request);
            if(!result.success) return result; 
            RouteHeader* header = result.ptr;
            header->sizeClass = sizeClass;
            header->lifetimeClass = request.lifetime;
            result.ptr = (void*)((byte*)result.ptr + sizeof(RouteHeader));
            result.size-= sizeof(RouteHeader);
            return result;
        }
        case ALLOCATOR_FREE:{
            RouteHeader* header = (RouteHeader*)request.oldPtr - 1;
            uint8 sizeClass = header->sizeClass;
            Allocator* alloc = &allocator->vmAllocator->base;
            if(sizeClass < ROUTER_SIZE_CLASSES){
                alloc = allocator->allocators[header->lifetimeClass][sizeClass];
            }
            request.size += sizeof(RouteHeader);
            request.oldPtr = header;
            result = alloc->dispatch(alloc, request);
            return result;
        }
        case ALLOCATOR_RESIZE:{
            RouteHeader* header = (RouteHeader*)request.oldPtr - 1;
            uint8 sizeClass = header->sizeClass;
            Allocator* alloc = &allocator->vmAllocator->base;
            if(sizeClass < ROUTER_SIZE_CLASSES){
                alloc = allocator->allocators[header->lifetimeClass][sizeClass];
            }
            request.size += sizeof(RouteHeader);
            request.oldPtr = header;
            result = alloc->dispatch(alloc, request);
            return result;
        }
        default: return result;
    }
}

bool32 routerAllocatorCreate(RouterAllocator* allocator, VmAllocator* vmAllocator, Allocator* permanentAllocators[ROUTER_SIZE_CLASSES], Allocator* longLivedAllocators[ROUTER_SIZE_CLASSES], Allocator* shortLivedAllocators[ROUTER_SIZE_CLASSES], Allocator* frameLivedAllocators[ROUTER_SIZE_CLASSES]){
    allocator->vmAllocator = vmAllocator;
    for(uint32 i = 0; i < ROUTER_SIZE_CLASSES; i++){
        allocator->allocators[ALLOCATION_LIFETIME_PERMANENT][i] = permanentAllocators[i];
        allocator->allocators[ALLOCATION_LIFETIME_LONG_LIVED][i] = longLivedAllocators[i];
        allocator->allocators[ALLOCATION_LIFETIME_SHORT_LIVED][i] = shortLivedAllocators[i];
        allocator->allocators[ALLOCATION_LIFETIME_FRAME_LIVED][i] = frameLivedAllocators[i];
    }
    return true;
}


void routerAllocatorDestroy(RouterAllocator* allocator){
    allocator->vmAllocator = NULL;
    for(uint32 i = 0; i < ROUTER_SIZE_CLASSES; i++)
        for(uint32 j = 0; j < ROUTER_LIFETIME_LEVELS; i++){
            allocator->allocators[j][i] = NULL;
    }
}