#include "listpool.h"

#include <AvUtils/avMemory.h>
#include "logging.h"

void initListPool(ListPool* pool){
    for(uint32 i = 0; i < MAX_SIZE_CLASSES; i++){
        pool->lists[i].head = NULL;
        pool->lists[i].count = 0;
        pool->lists[i].totalBytes = 0;
    }

    pool->elementMinSize = 4;
    pool->maxBytesPerClass = 1 << 20;
    pool->largeClassThreashold = 16;
}

void freeListPool(ListPool* pool){
    for(uint32 i = 0; i < MAX_SIZE_CLASSES; i++){
        FreeList* list = &pool->lists[i];

        while(list->head){
            FreeNode* node = list->head;
            list->head = node->next;

            list->count--;
            //avDebug("Freed class from pool %u", i);
            avFree(node);
        }
    }
}

void* poolAlloc(ListPool* pool, uint32 class, uint32 stride){
    FreeList* list = &pool->lists[class];

    if(list->head){
        FreeNode* node = list->head;
        list->head = node->next;

        list->count--;
        list->totalBytes -= (1U << class) * stride;

        return node;
    }

    uint32 size = (1U << class) * stride;
    //avDebug("Allocated class %u", class);
    return avAllocate(size, "");
}

void poolFree(ListPool* pool, void* ptr, uint32 class, uint32 stride){
    uint32 size = (1U << class) * stride;

    if(class >= pool->largeClassThreashold){
        avFree(ptr);
        return;
    }

    FreeList* list = &pool->lists[class];

    if(list->totalBytes + size > pool->maxBytesPerClass){
        //avDebug("Freed class %u", class);
        avFree(ptr);
        return;
    }
   // avDebug("Returned class %u", class);
    FreeNode* node = (FreeNode*) ptr;
    node->next = list->head;
    list->head = node;

    list->count++;
    list->totalBytes += size;
}

uint32 sizeToClass(uint32 count, uint32 stride){
    uint32 size = count* stride;

    if(size < 4) size = 4;
    uint32 class = 0;
    uint32 cap = 1;
    while(cap < size){
        cap <<= 1;
        class++;
    }
    return class;
}

void listInit(GenericList* list, ListPool* pool, uint32 stride){
    list->data = NULL;
    list->count = 0;
    list->capacityClass = 0;
    list->stride = stride;
    list->pool = pool;
}

void listResize(GenericList* list, uint32 newClass){
    void* newMem = poolAlloc(list->pool, newClass, list->stride);

    uint32 newCapacity = 1U << newClass;
    uint32 copyCount = list->count;

    if(copyCount > newCapacity){
        copyCount = newCapacity;
    }
    if(list->data){
        avMemcpy(newMem, list->data, copyCount * list->stride);
        poolFree(list->pool, list->data, list->capacityClass, list->stride);
    }

    list->data = newMem;
    list->capacityClass = newClass;

    if(list->count > newCapacity){
        list->count = newCapacity;
    }
}

void listPush(GenericList* list, void* element){
    uint32 capacity = 1U << list->capacityClass;

    if(list->data == NULL || list->count >= capacity){
        uint32 newClass = (list->data==NULL) ? 0 : list->capacityClass + 1;
        listResize(list, newClass);
    }

    uint8* base = (uint8*) list->data;
    avMemcpy(base + list->count * list->stride, element, list->stride);

    list->count++;
}

void listPop(GenericList* list){
    if(list->count == 0) return;
    list->count--;

    uint32 capacity = 1U << list->capacityClass;

    if(list->capacityClass > 0 && list->count <= (capacity >> 1)) {
        listResize(list, list->capacityClass - 1);
    }
}

void listFree(GenericList* list){
    if(list->data){
        poolFree(list->pool, list->data, list->capacityClass, list->stride);
    }
    list->data = NULL;
    list->count = 0;
}

void listSwapPop(GenericList* list, uint32 index){
    if(index >= list->count) return;

    uint32 last = list->count - 1;
    uint8* base = (uint8*)list->data;

    if(index != last){
        avMemcpy(base + index * list->stride, base + last * list->stride, list->stride);
    }

    listPop(list);
}

void listReserve(GenericList* list, uint32 desiredCount){
    uint32 requiredClass = sizeToClass(desiredCount, list->stride);

    if(list->data == NULL || requiredClass > list->capacityClass){
        listResize(list, requiredClass);
    }
}

void listPopNoShrink(GenericList* list){
    if(list->count > 0) list->count--;
}

void listClear(GenericList* list){
    list->count = 0;
}

void listShrinkToFit(GenericList* list){
    uint32 requiredClass = sizeToClass(list->count, list->stride);
    if(requiredClass < list->capacityClass){
        listResize(list, requiredClass);
    }
}