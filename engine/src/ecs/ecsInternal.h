#pragma once
#include "ecs.h"

#include <AvUtils/avMemory.h>
#include <AvUtils/threading/avRwLock.h>
#include <AvUtils/threading/avThread.h>
#include <AvUtils/memory/avAllocator.h>
#include "logging.h"

#include <stdatomic.h>

#define COMPONENT_REGISTRY_SIZE MAX_COMPONENT_COUNT
typedef struct ComponentEntry {
	uint32 size;
    ComponentConstructor constructor;
    ComponentDestructor destructor;
} ComponentEntry;

typedef struct ComponentRegistry {
	ComponentMask registeredComponents;
	ComponentEntry entries[COMPONENT_REGISTRY_SIZE];
} ComponentRegistry;

typedef ComponentData Component;

typedef Entity LocalEntity;

#define INVALID_ENTITY_TYPE ((EntityTypeID)-1)
//#define ENTITY_ID_RESERVED ((Entity)-2)


#define ENTITY_LOCAL_INDEX(entity) ((entity) & 0xff)
#define ENTITY_CHUNK(entity) (((entity) & ((MAX_CHUNKS-1)<<8))>>8)
#define ENTITY(chunk, index) (((chunk & 0xffff)<<8) | ((index) & 0xff))

#define ENTITY_GENERATION(entity) (((entity) >> 24) & 0x3f)
#define ENTITY_STAGED(entity) (((entity) >> 31) & 0x1)
//#define ENTITY_COMPONENT_STAGED(entity) (((entity) >> 30) & 0x1)

#define ENTITY_INDEX(entity) ((entity) & 0xffffff)

#define GLOBAL_ENTITY(generation, entity) ((((generation)&0x7f)<<24)|((entity)&0xffffff))

#define CHUNK_CAPACITY 5 //256
typedef struct EntityChunk {
    Component components[COMPONENT_REGISTRY_SIZE];
    Entity entities[CHUNK_CAPACITY];
    EntityTypeID entityType;
    uint32 count;
    uint8 localIndex[CHUNK_CAPACITY];
    uint8 localID[CHUNK_CAPACITY];
} EntityChunk;

#define MAX_CHUNKS 65536
typedef struct ChunkPool {
    uint32 chunkCount;
    EntityChunk chunks[MAX_CHUNKS];
    uint32 chunkIndex[MAX_CHUNKS];
    uint32 chunkReference[MAX_CHUNKS];
    bool8 initialized;
} ChunkPool;

typedef struct EntityType {
    ComponentMask mask; // order of components is specified in order of mask
    uint16 componentIndex[MAX_COMPONENT_COUNT];
    uint32 chunkCount;
    uint32 chunkCapacity;
    uint32* chunks;
    EntityTypeID typeID;
    Scene scene;
}EntityType;

typedef struct StagedComponent{
    ComponentType type;
    Entity entity;
    uint32 lastModifiedCommandIndex;
    bool8 isClone;
    bool8 isDestroyed;
    //uint32 index;
    byte* data;
} StagedComponent;

typedef struct StagedEntity {
    ComponentMask mask;
    uint32 createCommandIndex;
    Entity ID;
    struct StagedComponentList{
        StagedComponent data;
        struct StagedComponentList* next;
    }* components;
}StagedEntity;

typedef struct StagingBuffer {
    uint8 threadID;
    AvAllocator componentAllocator;
    StagedEntity* entities;
    AvAllocator componentHandleAllocator;
} StagingBuffer;

struct Scene {
    uint32 entityTypeCapacity;
    uint32 entityTypeCount;
    uint32* entityTypeIndex;
    EntityTypeID* entityTypeReference;
    EntityType* entityTypes;

    AvRwLock entityIdLock;
    uint32 entityCapacity; // capacity of entity ids
    uint32 entityMaxCount; // increases with every chunk allocated
    _Atomic uint32 entityCount; // number of entities
    _Atomic Entity* entityTable; // SceneEntity -> ChunkEntity
    uint8* entityGeneration;
    //Entity* entityReference;

    StagingBuffer* stagingBuffers; // IDMAPPING
};


uint32 getComponentSize(ComponentType component);
ComponentConstructor getComponentConstructor(ComponentType component);
ComponentDestructor getComponentDestructor(ComponentType component);

Entity allocateEntityID(Scene scene, LocalEntity value, bool8 staged);
void freeEntityID(Scene scene, Entity entity);

bool32 getEntityDetails(Scene scene, Entity entity, uint32* index, LocalEntity* localEntity, uint8* generation, bool8* staged);
LocalEntity getEntityLocal(Scene scene, Entity entity);

bool32 createEmptyEntity(Scene scene, Entity entity, ComponentMask mask);
EntityChunk* getLocalEntityChunk(LocalEntity localEntity);
uint32 getLocalEntityLocalIndex(LocalEntity localEntity);
EntityType* getEntityType(Scene scene, EntityTypeID type);
uint32 getComponentIndex(EntityType* type, ComponentType component);

typedef struct StagingBuffer* StagingBufferHandle;

void stagingBufferCreate(Scene scene, AvThreadID threadId);
void stagingBufferDestroy(Scene scene, AvThreadID threadId);
bool32 stagedEntityDestroy(Scene scene, StagingBufferHandle buffer, Entity entity);
bool32 stagedEntityRemoveComponent(Scene scene, StagingBufferHandle buffer, Entity entity, ComponentType type);
bool32 stagedEntityAddComponent(Scene scene, StagingBufferHandle buffer, Entity entity, ComponentInfo* info);
Entity stagedEntityCreate(Scene scene, StagingBufferHandle buffer, ComponentInfoRef info);
bool32 stagedEntityHasComponent(Scene scene, StagingBufferHandle buffer, Entity entity, ComponentType type);

bool32 stagingBufferCommit(Scene scene, StagingBufferHandle buffer);