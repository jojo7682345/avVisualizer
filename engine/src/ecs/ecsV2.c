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
    uint32 capacity;
    uint32* index;
    uint32* reference;
    ComponentData data;
} ComponentArray;

typedef union {
    ComponentData single;
    ComponentArray* array;
} Component;

#define INVALID_ENTITY_TYPE ((EntityTypeID)-1)

#define ENTITY_LOCAL_INDEX(entity) ((entity) & 0xff)
#define ENTITY_CHUNK(entity) (((entity) & ((MAX_CHUNKS-1)<<8))>>8)
#define ENTITY(chunk, index) (((chunk)<<8) | ((index) & 0xff))

#define CHUNK_CAPACITY 5 //256
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
    bool8 initialized;
} ChunkPool;

typedef struct EntityType {
    ComponentMask mask; // order of components is specified in order of mask
    ComponentMask arrayMask;
    uint32 chunkCount;
    uint32 chunkCapacity;
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

    uint32 entityCapacity;
    uint32 entityCount;
    Entity* entityTable; // SceneEntity -> ChunkEntity
    Entity* entityReference;
};

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

AV_API ComponentMask componentMaskAnd(ComponentMask maskA, ComponentMask maskB){
    ComponentMask mask = {0};
    for(uint32 i = 0; i < COMPONENT_MASK_SIZE; i++){
        mask.bits[i] = maskA.bits[i] & maskB.bits[i];
    }
    return mask;
}
AV_API ComponentMask componentMaskOr(ComponentMask maskA, ComponentMask maskB){
    ComponentMask mask = {0};
    for(uint32 i = 0; i < COMPONENT_MASK_SIZE; i++){
        mask.bits[i] = maskA.bits[i] | maskB.bits[i];
    }
    return mask;
}

AV_API ComponentMask componentMaskInvert(ComponentMask mask){
    for(uint32 i = 0; i < COMPONENT_MASK_SIZE; i++){
        mask.bits[i] = ~mask.bits[i];
    }
    return componentMaskAnd(mask, componentRegistry.registeredComponents);
}

static ChunkPool chunkPool;
static void initChunkPool(){
    chunkPool.chunkCount = 0;

    for(uint32 i = 0; i < MAX_CHUNKS; i++){
        avMemset(chunkPool.chunks + i, 0, sizeof(EntityChunk));
        chunkPool.chunkIndex[i] = i;
        chunkPool.chunkReference[i] = i;
    }
    chunkPool.initialized = true;
}

uint32 allocateChunk(Scene scene){
    if(chunkPool.initialized==false){
        initChunkPool();
    }
    if(chunkPool.chunkCount >= MAX_CHUNKS){
        return NULL;
    }

    uint32 index = chunkPool.chunkCount++;
    EntityChunk* chunk = &chunkPool.chunks[index];
    chunk->count = 0;
    chunk->entityType = INVALID_ENTITY_TYPE;

    uint32 chunkID = chunkPool.chunkReference[index];

    if(scene->entityCount + CHUNK_CAPACITY >= scene->entityCapacity){
        uint32 oldCapacity = scene->entityCapacity;
        scene->entityCapacity += CHUNK_CAPACITY;
        scene->entityTable = avReallocate(scene->entityTable, sizeof(Entity) * scene->entityCapacity, "");
        for(uint32 i = oldCapacity; i < scene->entityCapacity; i++){
            scene->entityTable[i] = ENTITY(chunkID, i % CHUNK_CAPACITY);
        }
    }

    return chunkID;
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

static EntityChunk* getEntityChunk(Scene scene, Entity entity){
    if(scene==NULL || entity==INVALID_ENTITY) return NULL;
    return &chunkPool.chunks[ENTITY_CHUNK(scene->entityTable[entity])];
}

static uint32 getEntityLocalIndex(Scene scene, Entity entity){
    return ENTITY_LOCAL_INDEX(scene->entityTable[entity]);
}

static Entity getEntity(uint32 chunkID, uint32 localIndex){
    return getChunk(chunkID)->entities[localIndex];
}

void freeChunk(uint32 chunkID){
    if(chunkPool.chunkCount == 0){
        return;
    }

    EntityChunk* chunk = getChunk(chunkID);
    if(chunk == NULL){
        return;
    }
    uint32 index = chunkPool.chunkIndex[chunkID];

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

EntityType* getEntityType(Scene scene, EntityTypeID type){
    if(scene==NULL) return NULL;
    if(type >= scene->entityTypeCapacity) return NULL;
    uint32 index = scene->entityTypeIndex[type];
    if(index >= scene->entityTypeCount) return NULL;
    return scene->entityTypes + index;
}

EntityType* getType(Scene scene, Entity entity){
    EntityChunk* chunk = getEntityChunk(scene, entity);
    if(chunk==NULL) return NULL;
    return getEntityType(scene, chunk->entityType);
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
    uint32 chunkID = allocateChunk(type->scene);
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
        chunk->entities[i] = type->scene->entityCount++;
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
        destroyComponent(scene, entity, (byte*)array->data + i*getComponentSize(component), component);
    }
    avFree(array->data);
}

static void destroyChunk(EntityType* type, uint32 chunkID){
    EntityChunk* chunk = getChunk(chunkID);
    if(chunk == NULL) return;
    if(type == NULL) return;
    
    ComponentMask mask = type->mask;
    ComponentMask arrayMask = type->arrayMask;

    uint32 index = 0;
    ITERATE_MASK(mask, component){
        uint32 size = getComponentSize(component);
        if(MASK_HAS_COMPONENT(arrayMask, component)){
            for(uint32 i = 0; i < chunk->count; i++){
                destroyComponentArray(type->scene, chunk->entities[i], (ComponentArray*)(((byte*)chunk->components[index].array) + sizeof(ComponentArray)*i), component);
            }
        }else{
            for(uint32 i = 0; i < chunk->count; i++){
                destroyComponent(type->scene, chunk->entities[i], (byte*)chunk->components[index].single + size*i, component);
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

    while(scene->entityTypeCount!=0){
        // as types are swapped after destruction the we can keep destroying the first element
        destroyEntityType(&scene->entityTypes[0]);
    }
    avFree(scene->entityTypes);
    avFree(scene->entityTypeReference);
    avFree(scene->entityTypeIndex);
    avFree(scene);

}

static bool32 buildMasks(ComponentInfo* info, ComponentMask* mask, ComponentMask* arrayMask){
    avMemset(mask, 0, sizeof(ComponentMask));
    avMemset(arrayMask, 0, sizeof(ComponentMask));
    bool32 ret = true;
    uint32 maxCount = 4096;
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
        maxCount--;
        if(maxCount==0) return false;
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

    EntityChunk* chunkA = getEntityChunk(scene, entityA);
    EntityChunk* chunkB = getEntityChunk(scene, entityB);

    if(chunkA != chunkB) return false;

    uint32 localA = getEntityLocalIndex(scene, entityA);
    uint32 localB = getEntityLocalIndex(scene, entityB);
    
    EntityChunk* chunk = chunkA;
    
    uint32 indexA = chunk->localIndex[localA];
    uint32 indexB = chunk->localIndex[localB];
    
    if(chunk->entities[indexA]!=entityA || chunk->entities[indexB]!=entityB) return false;
    if(indexA >= chunk->count || indexB >= chunk->count) return false;
    
    EntityType* type = getEntityType(scene, chunk->entityType);
    if(type==NULL) return false;

    ComponentMask mask = type->mask;
    ComponentMask arrayMask = type->arrayMask;

    uint32 maxSize = sizeof(ComponentArray);
    {ITERATE_MASK(mask, component){
        if(getComponentSize(component) > maxSize){
            maxSize = getComponentSize(component);
        }
    }}
    byte* componentBuffer = avAllocate(maxSize, "");
    uint32 index = 0;
    ITERATE_MASK(mask, component){
        uint32 size = getComponentSize(component);
        if(MASK_HAS_COMPONENT(arrayMask, component)){
            size = sizeof(ComponentArray);
        }
        avMemcpy(componentBuffer, ((byte*)chunk->components[index].single) + indexA*size, size);
        avMemcpy(((byte*)chunk->components[index].single) + indexA*size, ((byte*)chunk->components[index].single) + indexB*size, size);
        avMemcpy(((byte*)chunk->components[index].single) + indexB*size, componentBuffer, size);
        index++;
    }
    avFree(componentBuffer);

    //swap chunk ids
    chunk->localIndex[localA] = indexB;
    chunk->localIndex[localB] = indexA;
    
    chunk->entities[indexA] = entityB;
    chunk->entities[indexB] = entityA;

    //no need to swap entityTable as we have already swapped the localindex
    
    return true;
}

static uint32 getComponentIndex(ComponentMask mask, ComponentType type){
    uint32 index = 0;
    ITERATE_MASK(mask, component){
        if(component == type) return index;
        index++;
    }
    return INVALID_COMPONENT;
}

static bool32 moveEntity(Scene scene, Entity src, Entity dst){
    if(scene==NULL) return false;
    if(src==INVALID_ENTITY || dst==INVALID_ENTITY) return false;
    // move src to the end of the chunk
    // copy data from src to dst
    // skip entries not in either mask (constructors/destructors should be handled by calling function)
    EntityChunk* srcChunk = getEntityChunk(scene, src);
    EntityChunk* dstChunk = getEntityChunk(scene, dst);

    if(srcChunk==dstChunk){
        // move within same chunk
        return swapEntities(scene, src, dst);
    }

    if(srcChunk==NULL) return false;
    if(srcChunk->count==0) return false;
    uint32 srcLocalIndex = getEntityLocalIndex(scene, src);
    if(srcLocalIndex >= CHUNK_CAPACITY) return false;
    uint32 srcIndex = srcChunk->localIndex[srcLocalIndex];
    if(srcIndex >= srcChunk->count) return false;

    if(dstChunk==NULL) return false;
    if(dstChunk->count==0) return false;
    uint32 dstLocalIndex = getEntityLocalIndex(scene, dst);
    if(dstLocalIndex >= CHUNK_CAPACITY) return false;
    uint32 dstIndex = dstChunk->localIndex[dstLocalIndex];
    if(dstIndex >= dstChunk->count) return false;

    EntityType* srcType = getEntityType(scene, srcChunk->entityType);
    if(srcType==NULL) return false;
    EntityType* dstType = getEntityType(scene, dstChunk->entityType);
    if(dstType==NULL) return false;
    
    uint32 lastIndex = srcChunk->count - 1;
    Entity lastEntity = getEntity(getChunkID(srcChunk), lastIndex);
    if(!swapEntities(scene, src, lastEntity)) return false;
    srcChunk->count--;
    
    ComponentMask copyMask = componentMaskAnd(srcType->mask, dstType->mask);
    ComponentMask arrayCopyMask = componentMaskAnd(srcType->arrayMask, dstType->arrayMask);
    
    ITERATE_MASK(copyMask, component){
        uint32 size = getComponentSize(component);
        uint32 componentSrcIndex = getComponentIndex(srcType->mask, component);
        uint32 componentDstIndex = getComponentIndex(dstType->mask, component);

        if(MASK_HAS_COMPONENT(arrayCopyMask, component)){
            size = sizeof(ComponentArray);
        }
        avMemcpy(((byte*)dstChunk->components[componentDstIndex].single) + dstIndex*size, ((byte*)dstChunk->components[componentDstIndex].single) + srcIndex*size, size);
    }

    dstChunk->entities[dstIndex] = src;
    srcChunk->entities[srcIndex] = dst;

    scene->entityTable[src] = ENTITY(getChunkID(dstChunk), dstLocalIndex);
    scene->entityTable[dst] = ENTITY(getChunkID(srcChunk), srcLocalIndex);
    return true;
}

static void performDestructorMasked(Scene scene, uint32 i, EntityChunk* chunk, EntityType* type, ComponentMask mask){
    if(scene==NULL) return;
    if(type==NULL) return;
    if(chunk==NULL) return;
    if(i >= chunk->count) return;
    mask = componentMaskAnd(type->mask, mask);
    ComponentMask arrayMask = type->arrayMask;

    uint32 index = 0;
    ITERATE_MASK(mask, component){
        uint32 size = getComponentSize(component);
        if(MASK_HAS_COMPONENT(arrayMask, component)){
            destroyComponentArray(type->scene, chunk->entities[i], (ComponentArray*)((byte*)chunk->components[index].array + sizeof(ComponentArray)*i), component);
        }else{
            destroyComponent(type->scene, chunk->entities[i], (byte*)chunk->components[index].single + i*size, component);        
        }
        index++;
    }
}

static void performDestructor(Scene scene, uint32 i, EntityChunk* chunk, EntityType* type){
    if(type==NULL) return;
    performDestructorMasked(scene, i, chunk, type, type->mask);
}

static void createComponent(Scene scene, Entity entity, ComponentData data, ComponentType type, ComponentInfo* info){
  
    ComponentConstructor constructor = getComponentConstructor(type);
    uint32 size = getComponentSize(type);
    if(constructor){
        constructor(scene, entity, data, size, info);
    }
}

static void createArrayComponent(Scene scene, Entity entity, ComponentArray* array, ComponentType type, ComponentInfo* info){
    uint32 size = getComponentSize(type);
    if(array->data==NULL || array->capacity==0){
        array->count = 0;
        array->capacity = 2; // initial size of component array, as in creation at least 2 are always used
        array->data = avAllocate(size * array->capacity, "");
        array->index = avAllocate(sizeof(uint32)*array->capacity, "");
        array->reference = avAllocate(sizeof(uint32)*array->capacity, "");
        array->index[0] = 0;
        array->index[1] = 1;
        array->reference[0] = 0;
        array->reference[1] = 1;
    }
    if(array->count >= array->capacity){
        array->capacity *= 2;
        array->data = avReallocate(array->data, size * array->capacity, "");
        array->index = avReallocate(array->index, sizeof(uint32)*array->capacity, "");
        array->reference = avReallocate(array->reference, sizeof(uint32)*array->capacity, "");
        for(uint32 i = array->capacity>>1; i < array->capacity; i++){
            array->index[i] = i;
            array->reference[i] = i;
        }
    }

    uint32 index = array->index[array->count++];
    createComponent(scene, entity, (byte*)array->data + index*size, type, info);
}
static void performConstructorMasked(Scene scene, uint32 i, EntityChunk* chunk, EntityType* type, ComponentMask mask, ComponentInfo* info){
    if(scene==NULL || type==NULL || chunk==NULL) return;
    if(i >= chunk->count) return;

    mask = componentMaskAnd(type->mask, mask);
    ComponentMask arrayMask = type->arrayMask;

    uint32 entityIndex = chunk->localIndex[i];

    uint32 index = 0;
    ITERATE_MASK(mask, component){
        uint32 size = getComponentSize(component);
        if(MASK_HAS_COMPONENT(arrayMask, component)) {
            size = sizeof(ComponentArray);
        }
        avMemset((byte*)chunk->components[index].single + size * entityIndex, 0, size);
        index++;
    }

    while(info){
        ComponentType comp = info->type;

        if(!MASK_HAS_COMPONENT(type->mask, comp)){
            info = info->next;
            continue;
        }

        uint32 compIndex = getComponentIndex(type->mask, comp);
        if(compIndex == INVALID_COMPONENT){
            info = info->next;
            continue;
        }

        uint32 size = getComponentSize(comp);
        byte* base = (byte*) chunk->components[compIndex].single;
        
        void* dst = base + entityIndex * size;
        if(MASK_HAS_COMPONENT(arrayMask, comp)){
            createArrayComponent(scene, chunk->entities[entityIndex], (ComponentArray*)dst, comp, info);
        }else{
            createComponent(scene, chunk->entities[entityIndex], dst, comp, info);
        }

        info = info->next;
    }
}
static void performConstructor(Scene scene, uint32 i, EntityChunk* chunk, EntityType* type, ComponentInfo* info){
    if(type==NULL) return;
    performConstructorMasked(scene, i, chunk, type, type->mask, info);

}

static bool32 destroyEntity(Scene scene, Entity entity){
    EntityChunk* chunk = getEntityChunk(scene, entity);
    if(chunk==NULL) return false;
    if(chunk->count==0) return false;
    uint32 localIndex = getEntityLocalIndex(scene, entity);
    if(localIndex >= CHUNK_CAPACITY) return false;
    uint32 index = chunk->localIndex[localIndex];
    if(index >= chunk->count) return false;

    EntityType* type = getEntityType(scene, chunk->entityType);
    if(type==NULL) return false;
    performDestructor(scene, index, chunk, type);
    uint32 lastIndex = chunk->count - 1;
    Entity lastEntity = getEntity(getChunkID(chunk), lastIndex);
    if(swapEntities(scene, entity, lastEntity)==false) return false;
    chunk->count--;
    return true;
}

static Entity createEntity(EntityType* type){
    if(type==NULL) return INVALID_ENTITY;
    if(type->chunkCount==0){
        type->chunks = avAllocate(sizeof(EntityChunk*), "");
        type->chunks[type->chunkCount++] = createChunk(type);
    }
    
    EntityChunk* chunk = NULL;
    for(uint32 i = 0; i < type->chunkCount; i++){
        EntityChunk* chnk = getChunk(type->chunks[i]);
        if(chnk==NULL) continue;
        if(chnk->count < CHUNK_CAPACITY){
            chunk = chnk;
            break;
        }
    }

    if(chunk==NULL) return INVALID_ENTITY;
    if(chunk->count == CHUNK_CAPACITY){
        type->chunkCapacity *= 2;
        type->chunks = avReallocate(type->chunks, sizeof(uint32)*type->chunkCapacity, "");
        type->chunks[type->chunkCount++] = createChunk(type);
        chunk = getChunk(type->chunks[type->chunkCount-1]);
    }
    uint32 chunkID = getChunkID(chunk);
    uint32 localIndex = chunk->count++;
    return chunk->entities[localIndex];
}

static bool32 isComponentInfoCompatible(ComponentMask mask, ComponentMask isArray, ComponentInfo* info){
    ComponentMask createMask = {0};
    ComponentMask createArrayMask = {0};
    if(!buildMasks(info, &createMask, &createArrayMask)) return false;

    if(!componentMaskContains(componentMaskOr(mask, isArray), createMask)) return false;

    if(!componentMaskContains(isArray, createArrayMask)) return false;

    return true;
}

static bool32 entityChangeType(Scene scene, Entity entity, EntityType* dst, ComponentInfo* info){
    if(scene==NULL) return false;
    if(entity==INVALID_ENTITY) return false;
    if(dst==NULL) return false;
    
    EntityType* src = getType(scene, entity);
    if(src==dst) return true;

    if(componentMaskEquals(src->mask, dst->mask) && componentMaskEquals(src->arrayMask, dst->arrayMask)){
        return true; //nothing needs to be done
    }

    ComponentMask transitionMask = componentMaskAnd(src->mask, dst->mask);
    ComponentMask createMask = componentMaskAnd(componentMaskInvert(transitionMask), dst->mask);
    ComponentMask destroyMask = componentMaskAnd(componentMaskInvert(transitionMask), src->mask);

    
    if(!isComponentInfoCompatible(createMask, dst->arrayMask, info)){
        return false;
    }

    
    Entity dstEntity = createEntity(dst);
    if(dstEntity==INVALID_ENTITY) return false;
    
    performDestructorMasked(scene, getEntityLocalIndex(scene, entity), getEntityChunk(scene, entity), src, destroyMask);
    moveEntity(scene, entity, dstEntity);
    performConstructorMasked(scene, getEntityLocalIndex(scene, entity), getEntityChunk(scene, entity), dst,  componentMaskAnd(createMask, componentMaskInvert(dst->arrayMask)), info);

    return true;
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
        if(type==NULL){
            return INVALID_ENTITY;
        }
    }


    Entity entity = createEntity(type);

    performConstructor(scene, getEntityLocalIndex(scene, entity), getEntityChunk(scene, entity), type, info);

    return entity;
}

AV_API bool32 entityAddComponent(Scene scene, Entity entity, ComponentInfo* info){
    EntityType* type = getType(scene, entity);
    if(type==NULL){
        return false;
    }

    ComponentMask mask = {0};
    ComponentMask arrayMask = {0};
    if(!buildMasks(info, &mask, &arrayMask)){
        return false;
    }
    if(componentMaskEquals(mask, (ComponentMask){0})){
        return false; // cannot add none components
    }

    ComponentMask typeMask = type->mask;
    ComponentMask typeArrayMask = type->arrayMask;
    ComponentMask newArrayMask = componentMaskOr(typeArrayMask, arrayMask);
    newArrayMask = componentMaskOr(newArrayMask, componentMaskAnd(typeMask, mask));
    ComponentMask newMask = componentMaskOr(typeMask, mask);

    if(componentMaskEquals(mask, typeArrayMask)){
        performConstructor(scene, getEntityLocalIndex(scene, entity), getEntityChunk(scene, entity), type, info);
        return true; // only array items are added, no new type is required
    }

    type = findEntityType(scene, newMask, newArrayMask);
    if(type==NULL){
        type = createEntityType(scene, newMask, newArrayMask);
        if(type==NULL){
            return false;
        }
    }
    return entityChangeType(scene, entity, type, info);
}

AV_API bool32 entityRemoveComponent(Scene scene, Entity entity, ComponentType type, uint32 index){




}

AV_API bool32 entityRemoveComponentType(Scene scene, Entity entity, ComponentType type){

}

AV_API bool32 entityDestroy(Scene scene, Entity entity){
    if(scene==NULL) return false;
    if(entity==INVALID_ENTITY) return false;

    return destroyEntity(scene, entity);
}

