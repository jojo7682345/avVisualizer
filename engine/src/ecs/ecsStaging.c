#include "ecsStaging.h"

#include "containers/darray.h"
#include <AvUtils/threading/avMutex.h>

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
    StagedComponent* components[MAX_COMPONENT_COUNT];
}StagedEntity;

#define MAPPING_RANGE_COUNT (4)
#define MAPPING_BLOCK_COUNT (MAX_COMPONENT_COUNT*MAPPING_RANGE_COUNT)
typedef struct StagingBuffer {
    uint8 threadID;
    AvAllocator componentAllocator;
    StagedEntity* entities;
    AvAllocator componentHandleAllocator;
    
} StagingBuffer;


StagingBuffer* stagingBufferCreate(Scene scene){
    
    StagingBuffer* buffer = avAllocate(sizeof(StagingBuffer), "");
    avMemset(buffer, 0, sizeof(StagingBuffer));

    avAllocatorCreate(0, AV_ALLOCATOR_TYPE_DYNAMIC, &buffer->componentAllocator);
    avAllocatorCreate(0, AV_ALLOCATOR_TYPE_DYNAMIC, &buffer->componentHandleAllocator);

    buffer->entities = darrayCreate(StagedEntity);

    return buffer;
}

void stagingBufferDestroy(Scene scene, StagingBufferHandle buffer){
    if(darrayLength(buffer->entities)){
        return; // entities are still in use
    }

    avAllocatorDestroy(&buffer->componentAllocator);
    avAllocatorDestroy(&buffer->componentHandleAllocator);
    darrayDestroy(buffer->entities);
}

static bool32 getStagedEntity(Scene scene, StagingBuffer* buffer, Entity entity, StagedEntity** sEntity){
    if(entity == INVALID_ENTITY || scene == NULL || buffer == NULL) return false;

    bool8 isStaged;
    LocalEntity localEntity;
    uint32 entityIndex;

    if(!getEntityDetails(scene, entity, &entityIndex, &localEntity, NULL, &isStaged)) return false;

    if(!isStaged) return false;

    if((localEntity >> 24) != buffer->threadID) return false;
    *sEntity = &buffer->entities[localEntity & 0xffffff];
    return true;
}

static void stagedEntityAddStagedComponent(Scene scene, StagingBuffer* buffer, Entity entity, StagedEntity* sEntity, ComponentType type, ComponentInfo* info){
    uint32 size = getComponentSize(type);
    ComponentConstructor constructor = getComponentConstructor(type);
    byte* data = NULL;
    StagedComponent** component =  &sEntity->components[type];
    if((*component)!=NULL) return;

    *component = avAllocatorAllocate(sizeof(StagedComponent), &buffer->componentHandleAllocator);
    if(size!=0){    
        (*component)->data = avAllocatorAllocate(size, &buffer->componentAllocator);
    }else{
        (*component)->data = NULL;
    }
    (*component)->entity = entity;
    (*component)->type = type;
    data = (*component)->data;
    
    if(constructor) constructor(scene, entity, data, size, info);
}

Entity stagedEntityCreate(Scene scene, StagingBuffer* buffer, ComponentInfoRef infoRef){

    // create fully staged entity
    StagedEntity tmp = {0};
    uint32 entityIndex = darrayLength(buffer->entities);
    darrayPush(buffer->entities, tmp);
    StagedEntity* sEntity = &buffer->entities[entityIndex];
    LocalEntity localEntity = (buffer->threadID << 24) | (entityIndex & 0xffffff);    

    Entity entity = allocateEntityID(scene, localEntity, true);
    ComponentInfo* info = infoRef;
    while(info){
        while(info && MASK_HAS_COMPONENT(sEntity->mask, info->type)) info = info->next;
        if(info==NULL) break;
        MASK_ADD_COMPONENT(sEntity->mask, info->type);
        
        stagedEntityAddStagedComponent(scene, buffer, entity, sEntity, info->type, info);

        info = info->next;
    }

    return entity;
}



bool32 stagedEntityAddComponent(Scene scene, StagingBuffer* buffer, Entity entity, ComponentInfo* info){

    StagedEntity* sEntity;
    if(!getStagedEntity(scene, buffer, entity, &sEntity)) return false;
    
    while(info){
        while(info && MASK_HAS_COMPONENT(sEntity->mask, info->type)) info = info->next;
        if(info==NULL) break;
        MASK_ADD_COMPONENT(sEntity->mask, info->type);
        
        stagedEntityAddStagedComponent(scene, buffer, entity, sEntity, info->type, info);

        info = info->next;
    }
    return true;
}

static void performComponentDestructor(Scene scene, StagingBuffer* buffer, Entity entity, StagedEntity* sEntity, ComponentType type){
    uint32 size = getComponentSize(type);
    ComponentDestructor destructor = getComponentDestructor(type);
    StagedComponent* comp = sEntity->components[type];
    if(comp->isClone) return;
    if(comp->isDestroyed) return;
    if(destructor) destructor(scene, entity, comp->data, size);
    comp->isDestroyed = true;
}


bool32 stagedEntityRemoveComponent(Scene scene, StagingBuffer* buffer, Entity entity, ComponentType type){
    StagedEntity* sEntity;
    if(!getStagedEntity(scene, buffer, entity, &sEntity)) return false;
    if(MASK_HAS_COMPONENT(sEntity->mask, type)){
        MASK_REMOVE_COMPONENT(sEntity->mask, type);
        performComponentDestructor(scene, buffer, entity, sEntity, type);
        return true;
    }
    return false;
}


static void performStagedComponentDestructors(Scene scene, StagingBuffer* buffer, Entity entity, StagedEntity* sEntity){
    ITERATE_MASK(sEntity->mask, component){
        performComponentDestructor(scene, buffer, entity, sEntity, component);
    }
}

bool32 stagedEntityDestroy(Scene scene, StagingBuffer* buffer, Entity entity){
    StagedEntity* sEntity;
    if(!getStagedEntity(scene, buffer, entity, &sEntity)) return false;
    performStagedComponentDestructors(scene, buffer, entity, sEntity);
    freeEntityID(scene, entity); // the id can be returned as it shouldn't be used after this anymore
    return true;
}

bool32 stagingBufferCommit(Scene scene, StagingBufferHandle buffer){




    avAllocatorReset(&buffer->componentAllocator); // FIX this, as this also frees memory, and we need this for next frame
    avAllocatorReset(&buffer->componentHandleAllocator);
    darrayLengthSet(buffer->entities, 0);
}


uint32 stagedEntityHasComponent(Scene scene, Entity entity, ComponentType type);
void* stagedEntityGetComponentRead(Scene scene, Entity entity, ComponentType type);
void* stagedEntityGetComponentReadFast(Scene scene, Entity entity, ComponentType type);
void* stagedEntityGetComponentWrite(Scene scene, Entity entity, ComponentType type);
void* stagedEntityGetComponentWriteFast(Scene scene, Entity entity, ComponentType type);