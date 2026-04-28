#pragma once

#include "ecsInternal.h"

typedef enum CommandType {
    CMD_ENTITY_CREATE,
    CMD_ENTITY_DESTROY,
    CMD_COMPONENT_ADD,
    CMD_COMPONENT_REMOVE,
} CommandType;

typedef struct Command {
    CommandType type;

    Entity entityId;
    ComponentType componentId;

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
} CommandBucket;

#define MAX_SIZE_CLASSES 32

typedef struct FreeNode {
    uint32 next;
} FreeNode;

typedef struct CommandBuffer {
    CommandBucket createCommands; // darray
    CommandBucket destroyCommands;
    CommandBucket removeCommands;
    CommandBucket addCommands;

    uint8* dataBlob;     // raw component payload storage
    uint32 dataOffset;
    uint32 dataBlobCapacity;

    Command* commandMem;
    uint32 commandSize;
    uint32 commandCapacity; // stored as log2(capacity)

    uint32 freeList[MAX_SIZE_CLASSES];
} CommandBuffer; // command buffer per frame

void commandBufferCreate(CommandBuffer* buffer);
void commandBufferDestroy(CommandBuffer* buffer);

Entity cmdEntityCreate(Scene scene, CommandBuffer* buffer); //only from main thread
void cmdEntityAddComponent(Scene scene, CommandBuffer* buffer, Entity entity, ComponentType type, void* constructorData, uint32 constructorDataSize);
void cmdEntityRemoveComponent(Scene scene, CommandBuffer* buffer, Entity entity, ComponentType type);
void cmdEntityDestroy(Scene scene, CommandBuffer* buffer, Entity entity); //only from main thread