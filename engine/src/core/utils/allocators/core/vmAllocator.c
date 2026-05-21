#include "../../allocator.h"

#include "core/platform.h"
#include <stdatomic.h>

typedef struct VmAllocator {
    Allocator base;
    uint64 allocationGranularity;
    uint64 pageSize;

    _Atomic uint64 reservedMem; // global statistic
    _Atomic uint64 commitedMem; 
} VmAllocator;

typedef struct VmAllocationHeader {
    uint64 reserved;
    uint64 committedSize;
}VmAllocationHeader;



// the first page of the allocation is smaller than the others (as it contains the header)
// this function may overcommit some memory due to the header
// resizing may result in a larger than expected allocation due to the header
// requests don't need to be concerned with the header size
AllocationResult vmAllocatorDispatch(Allocator* self, AllocationRequest request){
    AllocationResult result = {.ptr=0, .success=0};
    VmAllocator* allocator = (VmAllocator*) self->ctx;
    switch(request.op){
        case ALLOCATOR_ALLOCATE:{ // allocates the address space
            uint64 reservationSize = request.oldSize; // use oldsize as a hint for reservation size
            if(reservationSize < request.size && reservationSize != 0) return result; // if reservation size is specified but not enough, fail
            uint64 commitSize = alignForward(request.size + sizeof(VmAllocationHeader), allocator->pageSize); // calculate the size to hold user allocation and header (aligned to page size)
            
            // if the reservation is not specified or the calculated commit size is larger than the request size, increase the reservation size
            if(reservationSize==0 || reservationSize < commitSize) reservationSize = alignForward(commitSize, allocator->allocationGranularity); 
            
            MemoryRegion region = platformReserveMemory(reservationSize);
            if(region.ptr == NULL) return result;

            // commit the allocation
            if(!platformCommitMemory(region.ptr, commitSize)){
                // allocation failed, release the virtual memory
                platformReleaseMemory(region);
                return result;
            }

            // write the header
            VmAllocationHeader* header = region.ptr;
            header->committedSize = commitSize;
            header->reserved = reservationSize;

            // update memory diagnostics
            atomic_fetch_add_explicit(&allocator->reservedMem, reservationSize, memory_order_relaxed);
            atomic_fetch_add_explicit(&allocator->commitedMem, commitSize, memory_order_relaxed);

            // present the user with a buffer that holds at least the requested size, after the header
            result.size = commitSize - sizeof(VmAllocationHeader);
            result.ptr = header + 1;
            return result;
        }
        case ALLOCATOR_FREE:{
            if(request.oldPtr==NULL) return result;
            VmAllocationHeader* header = ((VmAllocationHeader*) request.oldPtr) - 1;
            
            MemoryRegion region = {
                .ptr = header,
                .size = header->reserved,
            };

            uint64 freedCommitted = header->committedSize;
            uint64 freedReserve = header->reserved;
            platformReleaseMemory(region);

            atomic_fetch_sub_explicit(&allocator->commitedMem, freedCommitted, memory_order_relaxed);
            atomic_fetch_sub_explicit(&allocator->reservedMem, freedReserve, memory_order_release);

            result.success = 1;
            return result;
        }  
        case ALLOCATOR_RESIZE:{
            if(request.oldPtr==NULL) return result;
            VmAllocationHeader* header = (VmAllocationHeader*)request.oldPtr - 1;
            
            // reserved region
            MemoryRegion region = {
                .ptr = header,
                .size = header->reserved,
            };
            uint64 oldSize = header->committedSize; // original requested size
            uint64 newSize = request.size + sizeof(VmAllocationHeader); // requested new size

            if(newSize == 0) return vmAllocatorDispatch(self, (AllocationRequest){.oldPtr = request.oldPtr, .op=ALLOCATOR_FREE}); 
            if(newSize == oldSize) return (AllocationResult){.ptr=request.oldPtr, .size=oldSize - sizeof(VmAllocationHeader)};
            if(newSize < allocator->pageSize) newSize = allocator->pageSize;
            if(newSize > oldSize) {
                uint64 requiredCommit = alignForward(newSize, allocator->pageSize);
                uint64 extraSize = requiredCommit - oldSize;
                if(requiredCommit > header->reserved) return result;
                if(!platformCommitMemory((byte*) region.ptr + oldSize, extraSize)){
                    return result;
                }
                header->committedSize = requiredCommit;
                atomic_fetch_add_explicit(&allocator->commitedMem, extraSize, memory_order_relaxed);
                result.ptr = request.oldPtr;
                result.size = requiredCommit - sizeof(VmAllocationHeader);
                return result;
            }else{
                uint64 truncatedSize = alignForward(newSize, allocator->pageSize);
                uint64 excesSize = oldSize - truncatedSize;

                header->committedSize = truncatedSize;
                platformDecommitMemory((byte*) region.ptr + truncatedSize, excesSize);
                atomic_fetch_sub_explicit(&allocator->commitedMem, excesSize, memory_order_relaxed);
                
                result.ptr = request.oldPtr;
                result.size = truncatedSize - sizeof(VmAllocationHeader);
                return result;
            }
        }
        default:
            return (AllocationResult){.ptr=NULL, .size=0};
    }
}



bool32 vmAllocatorCreate(VmAllocator* allocator){
    allocator->base = (Allocator) {
        .ctx = allocator,
        .upstream = NULL,
        .dispatch = vmAllocatorDispatch,
        .capabilities = (AllocatorCapabilities){
            .supportsResize = 1,
            .resizeInplace = 1,
            .supportsFree = 1,
            .supportsReset = 0,
            .supportsReservation = 1,
            .supportsAlignment = 0,
            .alignment = { .max = 0, .min = 0, .preffered = 0 },
            .minSize = 0,
        }
    };
    return true;
}

void vmAllocatorDestroy(VmAllocator* allocator){
    allocator->base.dispatch = NULL;
    allocator->base.ctx = NULL;
    allocator->base.upstream = NULL;
    allocator->commitedMem = 0;
    allocator->reservedMem = 0;
}

void* vmAllocatorAllocate(uint64* size, uint64 reservedSize, VmAllocator* allocator){
    AllocationRequest request = {
        .op = ALLOCATOR_ALLOCATE,
        .size = *size,
        .oldSize = reservedSize,
    };
    AllocationResult res = vmAllocatorDispatch(&allocator->base, request);
    if(res.success){
        *size = res.size;
        return res.ptr;
    }
    return NULL;
}

void vmAllocatorFree(void* ptr, VmAllocator* allocator){
    AllocationRequest request = {
        .op = ALLOCATOR_FREE,
        .oldPtr = ptr,
    };
    vmAllocatorDispatch(&allocator->base, request);
}

uint32 vmAllocatorResize(void* ptr, uint64 size, VmAllocator* allocator){
    AllocationRequest request = {
        .op = ALLOCATOR_RESIZE,
        .size = size,
        .oldPtr = ptr,
    };
    AllocationResult res = vmAllocatorDispatch(&allocator->base, request);
    if(res.success){
        return res.size;
    }
    return NULL;
}