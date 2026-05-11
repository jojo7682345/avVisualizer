#pragma once
#include "defines.h"

#define MAX_SIZE_CLASSES 32

typedef struct FreeNode {
    struct FreeNode* next;
} FreeNode;

typedef struct {
    FreeNode* head;
    uint32 count;
    uint32 totalBytes;
} FreeList;

typedef struct {
    FreeList lists[MAX_SIZE_CLASSES];

    uint32 elementMinSize; 
    uint32 maxBytesPerClass;

    uint32 largeClassThreashold;
} ListPool;

typedef struct {
    void* data;
    uint32 count;
    uint32 capacityClass;
    uint32 stride;
    ListPool* pool;
} GenericList;
#include "listpool.h"

#include <AvUtils/avMemory.h>

void initListPool(ListPool* pool);
void freeListPool(ListPool* pool);

uint32 sizeToClass(uint32 count, uint32 stride);

void listInit(GenericList* list, ListPool* pool, uint32 stride);
void listPush(GenericList* list, void* element);
void listPop(GenericList* list);
void listFree(GenericList* list);
void listSwapPop(GenericList* list, uint32 index);
void listReserve(GenericList* list, uint32 desiredCount);
void listPopNoShrink(GenericList* list);
void listClear(GenericList* list);
void listShrinkToFit(GenericList* list);

#define LIST_INIT(list, pool, type) \
    listInit(&(list), (pool), sizeof(type))

#define LIST_GET(list, type, index) \
    (((type*)(list).data)[index])

#define LIST_PUSH(list, value) \
    do {\
        __typeof__(value) tmp = (value);\
        listPush(&(list), &tmp); \
    } while(0)

#define LIST_PUSH_PTR(list, ptr) \
    listPush(&(list), (ptr))

#define LIST_SWAP_POP(list, index) \
    listSwapPop(&(list), (index))

#define LIST_POP(list) \
    listPop(&(list))

#define LIST_COUNT(list) ((list).count)

#define LIST_CAPACITY(list) (1U << (list).capacityClass)

#define LIST_DATA(list, type) \
    ((type*)(list).data)

#define LIST_FOR(list, type, it) \
    for(type* it = (type*)(list).data; \
        it < (type*)(list).data + (list).count; \
        ++it)

#define LIST_FOR_I(list, i) \
    for(uint32 i = 0; i < (list).count; i++)

#define LIST_RESERVE(list, count) \
    listReserve(&(list), (count))

#define LIST_FREE(list) \
    listFree(&(list))
