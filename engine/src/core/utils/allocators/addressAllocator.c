#include "../allocator.h"
#include "logging.h"
#include "platform.h"

typedef enum AddressSpaceZone {
    ADDRESS_SPACE_ZONE_STATIC_FIXED,
    ADDRESS_SPACE_ZONE_STATIC_RESIZABLE,

    ADDRESS_SPACE_ZONE_STABLE_FIXED,
    ADDRESS_SPACE_ZONE_STABLE_RESIZABLE,

    ADDRESS_SPACE_ZONE_DYNAMIC_FIXED,
    ADDRESS_SPACE_ZONE_DYNAMIC_RESIZABLE,

    ADDRESS_SPACE_ZONE_TRANSIENT_FIXED,
    ADDRESS_SPACE_ZONE_TRANSIENT_RESIZABLE,

    ADDRESS_SPACE_ZONE_COUNT
} AddressSpaceZone;

typedef struct AddressSpaceZoneInfo {
    uint64 reservedSize;
    uint64 usedSize;

    VirtualMemoryRegion region;
} AddressSpaceZoneInfo;


typedef struct AddressAllocator {
    VirtualMemoryRegion addressSpace;
    uint64 pageSize;

    AddressSpaceZoneInfo zones[ADDRESS_SPACE_ZONE_COUNT];

} AddressAllocator;



static AllocationResult allocateAddressRegion(AddressAllocator* allocator, uint64 size, AllocationLifetimePolicy lifetime, AllocationGrowthPolicy growth){
    AllocationResult result = {.ptr = 0, .success = 0};
    if(size & allocator->pageSize != 0){
        avError("Can only allocate multiple of page size %llu", allocator->pageSize);
        return result;
    }




    return result;
}

static AllocationResult freeAddressRegion(AddressAllocator* allocator, void* oldPtr, uint64 oldSize){
    AllocationResult result = {.ptr = 0, .success = 0};
    uint64 ptr = (__UINTPTR_TYPE__)oldPtr;
    if(ptr & allocator->pageSize != 0){
        avError("Invalid pointer");
        return result;
    }
    if(oldSize & allocator->pageSize != 0){
        avError("Invalid size");
        return result;
    }




    return result;
}