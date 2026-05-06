#pragma once

#include "ecsInternal.h"
#include <AvUtils/threading/avThread.h>

#define FAST_CLASS_COUNT 4
#define FAST_CACHE_SIZE 8
typedef struct FastCache {
    uint32 offsets[FAST_CACHE_SIZE];
    uint32 count;
} FastCache;

typedef enum CommandType {
    CMD_COMPONENT_ADD,
    CMD_COMPONENT_REMOVE,
    CMD_ENTITY_DESTROY,
} CommandType;

typedef struct Command {
    Entity entityId;

    uint32 dataOffset;
    uint32 dataSize;
} Command;

typedef uint32 CommandID;
typedef uint32 CommandOffset;

typedef struct CommandList {
    uint32 capacity; // stored as log2(capacity)
    uint32 count;
    CommandOffset commands;
} CommandList;

typedef struct CommandBucket {
    CommandList commands[MAX_COMPONENT_COUNT]; //darray's
    ComponentMask mask;
} CommandBucket;

#define MAX_SIZE_CLASSES 32

typedef struct CommandBuffer {
    AvThreadID threadId;
    CommandList destroyCommands;
    CommandBucket removeCommands;
    CommandBucket addCommands;

    uint8* dataBlob;     // raw component payload storage
    uint32 dataOffset;
    uint32 dataBlobCapacity;

    Command* commandMem;
    uint32 commandCapacity; // stored as log2(capacity)
    uint32 freeList[MAX_SIZE_CLASSES];
    FastCache fastCache[FAST_CLASS_COUNT];
} CommandBuffer; // command buffer per frame

void commandBufferCreate(CommandBuffer* buffer);
void commandBufferDestroy(CommandBuffer* buffer);

void cmdEntityAddComponent(Scene scene, CommandBuffer* buffer, Entity entity, ComponentType type, void* constructorData, uint32 constructorDataSize);
void cmdEntityRemoveComponent(Scene scene, CommandBuffer* buffer, Entity entity, ComponentType type);
void cmdEntityDestroy(Scene scene, CommandBuffer* buffer, Entity entity);

void applyCommandBuffers(Scene scene, CommandBuffer* buffers, uint32 threadCount);