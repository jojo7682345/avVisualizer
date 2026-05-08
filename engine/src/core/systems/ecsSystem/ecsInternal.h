#pragma once
#include "../ecs.h"
#include "../jobs.h"

#include <stdatomic.h>
#include <AvUtils/avMemory.h>
#include <AvUtils/threading/avRwLock.h>
#include <AvUtils/threading/avThread.h>
#include <AvUtils/memory/avAllocator.h>

#include "logging.h"
#include "ecsStaging.h"
#include "containers/listpool.h"


#define COMPONENT_REGISTRY_SIZE MAX_COMPONENT_COUNT
typedef struct ComponentEntry {
	uint32 size;
    ComponentConstructor constructor;
    ComponentDestructor destructor;
} ComponentEntry;

typedef struct ComponentRegistry {
	ComponentMask registeredComponents;
	ComponentEntry entries[COMPONENT_REGISTRY_SIZE];
    uint32 maxSize;
} ComponentRegistry;

typedef ComponentData Component;

typedef Entity LocalEntity;

#define INVALID_ENTITY_TYPE ((EntityTypeID)-1)
//#define ENTITY_ID_RESERVED ((Entity)-2)

#define ENTITY_STAGED_BIT (1U<<31)

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

    GenericList systems;
}EntityType;

typedef struct FrameDataDescriptor {
    uint32 alignment; // constant
    uint64 capacity; // if capacity == 0, then it is not resizable, may grow
    uint64 size; // may grow
    uint64 offset; // runtime dependent
} FrameDataDescriptor;

typedef struct System{
    Scene scene;
    GenericList entityTypes;
    SelectionAccessCriteria selection;
    SystemExecution execution;
    SystemProcessFn process;
    SystemDispatchFn dispatchOverride;
    void* ctx;
    bool8 enabled;
} System;


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

    struct CommandBuffer* stagingBuffers; // IDMAPPING

    ListPool pool;

    System* systems;
    SystemChunk* systemChunkMem; //darray
    EcsSystemID* systemOrder;

    uint16 frameDataDescriptorCapacity;
    uint16 frameDataDescriptorCount;
    FrameDataDescriptor* frameDataDescriptors;
    byte* frameDataMem;
    uint64 frameDataCapacity;
};


bool32 isQuerrySelected(SelectionAccessCriteria criteria, ComponentMask mask);
void registerNewSystem(Scene scene, EcsSystemID id);
void unregisterSystemFromEntityTypes(Scene scene, EcsSystemID id);
EntityChunk* getChunk(uint32 chunkID);

uint32 getComponentSize(ComponentType component);
ComponentConstructor getComponentConstructor(ComponentType component);
ComponentDestructor getComponentDestructor(ComponentType component);
uint32 getMaxComponentSize();


Entity allocateEntityID(Scene scene, LocalEntity value, bool8 staged);
void freeEntityID(Scene scene, Entity entity);

bool32 getEntityDetails(Scene scene, Entity entity, uint32* index, LocalEntity* localEntity, uint8* generation, bool8* staged);
LocalEntity getEntityLocal(Scene scene, Entity entity);

bool32 createEmptyEntity(Scene scene, Entity entity, ComponentMask mask, EntityType** entityType, LocalEntity* locEntity, EntityChunk** chunkPtr);
EntityChunk* getLocalEntityChunk(LocalEntity localEntity);
uint32 getLocalEntityLocalIndex(LocalEntity localEntity);
EntityType* getEntityType(Scene scene, EntityTypeID type);
uint32 getComponentIndex(EntityType* type, ComponentType component);

void destroySystemFromPtr(Scene scene, System* sys);
