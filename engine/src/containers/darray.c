#include "containers/darray.h"

#include <AvUtils/avMemory.h>
#include <AvUtils/avLogging.h>
typedef struct darrayHeader {
    uint64 capacity;
    uint64 length;
    uint64 stride;
    AvAllocator* allocator;
} darrayHeader;

void* _darrayCreate(uint64 length, uint64 stride, uint64 initLength, AvAllocator* allocator, uint32 line, const char* func, const char* file) {
    uint64 headerSize = sizeof(darrayHeader);
    uint64 arraySize = length * stride;
    void* newArray = 0;
    if (allocator) {
        newArray = avAllocatorAllocate(sizeof(headerSize + arraySize), allocator);

    } else {
        newArray = avAllocate_(headerSize + arraySize, "darray", line, func, file);
    }
    avMemset(newArray, 0, headerSize + arraySize);
    if (length == 0) {
        avAssert(0, "_darrayCreate called with length of 0");
    }
    darrayHeader* header = newArray;
    header->capacity = length;
    header->length = initLength;
    header->stride = stride;
    header->allocator = allocator;

    return (void*)((uint8*)newArray + headerSize);
}

void darrayDestroy(void* array) {
    if (array) {
        uint64 headerSize = sizeof(darrayHeader);
        darrayHeader* header = (darrayHeader*)((uint8*)array - headerSize);
        uint64 total_size = headerSize + header->capacity * header->stride;
        if (!header->allocator) {
            avFree(header);
        }
    }
}

void* _darrayResize(void* array) {
    uint64 headerSize = sizeof(darrayHeader);
    darrayHeader* header = (darrayHeader*)((uint8*)array - headerSize);
    if (header->capacity == 0) {
        avAssert(0, "_darray_resize called on an array with 0 capacity. This should not be possible.");
        return 0;
    }
    void* temp = _darrayCreate((DARRAY_RESIZE_FACTOR * header->capacity), header->stride, 0, header->allocator, __LINE__, __func__, __FILE__);

    header = (darrayHeader*)((uint8*)array - headerSize);
    avMemcpy(temp, array, header->length * header->stride);

    darrayLengthSet(temp, header->length);
    darrayDestroy(array);
    return temp;
}

void* _darrayPush(void* array, const void* value_ptr) {
    uint64 headerSize = sizeof(darrayHeader);
    darrayHeader* header = (darrayHeader*)((uint8*)array - headerSize);
    if (header->length >= header->capacity) {
        array = _darrayResize(array);
    }
    header = (darrayHeader*)((uint8*)array - headerSize);

    uint64 addr = (uint64)array;
    addr += (header->length * header->stride);
    avMemcpy((void*)addr, value_ptr, header->stride);
    darrayLengthSet(array, header->length + 1);
    return array;
}

void _darray_pop(void* array, void* dest) {
    uint64 length = darrayLength(array);
    uint64 stride = darrayStride(array);
    if (length < 1) {
        avAssert(0, "darrayPop called on an empty darray. Nothing to be done.");
        return;
    }
    uint64 addr = (uint64)array;
    addr += ((length - 1) * stride);
    avMemcpy(dest, (void*)addr, stride);
    darrayLengthSet(array, length - 1);
}

void* darrayPopAt(void* array, uint64 index, void* dest) {
    uint64 length = darrayLength(array);
    uint64 stride = darrayStride(array);
    if (index >= length) {
        avAssert(0, "Index outside the bounds of this array!");
        return array;
    }

    uint64 addr = (uint64)array;
    avMemcpy(dest, (void*)(addr + (index * stride)), stride);

    // If not on the last element, snip out the entry and copy the rest inward.
    if (index != length - 1) {
        avMemcpy(
            (void*)(addr + (index * stride)),
            (void*)(addr + ((index + 1) * stride)),
            stride * (length - (index - 1)));
    }

    darrayLengthSet(array, length - 1);
    return array;
}

void* _darrayInsertAt(void* array, uint64 index, void* value_ptr) {
    uint64 length = darrayLength(array);
    uint64 stride = darrayStride(array);
    if (index >= length) {
        avAssert(0, "Index outside the bounds of this array!");
        return array;
    }
    if (length >= darrayCapacity(array)) {
        array = _darrayResize(array);
    }

    uint64 addr = (uint64)array;

    // Push element(s) from index forward out by one. This should
    // even happen if inserted at the last index.
    avMemcpy(
        (void*)(addr + ((index + 1) * stride)),
        (void*)(addr + (index * stride)),
        stride * (length - index));

    // Set the value at the index
    avMemcpy((void*)(addr + (index * stride)), value_ptr, stride);

    darrayLengthSet(array, length + 1);
    return array;
}

void darrayClear(void* array) {
    darrayLengthSet(array, 0);
}

uint64 darrayCapacity(void* array) {
    if(array==NULL) return 0;
    uint64 headerSize = sizeof(darrayHeader);
    darrayHeader* header = (darrayHeader*)((uint8*)array - headerSize);
    return header->capacity;
}

uint64 darrayLength(void* array) {
    if(array==NULL) return 0;
    uint64 headerSize = sizeof(darrayHeader);
    darrayHeader* header = (darrayHeader*)((uint8*)array - headerSize);
    return header->length;
}

uint64 darrayStride(void* array) {
    if(array==NULL) return 0;
    uint64 headerSize = sizeof(darrayHeader);
    darrayHeader* header = (darrayHeader*)((uint8*)array - headerSize);
    return header->stride;
}

void darrayLengthSet(void* array, uint64 value) {
    if(array==NULL) return;
    uint64 headerSize = sizeof(darrayHeader);
    darrayHeader* header = (darrayHeader*)((uint8*)array - headerSize);
    header->length = value;
}