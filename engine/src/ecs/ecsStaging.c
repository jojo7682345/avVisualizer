#include "ecsStaging.h"

#include "containers/darray.h"
#include <AvUtils/threading/avMutex.h>

typedef enum CommandType {
    CMD_NOP,
    CMD_CREATE_ENTTIY,
    CMD_DESTROY_ENTITY,
    CMD_ADD_COMPONENT,
    CMD_MODIFY_COMPONENT,
    CMD_REMOVE_COMPONENT,
} CommandType;

typedef struct Command {
    CommandType type;
    uint32 t; //derived from a atomic counter

    Entity entity;
    union {
        struct {
            ComponentType component;
        } add;
        struct{
            ComponentType component;
        } remove;
    };
} Command;

typedef struct StagedComponent{
    ComponentType type;
    Entity entity;
    uint32 lastModifiedCommandIndex;
    bool32 isClone;
    byte* data;
} StagedComponent;

typedef struct StagedComponentArray{
    Entity entity;
    ComponentType type;
    uint32 count;
    StagedComponent** data;
}StagedComponentArray;

typedef struct StagedEntity {
    ComponentMask mask;
    ComponentMask arrayMask;
    uint32 createCommandIndex;
    Entity ID;
    union {
        StagedComponent* single;
        StagedComponentArray* array;   
    }* components[MAX_COMPONENT_COUNT];
}StagedEntity;

#define MAPPING_RANGE_COUNT (4)
#define MAPPING_BLOCK_COUNT (MAX_COMPONENT_COUNT*MAPPING_RANGE_COUNT)
typedef struct StagingBuffer {
    uint8 threadID;
    AvAllocator componentAllocator;
    StagedEntity* entities;
    AvAllocator componentHandleAllocator;
    Command* commands;
    
    ComponentMask mappingBlockMask[MAPPING_RANGE_COUNT];
    uint32 mappingBlocks[MAPPING_BLOCK_COUNT];
} StagingBuffer;


#define MAPPING_BLOCK_SIZE ((CHUNK_CAPACITY * MAX_CHUNKS)/MAPPING_BLOCK_COUNT)
typedef struct EntityMappingBlock{
    Entity entityIndex[MAPPING_BLOCK_SIZE];
} EntityMappingBlock;

#define MAX_THREAD_COUNT 32
typedef struct EntityMappingPool{
    uint32 blockCount;
    union{
        EntityMappingBlock blocks;
        uint32 nextFree;
    }* blocks;
    AvRwLock lock;
} EntityMappingPool;

static EntityMappingPool pool = {0};

void initMappingPool(){
    avRWLockCreate(&pool.lock);
}

static uint32 mappingPoolAllocate(){

}

static void mappingPoolFree(uint32 block){

}

void deinitMappingPool(){
    avRWLockDestroy(pool.lock);
}

static uint32 getComponentIndex(StagedEntity* entity, ComponentType type){
    uint32 index = 0;
    if(!MASK_HAS_COMPONENT(entity->mask, type)) return MAX_COMPONENT_COUNT;
    ITERATE_MASK(entity->mask, component){
        if(type==component) return index;
        index++;
    }
    return MAX_COMPONENT_COUNT;
}

uint32 cmdCreateEntity(StagingBuffer* buffer, Entity entity){
    Command command = {
        .type = CMD_CREATE_ENTTIY,
        .entity = entity,
    };
    uint32 index = darrayLength(buffer->commands);
    darrayPush(buffer->commands, command);
    return index;
}

void cmdDestroyEntity(StagingBuffer* buffer, Entity entity){
    Command command = {
        .type = CMD_DESTROY_ENTITY,
        .entity = entity,
    };
    darrayPush(buffer->commands, command);
}

Entity stagedEntityCreate(Scene scene, StagingBuffer* buffer, ComponentInfoRef info){

    // create staged entity
    StagedEntity tmp = {0};
    uint32 entityIndex = darrayLength(buffer->entities);
    darrayPush(buffer->entities, tmp);
    StagedEntity* sEntity = &buffer->entities[entityIndex];
    LocalEntity localEntity = (buffer->threadID << 24) | (entityIndex & 0xffffff);    

    Entity entity = allocateEntityID(scene, localEntity, true);
    avRWLockReadLock(scene->entityIdLock);
    scene->entityGeneration[ENTITY_INDEX(entity)] |= ENTITY_STAGED_BIT;
    avRWLockReadUnlock(scene->entityIdLock);
   
    // emit command
    sEntity->createCommandIndex = cmdCreateEntity(buffer, entity);
}
bool32 stagedEntityAddComponent(Scene scene, Entity entity, ComponentInfoRef info);
bool32 stagedEntityRemoveComponent(Scene scene, Entity entity, ComponentType type, uint32 index);
bool32 stagedEntityRemoveComponentType(Scene scene, Entity entity, ComponentType type);


static void performStagedComponentDestructors(Scene scene, StagingBuffer* buffer, Entity entity, StagedEntity* sEntity){
    ITERATE_MASK(sEntity->mask, component){
        uint32 componentIndex = getComponentIndex(sEntity, component);
        uint32 size = getComponentSize(component);
        ComponentDestructor destructor = getComponentDestructor(component);
        if(destructor==NULL) continue;
        if(MASK_HAS_COMPONENT(sEntity->arrayMask, component)){
            StagedComponentArray* array = sEntity->components[componentIndex]->array;
            for(uint32 i = 0; i < array->count; i++){
                StagedComponent* component = array->data[i];
                if(component->isClone) continue;
                destructor(scene, entity, component->data, size);
                // remove modify command
                buffer->commands[component->lastModifiedCommandIndex].type = CMD_NOP;
            }
        }else{
            StagedComponent* component = sEntity->components[componentIndex]->single;
            if(component->isClone) continue;
            destructor(scene, entity, component->data, size);
            buffer->commands[component->lastModifiedCommandIndex].type = CMD_NOP;
        }
    }
}

bool32 stagedEntityDestroy(Scene scene, StagingBuffer* buffer, Entity entity){
    if(entity==INVALID_ENTITY || scene==NULL || buffer==NULL) return false;
    
    bool8 isStaged;
    LocalEntity localEntity;
    uint32 entityIndex;
    if(!getEntityDetails(scene, entity, &entityIndex, &localEntity, NULL, &isStaged)) return false;
    if(isStaged){
        // remove staged command
        if(localEntity >> 24 != buffer->threadID) return false;
        StagedEntity* sEntity = &buffer->entities[localEntity & 0xffffff];

        performStagedComponentDestructors(scene, buffer, entity, sEntity);

        buffer->commands[sEntity->createCommandIndex].type = CMD_NOP; 
        // it is no longer necissarry to commit this entity 
        // as it was fully staged and not yet commited
        return true;
    }

    uint32 block = entityIndex / MAPPING_BLOCK_COUNT;
    uint32 entityBlockIndex = entityIndex % MAPPING_BLOCK_COUNT;
    uint32 maskIndex = block / (MAX_COMPONENT_COUNT);
    uint32 blockBit = block % MAX_COMPONENT_COUNT;
    if(MASK_HAS_COMPONENT(buffer->mappingBlockMask[maskIndex], blockBit)){
        //the block is allocated to this thread
        uint32 blockIndex = entityIndex - block*MAPPING_BLOCK_SIZE;
        uint32 poolIndex = buffer->mappingBlocks[block];
        avRWLockReadLock(pool.lock);
        if(poolIndex >= pool.blockCount) {
            avRWLockReadUnlock(pool.lock);
            goto skipDestructors;
        }
        EntityMappingBlock* mappingBlock = &pool.blocks[poolIndex].blocks;
        avRWLockReadUnlock(pool.lock);
        LocalEntity locEntity;
        if((locEntity = mappingBlock->entityIndex[entityBlockIndex]) == INVALID_ENTITY) goto skipDestructors;
        
        uint32 localEntityIndex = locEntity & 0xffffff;
        uint32 threadId = locEntity >> 24;
        if(threadId != buffer->threadID) goto skipDestructors;

        StagedEntity* sEntity = &buffer->entities[localEntityIndex]; 
        // a partially staged entity only holds staged components
        performStagedComponentDestructors(scene, buffer, entity, sEntity);
    }
skipDestructors:
    cmdDestroyEntity(buffer, entity);
    return true;
}
uint32 stagedEntityGetComponentCount(Scene scene, Entity entity, ComponentType type);
void* stagedEntityGetComponentRead(Scene scene, Entity entity, ComponentType type, uint32 index);
void* stagedEntityGetComponentReadFast(Scene scene, Entity entity, ComponentType type, uint32 index);
void* stagedEntityGetComponentWrite(Scene scene, Entity entity, ComponentType type, uint32 index);
void* stagedEntityGetComponentWriteFast(Scene scene, Entity entity, ComponentType type, uint32 index);