#include "../allocator.h"
#include "core/platform.h"

static struct MemorySystemState {
    uint64 pageSize;
    VirtualMemoryRegion addressSpace;
    Allocator regionAllocator;
} state;

Allocator initAllocatorBase(void* ctx, AllocatorDispatch dispatch, Allocator* upstream, AllocatorCapabilities capabilities){
    Allocator allocator = {
        .dispatch = dispatch,
        .upstream = upstream,
        .ctx = ctx,
        .capabilities = capabilities,
    };
    if(upstream==NULL) allocator.upstream = &state.regionAllocator;
    return allocator;
}

#define ADRESS_ALLOCATOR_ADDRESS_SIZE (32 * _1TB)

static AllocationResult regionAllocatorDispatch(Allocator* self, AllocationRequest request){

}

AV_API bool8 initMemorySystem(AddressAllocatorConfig config){
    state.pageSize = platformGetPageSize();
    if(config.addressSpaceSize==0) config.addressSpaceSize = ADRESS_ALLOCATOR_ADDRESS_SIZE; 

    state.addressSpace = platformReserveMemory(config.addressSpaceSize);

    state.regionAllocator.capabilities = (AllocatorCapabilities){
        .supportsAlignment = 0,
        .supportsFree = 1,
        .supportsResize = 1,
        .resizeInplace = 1,
        .supportsReset = 0,
        .alignment = {
            .max = state.pageSize,
            .min = state.pageSize,
            .preffered = state.pageSize,
        },
    };
    state.regionAllocator.ctx = &state;
    state.regionAllocator.upstream = NULL;
    state.regionAllocator.dispatch = regionAllocatorDispatch;

    return true;

}