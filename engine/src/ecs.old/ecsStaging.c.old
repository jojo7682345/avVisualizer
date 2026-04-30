#include "ecsStaging.h"

#include "containers/darray.h"
#include <AvUtils/threading/avMutex.h>





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
    if(MASK_HAS_COMPONENT(sEntity->mask, type)) return;
    MASK_ADD_COMPONENT(sEntity->mask, info->type);
    uint32 size = getComponentSize(type);
    ComponentConstructor constructor = getComponentConstructor(type);
    byte* data = NULL;
 

    struct StagedComponentList* component = avAllocatorAllocate(sizeof(struct StagedComponentList), &buffer->componentHandleAllocator);
    if(size!=0){    
        component->data.data = avAllocatorAllocate(size, &buffer->componentAllocator);
    }else{
        component->data.data = NULL;
    }
    component->data.entity = entity;
    component->data.type = type;
    
    // TODO: remove this is deprecated
    //if(constructor) constructor(scene, entity, component->data.data, size, info);

    struct StagedComponentList* list =  sEntity->components;
    if(list==NULL) sEntity->components = component;
    while(list){
        if(list->next==NULL){
            list->next = component;
            break;
        }
        list = list->next;
    }
}

Entity stagedEntityCreate(Scene scene, StagingBuffer* buffer, ComponentInfoRef infoRef){

    // create fully staged entity
    StagedEntity tmp = {0};
    uint32 entityIndex = darrayLength(buffer->entities);
    darrayPush(buffer->entities, tmp);
    StagedEntity* sEntity = &buffer->entities[entityIndex];
    LocalEntity localEntity = (buffer->threadID << 24) | (entityIndex & 0xffffff);    

    Entity entity = allocateEntityID(scene, localEntity, true);
    sEntity->ID = entity;
    ComponentInfo* info = infoRef;
    while(info){
        while(info && MASK_HAS_COMPONENT(sEntity->mask, info->type)) info = info->next;
        if(info==NULL) break;
        
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
        
        stagedEntityAddStagedComponent(scene, buffer, entity, sEntity, info->type, info);

        info = info->next;
    }
    return true;
}

static void performComponentDestructor(Scene scene, StagingBuffer* buffer, Entity entity, StagedEntity* sEntity, ComponentType type){
    uint32 size = getComponentSize(type);
    ComponentDestructor destructor = getComponentDestructor(type);
    struct StagedComponentList* compList = sEntity->components;
    struct StagedComponentList* prev = NULL;
    StagedComponent* comp = NULL;
    while(compList && compList->data.type != type){
        prev = compList;
        compList = compList->next;
    }
    if(compList==NULL) return;
    if(compList->data.type != type) return;
    comp = &compList->data;
    if(comp->isClone) return;
    if(comp->isDestroyed) return;
    if(destructor) destructor(scene, entity, comp->data, size);
    comp->isDestroyed = true;

    if(prev==NULL) {
        sEntity->components = compList->next;
    }else{
        prev->next = compList->next;
    }
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
    sEntity->ID = INVALID_ENTITY;
    return true;
}

bool32 stagingBufferCommit(Scene scene, StagingBufferHandle buffer){
    uint32 entityCount = darrayLength(buffer->entities);
    for(uint32 i = 0; i < entityCount; i++){
        StagedEntity* sEntity = &buffer->entities[i];
        if(sEntity->ID==INVALID_ENTITY) continue;
        if(!createEmptyEntity(scene, sEntity->ID, sEntity->mask)){
            return false;
        }
        
        struct StagedComponentList* component = sEntity->components;
        LocalEntity localEntity;
        
        if(!getEntityDetails(scene, sEntity->ID, NULL, &localEntity, NULL, NULL)) return false;
        EntityChunk* chunk = getLocalEntityChunk(localEntity);
        EntityType* type = getEntityType(scene, chunk->entityType);
        uint32 localIndex = getLocalEntityLocalIndex(localEntity);
        while(component){
            uint32 compIndex = getComponentIndex(type, component->data.type);
            if(compIndex == -1) return false;
            uint32 size = getComponentSize(component->data.type);
            void* data = ((byte*)chunk->components[compIndex]) + localIndex*size;
            avMemcpy(data, component->data.data, size);
            component = component->next;
        }
    }

    avAllocatorReset(&buffer->componentAllocator); // FIX this, as this also frees memory, and we need this for next frame
    avAllocatorReset(&buffer->componentHandleAllocator);
    darrayLengthSet(buffer->entities, 0);
    return true;
}


bool32 stagedEntityHasComponent(Scene scene, StagingBufferHandle buffer, Entity entity, ComponentType type){
    StagedEntity* sEntity;
    if(!getStagedEntity(scene, buffer, entity, &sEntity)) return false;
    return MASK_HAS_COMPONENT(sEntity->mask, type);
}
void* stagedEntityGetComponentRead(Scene scene, Entity entity, ComponentType type);
void* stagedEntityGetComponentReadFast(Scene scene, Entity entity, ComponentType type);
void* stagedEntityGetComponentWrite(Scene scene, Entity entity, ComponentType type);
void* stagedEntityGetComponentWriteFast(Scene scene, Entity entity, ComponentType type);