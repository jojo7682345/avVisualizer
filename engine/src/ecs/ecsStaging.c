#include "ecsStaging.h"


typedef enum CommandType {
    CMD_CREATE_ENTTIY,
    CMD_DESTROY_ENTITY,
    CMD_ADD_COMPONENT,
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

typedef struct CommandBuffer {
    Command* data;
    uint32 count;
    uint32 capacity;
} CommandBuffer;

typedef struct StagedComponent{
    ComponentType type;
    Entity entity;
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
    Entity ID;
    union {
        StagedComponent single;
        StagedComponentArray array;   
    }* components[MAX_COMPONENT_COUNT];
}StagedEntity;



typedef struct StagingBuffer {
    uint32 capacity;
    uint32 size;
    byte* componentData;
    StagedEntity* entityData;
} StagingBuffer;


StagedEntity* getStagedEntity(Scene scene, StagingBuffer buffer, Entity entity){
    if(scene==NULL || entity==INVALID_ENTITY) return NULL;
    uint32 index = ENTITY_INDEX(entity);
    avRWLockReadLock(scene->entityIdLock);
    Entity localEntity = scene->entityTable[index];
    uint32 generation = scene->entityGeneration[index] & ~ENTITY_STAGED_BIT;
    avRWLockReadUnlock(scene->entityIdLock);
    if(generation != ENTITY_GENERATION(entity)) return NULL;
    return buffer.entityData + localEntity;
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

StagedComponent* getStagedComponent(Scene scene, StagedEntity* entity, ComponentType type, uint32 index){
    if(scene == NULL || entity == NULL || type >= MAX_COMPONENT_COUNT) return NULL;
    uint32 index = getComponentIndex(entity, type);
    if(index >= MAX_COMPONENT_COUNT) return NULL;
    if(index >= 0 && !MASK_HAS_COMPONENT(entity->arrayMask, type)) return NULL;
}


void stagedEntityAddComponent(StagedEntity* entity, ComponentInfo* info){
    if(entity == NULL) return;
    uint32 componentCounts[MAX_COMPONENT_COUNT] = {0};
    ComponentInfo* tmpInfo = info;
    while(tmpInfo){
        if(tmpInfo->type >= MAX_COMPONENT_COUNT) continue;
        componentCounts[tmpInfo->type]++;
        tmpInfo = tmpInfo->next;
    }

    uint32 componentSize = 0;
    uint32 dataSize = 0;
    for(uint32 i = 0; i < MAX_COMPONENT_COUNT; i++){
        if(componentCounts[i]==0) continue;
        dataSize += getComponentSize(i) * componentCounts[i];
        componentSize += componentCounts[i]==1 ? sizeof(StagedComponent) : sizeof(StagedComponentArray);

    }
}

void* stagingBufferGet(StagingBuffer* buffer, uint32 size){
    
}