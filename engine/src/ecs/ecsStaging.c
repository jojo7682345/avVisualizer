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
    CMD_REMOVE_COMPONENT_TYPE,
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
            uint32 index;
        } remove;
    };
} Command;

typedef struct StagedComponent{
    ComponentType type;
    Entity entity;
    uint32 lastModifiedCommandIndex;
    bool8 isClone;
    bool8 isDestroyed;
    uint32 index;
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
    uint32 blockCapacity;
    union{
        EntityMappingBlock blocks;
        uint32 nextFree;
    }* blocks;
    AvRwLock lock;
    _Atomic uint32 nextFree;
} EntityMappingPool;

static EntityMappingPool pool = {0};

void initMappingPool(){
    avRWLockCreate(&pool.lock);
    pool.nextFree = (uint32)-1;
}

static uint32 mappingPoolAllocate(){
    avRWLockReadLock(pool.lock);
    uint32 block;
    while(1){
        block = atomic_load_explicit(&pool.nextFree, memory_order_acquire);
        if(block == (uint32)-1){
            avRWLockReadUnlock(pool.lock);
            avRWLockWriteLock(pool.lock);
            // raw read to blockCount is valid here as we have soul ownership of the pool at the moment
            if(pool.nextFree != (uint32)-1){
                goto skipGrow; // another thread beat us to increasing the size
            }
            uint32 oldCapacity = pool.blockCapacity;
            pool.blockCapacity *= 2;
            if(pool.blockCapacity==0){
                pool.blockCapacity = 2;
            }
            pool.blocks = avReallocate(pool.blocks, pool.blockCapacity * sizeof(EntityMappingBlock), "");
            for(uint32 i = oldCapacity; i < pool.blockCapacity; i++){
                pool.blocks[i].nextFree = pool.nextFree;
                pool.nextFree = i;
            }
            skipGrow:
            avRWLockWriteUnlock(pool.lock);
            avRWLockReadLock(pool.lock);
        }
        uint32 next = pool.blocks[block].nextFree;
        if(atomic_compare_exchange_weak_explicit(&pool.nextFree, &block, next, memory_order_acquire, memory_order_relaxed)){
            goto blockFound;
        }
    }
    blockFound:
    for(uint32 i = 0; i < MAPPING_BLOCK_SIZE; i++){
        pool.blocks[block].blocks.entityIndex[i] = INVALID_ENTITY;
    }
    avRWLockReadUnlock(pool.lock);
}

static void mappingPoolFree(uint32 block) {
    avRWLockReadLock(pool.lock);
    uint32 head;
    do {
        head = atomic_load_explicit(&pool.nextFree, memory_order_acquire);
        pool.blocks[block].nextFree = head;
    } while (!atomic_compare_exchange_weak_explicit(&pool.nextFree, &head, block, memory_order_release, memory_order_relaxed));
    avRWLockReadUnlock(pool.lock);
}

void deinitMappingPool(){
    avRWLockDestroy(pool.lock);
    if(pool.blocks) avFree(pool.blocks);
}

StagedEntity* registerExistingEntity(StagingBuffer* buffer, Entity entity){
    if(buffer==NULL) return NULL;

    uint32 block = entity / MAPPING_BLOCK_COUNT;
    uint32 entityBlockIndex = entity % MAPPING_BLOCK_COUNT;
    uint32 maskIndex = block / MAX_COMPONENT_COUNT;
    uint32 blockBit = block % MAX_COMPONENT_COUNT;

    if(!MASK_HAS_COMPONENT(buffer->mappingBlockMask[maskIndex], blockBit)){
        uint32 poolIndex = mappingPoolAllocate();
        buffer->mappingBlocks[block] = poolIndex;
        MASK_ADD_COMPONENT(buffer->mappingBlockMask[maskIndex], blockBit);
    }

    uint32 poolIndex = buffer->mappingBlocks[block];

    uint32 entityIndex = darrayLength(buffer->entities);
    LocalEntity localEntity = ((buffer->threadID & 0xff) << 24) | (entityIndex & 0xffffff);
    darrayPush(buffer->entities, (StagedEntity){0});

    StagedEntity* sEntity = &buffer->entities[entityIndex];
    sEntity->ID = entity;

    avRWLockReadLock(pool.lock);
    pool.blocks[poolIndex].blocks.entityIndex[entityBlockIndex] = localEntity;
    avRWLockReadUnlock(pool.lock);
    return sEntity;
}

// static uint32 getComponentIndex(StagedEntity* entity, ComponentType type){
//     uint32 index = 0;
//     if(!MASK_HAS_COMPONENT(entity->mask, type)) return MAX_COMPONENT_COUNT;
//     ITERATE_MASK(entity->mask, component){
//         if(type==component) return index;
//         index++;
//     }
//     return MAX_COMPONENT_COUNT;
// }

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

void cmdDestroyComponentType(StagingBuffer* buffer, Entity entity, ComponentType type){
    Command command = {
        .type = CMD_REMOVE_COMPONENT_TYPE,
        .entity = entity,
        .remove.component = type,
    };
    darrayPush(buffer->commands, command);
}

void cmdDestroyComponent(StagingBuffer* buffer, Entity entity, ComponentType type, uint32 index){
    Command command = {
        .type = CMD_REMOVE_COMPONENT_TYPE,
        .entity = entity,
        .remove.component = type,
        .remove.index = index,
    };
    darrayPush(buffer->commands, command);
}

uint32 cmdAddComponent(StagingBuffer* buffer, Entity entity, ComponentType type){
    Command command = {
        .type = CMD_ADD_COMPONENT,
        .entity = entity,
        .add.component = type,
    };
    uint32 index = darrayLength(buffer->commands);
    darrayPush(buffer->commands, command);
    return index;
}

typedef struct {
    bool8 found;
    bool8 exists;
    bool8 isStaged;
    StagedEntity* sEntity;
} StagedAccessResult;

static StagedAccessResult getStagedEntityForWrite(Scene scene, StagingBuffer* buffer, Entity entity, uint32* outEntityIndex){
    StagedAccessResult result = {0};

    if(entity == INVALID_ENTITY || scene == NULL || buffer == NULL) return result;

    bool8 isStaged;
    LocalEntity localEntity;
    uint32 entityIndex;

    if(!getEntityDetails(scene, entity, &entityIndex, &localEntity, NULL, &isStaged)) return result;

    result.exists = true; // entity exists
    if(outEntityIndex) *outEntityIndex = entityIndex;

    // fully staged
    if(isStaged){
        if((localEntity >> 24) != buffer->threadID) return result;
        result.found = true;
        result.isStaged = true;
        result.sEntity = &buffer->entities[localEntity & 0xffffff];
        return result;
    }

    uint32 block = entityIndex / MAPPING_BLOCK_COUNT;
    uint32 entityBlockIndex = entityIndex % MAPPING_BLOCK_COUNT;
    uint32 maskIndex  = block / MAX_COMPONENT_COUNT;
    uint32 blockBit = block % MAX_COMPONENT_COUNT;

    if(!MASK_HAS_COMPONENT(buffer->mappingBlockMask[maskIndex], blockBit)) return result;

    uint32 poolIndex = buffer->mappingBlocks[block];
    avRWLockReadLock(pool.lock);
    if(poolIndex >= pool.blockCapacity){
        avRWLockReadUnlock(pool.lock);
        return result;
    }
    EntityMappingBlock* mappingBlock = &pool.blocks[poolIndex].blocks;
    LocalEntity locEntity = mappingBlock->entityIndex[entityBlockIndex];
    avRWLockReadUnlock(pool.lock);

    if(locEntity == INVALID_ENTITY) return result;

    uint32 threadId = locEntity >> 24;
    if(threadId != buffer->threadID) return result;

    result.found = true;
    result.isStaged = false;
    result.sEntity = &buffer->entities[locEntity & 0xffffff];
    return result;
}

Entity stagedEntityCreate(Scene scene, StagingBuffer* buffer, ComponentInfoRef info){

    // create fully staged entity
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

static void entityAddComponent(Scene scene, StagingBuffer* buffer, Entity entity, StagedEntity* sEntity, ComponentType type, ComponentInfo* info){
    uint32 size = getComponentSize(type);
    ComponentConstructor constructor = getComponentConstructor(type);
    byte* data = NULL;
    StagedComponent** component =  &sEntity->components[type]->single;
    if(component==NULL){
        *component = avAllocatorAllocate(sizeof(StagedComponent), &buffer->componentHandleAllocator);
        (*component)->data = avAllocatorAllocate(size, &buffer->componentAllocator);
        (*component)->index = (uint32)-1;
        (*component)->entity = entity;
        (*component)->type = type;
        data = (*component)->data;
    }else{
        
        //add array
    }


}

bool32 stagedEntityAddComponent(Scene scene, StagingBuffer* buffer, Entity entity, ComponentInfo* info){

    uint32 entityIndex;
    StagedAccessResult res = getStagedEntityForWrite(scene, buffer, entity, &entityIndex);
    if(!res.exists) return false;

    if(!res.found){
        if((res.sEntity = registerExistingEntity(buffer, entity))==NULL) return false;
        res.isStaged = false;
        res.found = true;
        res.exists = true;
    }
    if(res.isStaged){
        // easiest scenario, just add to staged component


    }else{
        // add staged components
        while(info){
           

            // create staged component buffer




            cmdAddComponent(buffer, entity, info->type);
            info = info->next;
        }
    }

}

static void performComponentDestructor(Scene scene, StagingBuffer* buffer, Entity entity, StagedEntity* sEntity, ComponentType type, uint32 index){
    uint32 size = getComponentSize(type);
    ComponentDestructor destructor = getComponentDestructor(type);
    if(MASK_HAS_COMPONENT(sEntity->arrayMask, type)){
        StagedComponentArray* array = sEntity->components[type]->array;
        for(uint32 i = 0; i < array->count; i++){
            StagedComponent* comp = array->data[i];
            if(index != (uint32)-1 && comp->index != index) continue;
            if(comp->isClone) continue;
            if(comp->isDestroyed) return;
            if(destructor) destructor(scene, entity, comp->data, size);
            buffer->commands[comp->lastModifiedCommandIndex].type = CMD_NOP;
            comp->isDestroyed = true;
        }
    }else{
        StagedComponent* comp = sEntity->components[type]->single;
        if(index != (uint32)-1 && comp->index != index) return;
        if(comp->isClone) return;
        if(comp->isDestroyed) return;
        if(destructor) destructor(scene, entity, comp->data, size);
        buffer->commands[comp->lastModifiedCommandIndex].type = CMD_NOP;
        comp->isDestroyed = true;
    }
}


bool32 stagedEntityRemoveComponent(Scene scene, StagingBuffer* buffer, Entity entity, ComponentType type, uint32 index){
    uint32 entityIndex;
    StagedAccessResult res = getStagedEntityForWrite(scene, buffer, entity, &entityIndex);
    if(!res.found) return false;
    if(res.sEntity && MASK_HAS_COMPONENT(res.sEntity->mask, type)){
        if(MASK_HAS_COMPONENT(res.sEntity->arrayMask, type)){
            uint32 count = res.sEntity->components[type]->array->count;
            count--;
        }else{
            MASK_REMOVE_COMPONENT(res.sEntity->mask, type);
        }
        performComponentDestructor(scene, buffer, entity, res.sEntity, type, index);
    }
    cmdDestroyComponent(buffer, entity, type, index);
}

bool32 stagedEntityRemoveComponentType(Scene scene, StagingBuffer* buffer, Entity entity, ComponentType type){
    uint32 entityIndex;
    StagedAccessResult res = getStagedEntityForWrite(scene, buffer, entity, &entityIndex);
    if(!res.found) return false;
    if(res.sEntity && MASK_HAS_COMPONENT(res.sEntity->mask, type)){
        performComponentDestructor(scene, buffer, entity, res.sEntity, type, (uint32)-1);
    }
    cmdDestroyComponentType(buffer, entity, type);
}


static void performStagedComponentDestructors(Scene scene, StagingBuffer* buffer, Entity entity, StagedEntity* sEntity){
    ITERATE_MASK(sEntity->mask, component){
        performComponentDestructor(scene, buffer, entity, sEntity, component, (uint32)-1);
    }
}

bool32 stagedEntityDestroy(Scene scene, StagingBuffer* buffer, Entity entity){
    uint32 entityIndex;
    StagedAccessResult res = getStagedEntityForWrite(scene, buffer, entity, &entityIndex);
    if(!res.found) return false;
    if(res.sEntity){
        performStagedComponentDestructors(scene, buffer, entity, res.sEntity);

        if(res.isStaged){
            buffer->commands[res.sEntity->createCommandIndex].type = CMD_NOP;
            return true;
        }
    }
    cmdDestroyEntity(buffer, entity);
    return true;
}


uint32 stagedEntityGetComponentCount(Scene scene, Entity entity, ComponentType type);
void* stagedEntityGetComponentRead(Scene scene, Entity entity, ComponentType type, uint32 index);
void* stagedEntityGetComponentReadFast(Scene scene, Entity entity, ComponentType type, uint32 index);
void* stagedEntityGetComponentWrite(Scene scene, Entity entity, ComponentType type, uint32 index);
void* stagedEntityGetComponentWriteFast(Scene scene, Entity entity, ComponentType type, uint32 index);