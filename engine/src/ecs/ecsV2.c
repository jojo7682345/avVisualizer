#include "ecsV2.h"

#include <AvUtils/avMemory.h>

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

static ComponentRegistry componentRegistry = {0};

typedef struct ComponentArray {
    uint32 count;
    uint32* index;
    uint32* reference;
    ComponentData* data;
} ComponentArray;

typedef union {
    ComponentData single;
    ComponentArray* array;
} Component;

#define INVALID_ENTITY_TYPE ((EntityTypeID)-1)

#define CHUNK_CAPACITY 256
typedef struct EntityChunk {
    Component components[COMPONENT_REGISTRY_SIZE];
    Entity entities[CHUNK_CAPACITY];
    EntityTypeID entityType;
    uint32 count;
    uint8 localIndex[CHUNK_CAPACITY];
} EntityChunk;



#define MAX_CHUNKS 65536
typedef struct ChunkPool {
    uint32 chunkCount;
    EntityChunk chunks[MAX_CHUNKS];
    uint32 chunkIndex[MAX_CHUNKS];
    uint32 chunkReference[MAX_CHUNKS];
} ChunkPool;

bool32 registerComponent(ComponentType* component, uint32 size, ComponentConstructor constructor, ComponentDestructor destructor){
	if(component==NULL) return false;
	//if(size==0) return false;
	if(*component==INVALID_COMPONENT) {
		bool32 found = false;
		for(uint32 i = 0; i < COMPONENT_MASK_SIZE; i++){
			for(uint32 j = 0; j < 64; j++){
				if((componentRegistry.registeredComponents.bits[i] & (1ULL << j)) != 0) continue;
				found = true;
				*component = i*64 + j;
				break;
			}
			if(found) break;
		}	
		if(!found){
			return false;
		}
	}else{
		if(*component >= MAX_COMPONENT_COUNT) return false;
		if(MASK_HAS_COMPONENT(componentRegistry.registeredComponents, *component)) return false;
	}
	MASK_ADD_COMPONENT(componentRegistry.registeredComponents, *component);
	componentRegistry.entries[*component].size = size;
	componentRegistry.entries[*component].constructor = constructor;
	componentRegistry.entries[*component].destructor = destructor;
	return true;
}

static bool32 isComponentRegistered(ComponentType component){
    return MASK_HAS_COMPONENT(componentRegistry.registeredComponents, component);
}

static inline uint32 getComponentSize(ComponentType component){
    if(!isComponentRegistered(component)) return 0;
    return componentRegistry.entries[component].size;
}

static inline ComponentConstructor getComponentConstructor(ComponentType component){
    if(!isComponentRegistered(component)) return NULL;
    return componentRegistry.entries[component].constructor;
}

static inline ComponentDestructor getComponentDestructor(ComponentType component){
    if(!isComponentRegistered(component)) return NULL;
    return componentRegistry.entries[component].destructor;
}

bool32 componentMaskContains(ComponentMask mask, ComponentMask componentMask){
    for(uint32 i = 0; i < COMPONENT_MASK_SIZE; i++) {
        if((mask.bits[i] & componentMask.bits[i]) != componentMask.bits[i]) return 0;
    }
    return 1;
}

bool32 componentMaskEquals(ComponentMask maskA, ComponentMask maskB){
    for(uint32 i = 0; i < COMPONENT_MASK_SIZE; i++){
        if(maskA.bits[i] != maskB.bits[i]) return false;
    }
    return true;
}

static ChunkPool chunkPool;
static void initChunkPool(){
    chunkPool.chunkCount = 0;

    for(uint32 i = 0; i < MAX_CHUNKS; i++){
        avMemset(chunkPool.chunks + i, 0, sizeof(EntityChunk));
        chunkPool.chunkIndex[i] = i;
        chunkPool.chunkReference[i] = i;
    }
}

uint32 allocateChunk(){
    if(chunkPool.chunkCount >= MAX_CHUNKS){
        return NULL;
    }

    uint32 index = chunkPool.chunkCount++;
    EntityChunk* chunk = &chunkPool.chunks[index];
    chunk->count = 0;
    chunk->entityType = INVALID_ENTITY_TYPE;
    return chunkPool.chunkReference[index];
}

uint32 getChunkID(EntityChunk* chunk){
    if(chunk==NULL)return MAX_CHUNKS;
    uint32 chunkIndex = chunk - chunkPool.chunks;
    if(chunkIndex >= chunkPool.chunkCount) return MAX_CHUNKS;
    return chunkPool.chunkReference[chunkIndex];
}

static EntityChunk* getChunk(uint32 chunkID){
    if(chunkID >= MAX_CHUNKS) return NULL;
    uint32 index = chunkPool.chunkIndex[chunkID];
    if(index >= chunkPool.chunkCount) return NULL;
    return chunkPool.chunks + index;
}

void freeChunk(uint32 chunkID){
    if(chunkPool.chunkCount == 0){
        return;
    }

    EntityChunk* chunk = getChunk(chunkID);
    if(chunk == NULL){
        return;
    }
    uint32 index = chunkPool.chunkIndex[index];

    uint32 lastIndex = chunkPool.chunkCount - 1;
    uint32 lastID = chunkPool.chunkReference[lastIndex];
    EntityChunk* lastChunk = &chunkPool.chunks[lastIndex]; 

    avMemcpy(chunk, lastChunk, sizeof(EntityChunk));
    chunkPool.chunkReference[index] = lastIndex;
    chunkPool.chunkReference[lastIndex] = index;
    chunkPool.chunkIndex[chunkID] = lastIndex;
    chunkPool.chunkIndex[lastID] = chunkID;
    chunkPool.chunkCount--;
}

typedef struct EntityType {
    ComponentMask mask; // order of components is specified in order of mask
    ComponentMask arrayMask;
    uint32 chunkCount;
    uint32* chunks;
    EntityTypeID typeID;
    Scene scene;
}EntityType;


struct Scene {
    uint32 entityTypeCapacity;
    uint32 entityTypeCount;
    uint32* entityTypeIndex;
    EntityTypeID* entityTypeReference;
    EntityType* entityTypes;
};

EntityType* getEntityType(Scene scene, EntityTypeID type){
    if(scene==NULL) return NULL;
    if(type >= scene->entityTypeCapacity) return NULL;
    uint32 index = scene->entityTypeIndex[type];
    if(index >= scene->entityTypeCount) return NULL;
    return scene->entityTypes + index;
}


static void increaseSceneEntityTypeCapacity(Scene scene){
    uint32 oldCapacity = scene->entityTypeCapacity;
    scene->entityTypeCapacity *= 2;
    scene->entityTypeIndex = avReallocate(scene->entityTypeIndex, sizeof(uint32)*scene->entityTypeCapacity, "");
    scene->entityTypeReference = avReallocate(scene->entityTypeReference, sizeof(EntityTypeID)*scene->entityTypeCapacity, "");
    scene->entityTypes = avReallocate(scene->entityTypes, sizeof(EntityType)*scene->entityTypeCapacity, "");
    for(uint32 i = oldCapacity; i < scene->entityTypeCapacity; i++){
        scene->entityTypeReference[i] = i;
        scene->entityTypeIndex[i] = i;
    }
}


#define ENTITY_LOCAL_INDEX(entity) ((entity) & 0xff)
#define ENTITY_CHUNK(entity) (((entity) & ((MAX_CHUNKS-1)<<8))>>8)
#define ENTITY(chunk, index) (((chunk)<<8) | ((index) & 0xff))

static uint32 createChunk(EntityType* type){
    if(type==NULL) return MAX_CHUNKS;
    ComponentMask mask = type->mask;
    ComponentMask arrayMask = type->arrayMask;

    {
        ITERATE_MASK(mask, component){
            if(!isComponentRegistered(component)){
                return MAX_CHUNKS;
            }
        }
    }
    uint32 chunkID = allocateChunk();
    EntityChunk* chunk = getChunk(chunkID);
    chunk->entityType = type->typeID;
    uint32 index = 0;
    ITERATE_MASK(mask, component){
        uint32 size = getComponentSize(component);
        bool32 isArray = false;
        if(MASK_HAS_COMPONENT(arrayMask, component)){
            size = sizeof(ComponentArray);
        }
        chunk->components[index] = (Component) { avAllocate(size * CHUNK_CAPACITY, "") };
        avMemset(chunk->components[index].single, 0, size);
        index++;
    }
    chunk->count = 0;
    
    for(uint32 i = 0; i < CHUNK_CAPACITY; i++){
        chunk->entities[i] = ENTITY(chunkID, i);
        chunk->localIndex[i] = i;
    }

    return chunkID;
}

static void destroyComponent(Scene scene, Entity entity, ComponentData data, ComponentType type){
    ComponentDestructor destructor = getComponentDestructor(type);
    if(destructor) {
        destructor(scene, entity, data, getComponentSize(type));
    }
}

static void destroyComponentArray(Scene scene, Entity entity, ComponentArray* array, ComponentType component){
    for(uint32 i = 0; i < array->count; i++){
        destroyComponent(scene, entity, array->data[i], component);
    }
    avFree(array->data);
}

static void destroyChunk(EntityType* type, uint32 chunkID){
    EntityChunk* chunk = getChunk(chunkID);
    if(chunk == NULL) return;
    if(type == NULL) return;
    
    ComponentMask mask = type->mask;
    ComponentMask arrayMask = type->mask;

    uint32 index = 0;
    ITERATE_MASK(mask, component){
        uint32 size = getComponentSize(component);
        if(MASK_HAS_COMPONENT(arrayMask, component)){
            for(uint32 i = 0; i < chunk->count; i++){
                destroyComponentArray(type->scene, chunk->entities[i], chunk->components[index].array + sizeof(ComponentArray)*i, component);
            }
        }else{
            for(uint32 i = 0; i < chunk->count; i++){
                destroyComponent(type->scene, chunk->entities[i], chunk->components[index].single, component);
            }
        }
        avFree(chunk->components[index].single);
        index++;
    }

    freeChunk(chunkID);
}

static EntityType* createEntityType(Scene scene, ComponentMask mask, ComponentMask arrayMask){

    if(scene->entityTypeCount >= scene->entityTypeCapacity){
        increaseSceneEntityTypeCapacity(scene);
    }
    uint32 index = scene->entityTypeCount++;
    EntityTypeID id = scene->entityTypeReference[index];
    avMemset(scene->entityTypes + index, 0, sizeof(EntityType));
    EntityType* type = scene->entityTypes + index;
    type->mask = mask;
    type->arrayMask = arrayMask;
    type->typeID = id;
    type->scene = scene;
    return type;
}

static bool32 destroyEntityType(EntityType* type){
    if(type==NULL) return false;
    if(type->scene==NULL) return false;

    Scene scene = type->scene;
    if(scene->entityTypeCount == 0) return false;

    EntityTypeID id = type->typeID;
    if(id >= scene->entityTypeCapacity) return false;
    uint32 index = scene->entityTypeIndex[id];
    if(index >= scene->entityTypeCount) return false;
    if(scene->entityTypes + index != type) return false;

    uint32 lastIndex = scene->entityTypeCount - 1;
    EntityTypeID lastID = scene->entityTypeReference[lastIndex];
    
    for(uint32 i = 0; i < type->chunkCount; i++){
        destroyChunk(type, type->chunks[i]);
        
    }
    if(type->chunks) avFree(type->chunks);

    avMemcpy(&scene->entityTypes[index], &scene->entityTypes[lastIndex], sizeof(EntityType));
    scene->entityTypeReference[index] = lastID;
    scene->entityTypeReference[lastIndex] = id;
    scene->entityTypeIndex[lastID] = index;
    scene->entityTypeIndex[id] = lastIndex;
    scene->entityTypeCount--;
    return true;
}

Scene sceneCreate(){
    Scene scene = avAllocate(sizeof(struct Scene), "");
    avMemset(scene, 0, sizeof(struct Scene));
    scene->entityTypeCapacity = 1;
    scene->entityTypeCount = 0;
    scene->entityTypeIndex = avAllocate(sizeof(uint32)*scene->entityTypeCapacity, "");
    scene->entityTypeReference = avAllocate(sizeof(EntityTypeID)*scene->entityTypeCapacity, "");
    scene->entityTypes = avAllocate(sizeof(EntityType)*scene->entityTypeCapacity, "");
    scene->entityTypeIndex[0] = 0;
    scene->entityTypeReference[0] = 0;
    return scene;
}

void sceneDestroy(Scene scene){

    for(uint32 i = 0; i < scene->entityTypeCount; i++){
        destroyEntityType(&scene->entityTypes[i]);
    }
    avFree(scene->entityTypes);
    avFree(scene->entityTypeReference);
    avFree(scene->entityTypeIndex);
    avFree(scene);

}


static EntityChunk* getEntityChunk(Entity entity){
    return &chunkPool.chunks[ENTITY_CHUNK(entity)];
}

static bool32 buildMasks(ComponentInfo* info, ComponentMask* mask, ComponentMask* arrayMask){
    avMemset(mask, 0, sizeof(ComponentMask));
    avMemset(arrayMask, 0, sizeof(ComponentMask));
    bool32 ret = true;
    while(info){
        if(info->type >= MAX_COMPONENT_COUNT) {
            ret = false;
            continue;
        }
        if(MASK_HAS_COMPONENT(*mask, info->type)){
            MASK_ADD_COMPONENT(*arrayMask, info->type);
        }else{
            MASK_ADD_COMPONENT(*mask, info->type);
        }
        info = info->next;
    }
    return ret;
}

static EntityType* findEntityType(Scene scene, ComponentMask mask, ComponentMask arrayMask){
    for(uint32 i = 0; i < scene->entityTypeCount; i++){
        EntityType* type = scene->entityTypes + i;
        if(componentMaskEquals(type->mask, mask) && componentMaskEquals(type->arrayMask, arrayMask)){
            return type;
        }
    }
    return NULL;
}


static bool32 swapEntities(Scene scene, Entity entityA, Entity entityB){
    if(scene==NULL) return false;
    if(entityA==entityB) return true;

    uint32 chunkIdA = ENTITY_CHUNK(entityA);
    uint32 chunkIdB = ENTITY_CHUNK(entityB);

    if(chunkIdA  != chunkIdB) return false;

    uint32 localA = ENTITY_LOCAL_INDEX(entityA);
    uint32 localB = ENTITY_LOCAL_INDEX(entityB);
    
    EntityChunk* chunk = getChunk(chunkIdA);
    
    uint32 indexA = chunkPool.chunkIndex[chunkIdA];
    uint32 indexB = chunkPool.chunkIndex[chunkIdB];
    
    if(chunk->entities[localA]!=entityA || chunk->entities[localB]!=entityB) return false;
    if(indexA >= chunk->count || indexB >= chunk->count) return false;
    
    EntityType* type = getEntityType(scene, chunk->entityType);
    if(type==NULL) return false;

    ComponentMask mask = type->mask;
    ComponentMask arrayMask = type->arrayMask;

    uint32 componentCount = 0;
    {ITERATE_MASK(mask, component){
        componentCount++;
    }}
    byte** componentBuffer = avAllocate(componentCount*sizeof(byte*), "");
    uint32 index = 0;
    ITERATE_MASK(mask, component){
        uint32 size = getComponentSize(component);
        if(MASK_HAS_COMPONENT(arrayMask, component)){
            size = sizeof(ComponentArray);
        }
        componentBuffer[index] = avAllocate(size, "");
        avMemcpy(componentBuffer[index], ((byte*)chunk->components[index].single) + localA*size, size);
        avMemcpy(((byte*)chunk->components[index].single) + localA*size, ((byte*)chunk->components[index].single) + localB*size, size);
        avMemcpy(((byte*)chunk->components[index].single) + localB*size, componentBuffer[index], size);
        index++;
    }

    //swap chunk ids
    chunk->localIndex[localA] = indexB;
    chunk->localIndex[localB] = indexA;
    
    chunk->entities[indexA] = entityB;
    chunk->entities[indexB] = entityB;
    
    return true;
}

static void performDestructor(Scene scene, uint32 i, EntityChunk* chunk, EntityType* type){
    if(scene==NULL) return;
    if(type==NULL) return;
    if(chunk==NULL) return;
    if(i >= chunk->count) return;
    ComponentMask mask = type->mask;
    ComponentMask arrayMask = type->arrayMask;

    uint32 index = 0;
    ITERATE_MASK(mask, component){
        uint32 size = getComponentSize(component);
        if(MASK_HAS_COMPONENT(arrayMask, component)){
            destroyComponentArray(type->scene, chunk->entities[i], chunk->components[index].array + sizeof(ComponentArray)*i, component);
        }else{
            destroyComponent(type->scene, chunk->entities[i], chunk->components[index].single, component);        
        }
        index++;
    }
}

static void createComponent(Scene scene, Entity entity, ComponentData data, ComponentType type){

}

static void createComponentArray(Scene scene, Entity entity, ComponentArray array, ComponentType type){
    
}

static void performConstructor(Scene scene, uint32 i, EntityChunk* chunk, EntityType* type, ComponentInfo* info){
    if(scene==NULL) return;
    if(type==NULL) return;
    if(chunk==NULL) return;
    if(i >= chunk->count) return;
    ComponentMask mask = type->mask;
    ComponentMask arrayMask = type->arrayMask;

    uint32 index = 0;
    ITERATE_MASK(mask, component){
        uint32 size = getComponentSize(component);
        avMemset(chunk->components[index].single, 0, size);
        index++;
    }

    while(info){
        ComponentType type = info->type;




        info = info->next;
    }

}

static bool32 destroyEntity(Scene scene, Entity entity){
    EntityChunk* chunk = getEntityChunk(entity);
    if(chunk==NULL) return false;
    if(chunk->count==0) return false;
    uint32 localIndex = ENTITY_LOCAL_INDEX(entity);
    if(localIndex >= CHUNK_CAPACITY) return false;
    uint32 index = chunk->localIndex[localIndex];
    if(index >= chunk->count) return false;

    EntityType* type = getEntityType(scene, chunk->entityType);
    if(type==NULL) return false;
    performDestructor(scene, index, chunk, type);
    uint32 lastIndex = chunk->count - 1;
    Entity lastEntity = ENTITY(ENTITY_CHUNK(entity), lastIndex);
    swapEntities(scene, entity, lastEntity);
    chunk->count--;
    return true;
}

static Entity createEntity(EntityType* type){
    if(type==NULL) return INVALID_ENTITY;
    if(type->chunkCount==0){
        type->chunks = avAllocate(sizeof(EntityChunk*), "");
        type->chunks[type->chunkCount++] = createChunk(type);
    }
    
    EntityChunk* chunk = getChunk(type->chunks[type->chunkCount-1]);
    if(chunk==NULL) return INVALID_ENTITY;
    if(chunk->count == CHUNK_CAPACITY){
        type->chunks = avReallocate(type->chunks, sizeof(uint32)*type->chunkCount*2, "");
        type->chunks[type->chunkCount++] = createChunk(type);
        chunk = getChunk(type->chunks[type->chunkCount-1]);
    }
    uint32 chunkID = getChunkID(chunk);
    uint32 localIndex = chunk->localIndex[chunk->count++];
    return chunk->entities[localIndex];
}



AV_API Entity entityCreate(Scene scene, ComponentInfo* info){

    ComponentMask mask = {0};
    ComponentMask arrayMask = {0};
    if(!buildMasks(info, &mask, &arrayMask)){
        return INVALID_ENTITY;
    }

    EntityType* type = findEntityType(scene, mask, arrayMask);
    if(type==NULL){
        type = createEntityType(scene, mask, arrayMask);
    }

    Entity entity = createEntity(type);



}
AV_API bool32 entityAddComponent(Scene scene, Entity entity, ComponentInfo* info){

}
AV_API bool32 entityRemoveComponent(Scene scene, Entity entity, ComponentType type, uint32 index){

}
AV_API bool32 entityRemoveComponentType(Scene scene, Entity entity, ComponentType type){

}
AV_API bool32 entityDestroy(Scene scene, Entity entity){

}

