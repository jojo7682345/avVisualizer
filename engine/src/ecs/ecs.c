#include "ecsInternal.h"
#include "ecsStaging.h"


// the slot reserved is set to ENTITY_ID_RESERVED
// and should be imediately set to the correct value, 
// as the id is not checked for the reserved case (will segfault)
static Entity allocateEntityID(Scene scene){
    // allocate more space if needed
    avRWLockReadLock(scene->entityIdLock);
    if(scene->entityCount >= scene->entityCapacity){
        avRWLockReadUnlock(scene->entityIdLock);
        avRWLockWriteLock(scene->entityIdLock);
        if(scene->entityCount >= scene->entityCapacity){
            uint32 oldCapacity = scene->entityCapacity;
            scene->entityCapacity *= 2;
            scene->entityTable = avReallocate(scene->entityTable, sizeof(Entity) * scene->entityCapacity, "");
            scene->entityGeneration = avReallocate(scene->entityGeneration, sizeof(uint8)*scene->entityCapacity, "");
            for(uint32 i = oldCapacity; i < scene->entityCapacity; i++){
                scene->entityTable[i] = INVALID_ENTITY;
                scene->entityGeneration[i] = (uint8)~(ENTITY_STAGED_BIT>>24);
            }
        }
        avRWLockWriteUnlock(scene->entityIdLock);
    }else{
        avRWLockReadUnlock(scene->entityIdLock);
    }
    Entity entity = INVALID_ENTITY;
    avRWLockReadLock(scene->entityIdLock);
    uint32 i = 0;
    uint32 entityCount = scene->entityCount;
    for(uint32 checkCount = 0; checkCount < scene->entityCapacity; checkCount++){
        // start at the end of the components, as there is bound to be space ther, 
        // and only wrap back around if there isn't
        i = (checkCount + entityCount) % scene->entityCapacity; 
        if(scene->entityTable[i]==INVALID_ENTITY){
            Entity id = atomic_exchange(scene->entityTable + i, ENTITY_ID_RESERVED);
            if(id==INVALID_ENTITY){
                // we have clamed the slot
                entity = i;
                atomic_fetch_add(&scene->entityCount, 1);
                break;
            }else{
                // restore the slot, this should be fine as contention only happens when the id is reserved, and not yet used
                Entity newId = id;
                do{
                    id = newId;
                    newId = atomic_exchange(scene->entityTable + i, id);
                } while(newId != id);
            }
        }
    }
    avRWLockReadUnlock(scene->entityIdLock);
    return entity;
}

static void freeEntityID(Scene scene, Entity entity){
    avRWLockReadLock(scene->entityIdLock);
    atomic_exchange(scene->entityTable + entity, INVALID_ENTITY);
    atomic_fetch_sub(&scene->entityCount, 1);
    avRWLockReadUnlock(scene->entityIdLock);
}

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

uint32 getComponentSize(ComponentType component){
    if(!isComponentRegistered(component)) return 0;
    return componentRegistry.entries[component].size;
}

ComponentConstructor getComponentConstructor(ComponentType component){
    if(!isComponentRegistered(component)) return NULL;
    return componentRegistry.entries[component].constructor;
}

ComponentDestructor getComponentDestructor(ComponentType component){
    if(!isComponentRegistered(component)) return NULL;
    return componentRegistry.entries[component].destructor;
}

AV_API bool32 componentMaskContains(ComponentMask mask, ComponentMask componentMask){
    for(uint32 i = 0; i < COMPONENT_MASK_SIZE; i++) {
        if((mask.bits[i] & componentMask.bits[i]) != componentMask.bits[i]) return 0;
    }
    return 1;
}

AV_API bool32 componentMaskEquals(ComponentMask maskA, ComponentMask maskB){
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

AV_API uint32 componentMaskCount(ComponentMask mask){
    uint32 count = 0;
    for(uint32 i = 0; i < COMPONENT_MASK_SIZE; i++){
        count += __builtin_popcountll(mask.bits[i]);
    }
    return count;
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

static uint32 allocateChunk(Scene scene){
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

    return chunkPool.chunkReference[index];
}

static uint32 getChunkID(EntityChunk* chunk){
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

static EntityChunk* getLocalEntityChunk(Entity localEntity){
    if(localEntity==INVALID_ENTITY || localEntity==ENTITY_ID_RESERVED || localEntity & ENTITY_STAGED_BIT) return NULL;
    return &chunkPool.chunks[ENTITY_CHUNK(localEntity)];
}

static EntityChunk* getEntityChunk(Scene scene, Entity entity){
    if(scene==NULL || entity==INVALID_ENTITY) return NULL;
    uint32 index = ENTITY_INDEX(entity);
    avRWLockReadLock(scene->entityIdLock);
    Entity localEntity = scene->entityTable[index];
    uint32 generation = scene->entityGeneration[index] & ~ENTITY_STAGED_BIT;
    avRWLockReadUnlock(scene->entityIdLock);
    if(generation !=  (ENTITY_GENERATION(entity) & ~ENTITY_STAGED_BIT)) return NULL;
    return getLocalEntityChunk(localEntity);
}

static uint32 getLocalEntityLocalIndex(Entity localEntity){
    if(localEntity==INVALID_ENTITY || localEntity==ENTITY_ID_RESERVED || localEntity & ENTITY_STAGED_BIT) return 0;
    return ENTITY_LOCAL_INDEX(localEntity);
}

static uint32 getEntityLocalIndex(Scene scene, Entity entity){
    if(scene==NULL || entity==INVALID_ENTITY) return NULL;
    uint32 index = ENTITY_INDEX(entity);
    avRWLockReadLock(scene->entityIdLock);
    Entity localEntity = scene->entityTable[index];
    uint32 generation = scene->entityGeneration[index] & ~ENTITY_STAGED_BIT;
    avRWLockReadUnlock(scene->entityIdLock);
    if(generation != (ENTITY_GENERATION(entity) & ~ENTITY_STAGED_BIT)) return NULL;
    return getLocalEntityLocalIndex(localEntity);
}

static Entity getEntity(uint32 chunkID, uint32 localIndex){
    return getChunk(chunkID)->entities[localIndex];
}

static void freeChunk(uint32 chunkID){
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
        chunk->components[index] = (Component) { avAllocate((size!=0 ? size*CHUNK_CAPACITY : 1), "") };
        avMemset(chunk->components[index].single, 0, size);
        index++;
    }
    chunk->count = 0;
    
    for(uint32 i = 0; i < CHUNK_CAPACITY; i++){
        chunk->entities[i] = INVALID_ENTITY;
        chunk->localIndex[i] = i;
        chunk->localID[i] = i;
    }

    return chunkID;
}

static void destroyComponent(Scene scene, Entity entity, ComponentData data, ComponentType type){
    ComponentDestructor destructor = getComponentDestructor(type);
    if(destructor) {
        destructor(scene, entity, data, getComponentSize(type));
    }
}

static void deinitComponentArray(ComponentArray* array){
    avFree(array->data);
    avFree(array->index);
    //avFree(array->reference);
}

static void destroyComponentArray(Scene scene, Entity entity, ComponentArray* array, ComponentType component){
    for(uint32 i = 0; i < array->count; i++){
        destroyComponent(scene, entity, (byte*)array->data + i*getComponentSize(component), component);
    }
    deinitComponentArray(array);
}


/*
6
[0][1][2][3][4][5]
[a][b][c][d][e][f]

remove 0

5
[1][2][3][4][0][5]
[f][b][c][d][e][a]
*/

static void freeComponentArraySlot(ComponentArray* array, ComponentType component, uint32 index){
    if(index >= array->count) return;
    if(array == NULL) return;
    if(array->count == 0) return;
    uint32 size = getComponentSize(component);

    uint32 localIndex = array->index[index];

    uint32 lastIndex = array->count-1;

    if(localIndex!=lastIndex){
        byte* srcOffset = (byte*)array->data + size*lastIndex;
        byte* dstOffset = (byte*) array->data + size*localIndex;
        avMemcpy(dstOffset, srcOffset, size);

        for(uint32 i = index; i < lastIndex - 1; i++){
            array->index[i] = array->index[i+1];
        }
        array->index[lastIndex-1] = localIndex;
    }
    array->count--;

}

static void destroyArrayComponent(Scene scene, Entity entity, ComponentArray* array, ComponentType component, uint32 index){
    if(array==NULL) return;
    if(index >= array->count) return;
    uint32 size = getComponentSize(component);

    uint32 localIndex = array->index[index];
    destroyComponent(scene, entity, (byte*)array->data + localIndex*size, component);

    freeComponentArraySlot(array, component, index);

}

static void removeEntityType(EntityType* type){
    if(type==NULL) return;
    if(type->scene==NULL) return;
    Scene scene = type->scene;
    if(scene->entityTypeCount == 0) return;

    EntityTypeID id = type->typeID;
    if(id >= scene->entityTypeCapacity) return;
    uint32 index = scene->entityTypeIndex[id];
    if(index >= scene->entityTypeCount) return;
    if(scene->entityTypes + index != type) return;

    uint32 lastIndex = scene->entityTypeCount - 1;
    EntityTypeID lastID = scene->entityTypeReference[lastIndex];

    avMemcpy(&scene->entityTypes[index], &scene->entityTypes[lastIndex], sizeof(EntityType));
    scene->entityTypeReference[index] = lastID;
    scene->entityTypeReference[lastIndex] = id;
    scene->entityTypeIndex[lastID] = index;
    scene->entityTypeIndex[id] = lastIndex;
    scene->entityTypeCount--;
}
static void destroyChunk(EntityType* type, uint32 chunkID);
static bool32 destroyEntityTypeChunks(EntityType* type){
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
    type->chunkCount = 0;
    if(type->chunks) avFree(type->chunks);
}

static bool32 destroyEntityType(EntityType* type){
    if(!destroyEntityTypeChunks(type)) return false;
    removeEntityType(type);
    return true;
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
    type->chunkCount--;
}

static void buildComponentIndex(EntityType* type){
    uint32 index = 0;
    for(uint32 i = 0; i < MAX_COMPONENT_COUNT; i++){
        if(MASK_HAS_COMPONENT(type->mask, i)){
            type->componentIndex[i] = index++;
        }
    }
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
    type->componentIndex[0] = MAX_COMPONENT_COUNT;
    buildComponentIndex(type);
    type->arrayMask = arrayMask;
    type->typeID = id;
    type->scene = scene;
    return type;
}

AV_API Scene sceneCreate(){
    Scene scene = avAllocate(sizeof(struct Scene), "");
    avMemset(scene, 0, sizeof(struct Scene));
    scene->entityTypeCapacity = 1;
    scene->entityTypeCount = 0;
    scene->entityTypeIndex = avAllocate(sizeof(uint32)*scene->entityTypeCapacity, "");
    scene->entityTypeReference = avAllocate(sizeof(EntityTypeID)*scene->entityTypeCapacity, "");
    scene->entityTypes = avAllocate(sizeof(EntityType)*scene->entityTypeCapacity, "");
    scene->entityTypeIndex[0] = 0;
    scene->entityTypeReference[0] = 0;
    scene->entityCapacity = 1;
    scene->entityCount = 0;
    scene->entityTable = avAllocate(sizeof(Entity)*scene->entityCapacity, "");
    scene->entityTable[0] = INVALID_ENTITY;
    scene->entityGeneration = avAllocate(sizeof(uint8)*scene->entityCapacity, "");
    scene->entityGeneration[0] = (uint8) ~(ENTITY_STAGED_BIT>>24);
    avRWLockCreate(&scene->entityIdLock);
    return scene;
}

AV_API void sceneDestroy(Scene scene){

    uint32 typeCount = scene->entityTypeCount;
    for(uint32 i = 0; i < typeCount; i++){
        // as types are swapped after destruction the we can keep destroying the first element
        destroyEntityTypeChunks(&scene->entityTypes[i]);
    }
    avFree(scene->entityTypes);
    avFree(scene->entityTypeReference);
    avFree(scene->entityTypeIndex);
    avFree(scene->entityTable);
    avFree(scene->entityGeneration);
    avRWLockDestroy(scene->entityIdLock);
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

    uint32 idA = chunk->localID[indexA];
    uint32 idB = chunk->localID[indexB];
    chunk->localID[indexA] = idB;
    chunk->localID[indexB] = idA;
    
    chunk->entities[indexA] = entityB;
    chunk->entities[indexB] = entityA;

    //no need to swap entityTable as we have already swapped the localindex
    
    return true;
}

static uint32 getComponentIndex(EntityType* type, ComponentType component){
    if(type==NULL) return MAX_COMPONENT_COUNT;
    if(component >= MAX_COMPONENT_COUNT) return MAX_COMPONENT_COUNT;
    return type->componentIndex[component];
}

static void initComponentArray(ComponentArray* array, ComponentType type){
    uint32 size= getComponentSize(type);
    if(array->data!=NULL || array->capacity!=0) return;

    array->count = 0;
    array->capacity = 2; // initial size of component array, as in creation at least 2 are always used
    array->data = avAllocate(size * array->capacity, "");
    array->index = avAllocate(sizeof(uint32)*array->capacity, "");
    //array->reference = avAllocate(sizeof(uint32)*array->capacity, "");
    array->index[0] = 0;
    array->index[1] = 1;
    //array->reference[0] = 0;
    //array->reference[1] = 1;
}

static uint32 appendArraySlot(ComponentArray* array, ComponentType type){
    uint32 size = getComponentSize(type);
    if(array->data==NULL || array->capacity==0){
        initComponentArray(array, type);
    }
    if(array->count >= array->capacity){
        array->capacity *= 2;
        array->data = avReallocate(array->data, size * array->capacity, "");
        array->index = avReallocate(array->index, sizeof(uint32)*array->capacity, "");
        //array->reference = avReallocate(array->reference, sizeof(uint32)*array->capacity, "");
        for(uint32 i = array->capacity>>1; i < array->capacity; i++){
            array->index[i] = i;
            //array->reference[i] = i;
        }
    }
    return array->index[array->count++];
}

static bool32 moveEntity(Scene scene, Entity src, LocalEntity dst){
    if(scene==NULL) return false;
    if(src==INVALID_ENTITY || dst==INVALID_ENTITY) return false;
    // move src to the end of the chunk
    // copy data from src to dst
    // skip entries not in either mask (constructors/destructors should be handled by calling function)
    EntityChunk* srcChunk = getEntityChunk(scene, src);
    EntityChunk* dstChunk = getLocalEntityChunk(dst);

    if(srcChunk==dstChunk){
        // move within same chunk, this shouln't happen
        return false; //swapEntities(scene, src, dst);
    }

    if(srcChunk==NULL) return false;
    if(srcChunk->count==0) return false;
    uint32 srcLocalIndex = getEntityLocalIndex(scene, src);
    if(srcLocalIndex >= CHUNK_CAPACITY) return false;
    uint32 srcIndex = srcChunk->localIndex[srcLocalIndex];
    if(srcIndex >= srcChunk->count) return false;

    if(dstChunk==NULL) return false;
    if(dstChunk->count==0) return false;
    uint32 dstLocalIndex = getLocalEntityLocalIndex(dst);
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
    srcIndex = lastIndex;
    
    
    // component -> none
    // array -> none
    // component -> component = copy
    // array -> component = copy from array
    // component -> array = create array and copy to array
    // array -> array = copy
    // none -> component = zero
    // none -> array = zero


    ComponentMask srcC = srcType->mask;
    ComponentMask srcA = srcType->arrayMask;

    ComponentMask dstC = dstType->mask;
    ComponentMask dstA = dstType->arrayMask;

    ComponentMask srcInvC = componentMaskInvert(srcC);
    ComponentMask dstInvC = componentMaskInvert(dstC);

    ComponentMask srcInvA = componentMaskInvert(srcA);
    ComponentMask dstInvA = componentMaskInvert(dstA);

    // ComponentMask compToNone = componentMaskAnd(componentMaskAnd(srcC, dstInvC), srcInvA);
    // ComponentMask arrayToNone = componentMaskAnd(srcA, dstInvC);

    ComponentMask compToComp = componentMaskAnd(componentMaskAnd(srcC, dstC), componentMaskAnd(srcInvA, dstInvA));
    ComponentMask arrayToArray = componentMaskAnd(srcA, dstA);

    ComponentMask arrayToComp = componentMaskAnd(srcA, componentMaskAnd(dstC, dstInvA));
    ComponentMask compToArray = componentMaskAnd(componentMaskAnd(srcC, srcInvA), dstA);

    ComponentMask noneToComp = componentMaskAnd(componentMaskAnd(dstC, srcInvC), dstInvA);
    ComponentMask noneToArray = componentMaskAnd(dstA, srcInvC);

    ComponentMask createArray = componentMaskOr(compToArray, noneToArray);
    ComponentMask copyToArray = compToArray;
    ComponentMask copyFromArray = arrayToComp;
    ComponentMask rawCopy = componentMaskOr(compToComp, arrayToArray);
    ComponentMask zeroMem = noneToComp;

    ComponentMask mask = componentMaskOr(srcC, dstC);

    ITERATE_MASK(createArray, component){
        uint32 size = getComponentSize(component);
        uint32 componentDstIndex = getComponentIndex(dstType, component);
        byte* dstOffset = ((byte*)dstChunk->components[componentDstIndex].single) + dstIndex*size;
        initComponentArray((ComponentArray*) dstOffset, component);
    }
    ITERATE_MASK(copyToArray, component){
        uint32 size = getComponentSize(component);
        uint32 componentSrcIndex = getComponentIndex(srcType, component);
        uint32 componentDstIndex = getComponentIndex(dstType, component);
        byte* srcOffset = ((byte*)srcChunk->components[componentSrcIndex].single) + srcIndex*size;
        byte* dstOffset = ((byte*)dstChunk->components[componentDstIndex].single) + dstIndex*size;
        ComponentArray* array = (ComponentArray*)dstOffset;
        dstOffset = (byte*)array->data + appendArraySlot(array, component) * size;
        avMemcpy(dstOffset, srcOffset, size);
    }
    ITERATE_MASK(copyFromArray, component){
        uint32 size = getComponentSize(component);
        uint32 componentSrcIndex = getComponentIndex(srcType, component);
        uint32 componentDstIndex = getComponentIndex(dstType, component);
        byte* srcOffset = ((byte*)srcChunk->components[componentSrcIndex].single) + srcIndex*size;
        byte* dstOffset = ((byte*)dstChunk->components[componentDstIndex].single) + dstIndex*size;
        ComponentArray* array = (ComponentArray*)srcOffset;
        srcOffset = (byte*)array->data + array->index[0] * size;
        avMemcpy(dstOffset, srcOffset, size);
        deinitComponentArray(array);
    }
    ITERATE_MASK(rawCopy, component){
        uint32 size = getComponentSize(component);
        uint32 componentSrcIndex = getComponentIndex(srcType, component);
        uint32 componentDstIndex = getComponentIndex(dstType, component);
        byte* srcOffset = ((byte*)srcChunk->components[componentSrcIndex].single) + srcIndex*size;
        byte* dstOffset = ((byte*)dstChunk->components[componentDstIndex].single) + dstIndex*size;
        avMemcpy(dstOffset, srcOffset, size);
    }
    ITERATE_MASK(zeroMem, component){
        uint32 size = getComponentSize(component);
        uint32 componentSrcIndex = getComponentIndex(srcType, component);
        uint32 componentDstIndex = getComponentIndex(dstType, component);
        byte* srcOffset = ((byte*)srcChunk->components[componentSrcIndex].single) + srcIndex*size;
        byte* dstOffset = ((byte*)dstChunk->components[componentDstIndex].single) + dstIndex*size;
        avMemset(dstOffset, 0 , size);
    }

    dstChunk->entities[dstIndex] = src;
    srcChunk->entities[srcIndex] = INVALID_ENTITY;

    avRWLockWriteLock(scene->entityIdLock); // should still be fast as this should only run on main thread
    scene->entityTable[ENTITY_INDEX(src)] = dst;
    // scene->entityTable[ENTITY_INDEX(dst)] = ENTITY(getChunkID(srcChunk), srcLocalIndex);
    avRWLockWriteUnlock(scene->entityIdLock);

    srcChunk->count--;

    if(srcChunk->count==0){
        destroyChunk(srcType, getChunkID(srcChunk));
        
        if(srcType->chunkCount==0){
            destroyEntityType(srcType);
        }
    }

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
    uint32 index = appendArraySlot(array, type);
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
            if(chunk->components[index].array[entityIndex].data!=NULL){
                index++;
                continue;
            }
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

        uint32 compIndex = getComponentIndex(type, comp);
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

    chunk->entities[lastIndex] = INVALID_ENTITY;
    freeEntityID(scene, entity);

    chunk->count--;

    if(chunk->count==0){
        destroyChunk(type, getChunkID(chunk));

        if(type->chunkCount==0){
            destroyEntityType(type);
        }
    }
  

    return true;
}

static Entity createLocalEntity(EntityType* type, EntityChunk** chunkPtr, uint32* localIndexPtr){
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

    Entity localEntity =  ENTITY(chunkID, chunk->localID[localIndex]);
    if(localIndexPtr) *localIndexPtr = localIndex;
    if(chunkPtr) *chunkPtr = chunk;
    return localEntity;
}

static Entity createEntity(EntityType* type){
    if(type==NULL) return INVALID_ENTITY;
    
    uint32 localIndex = 0;
    EntityChunk* chunk = NULL;
    Entity localEntity = createLocalEntity(type, &chunk, &localIndex);
    if(localEntity==INVALID_ENTITY) return INVALID_ENTITY;

    Entity globalEntity = allocateEntityID(type->scene);
    avRWLockReadLock(type->scene->entityIdLock);
    // we can alter the generation and table without write lock as we have soul ownership
    type->scene->entityGeneration[globalEntity] = (type->scene->entityGeneration[globalEntity]+1) & (~ENTITY_STAGED_BIT);
    uint8 generation = type->scene->entityGeneration[globalEntity];
    type->scene->entityTable[globalEntity] = localEntity;
    avRWLockReadUnlock(type->scene->entityIdLock);

    Entity entity = GLOBAL_ENTITY(generation, globalEntity);
    chunk->entities[localIndex] = entity;
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

    
    Entity dstEntity = createLocalEntity(dst, NULL, NULL);
    if(dstEntity==INVALID_ENTITY) return false;
    
    performDestructorMasked(scene, getEntityLocalIndex(scene, entity), getEntityChunk(scene, entity), src, destroyMask);
    moveEntity(scene, entity, dstEntity);
    performConstructorMasked(scene, getEntityLocalIndex(scene, entity), getEntityChunk(scene, entity), dst,  componentMaskAnd(createMask, componentMaskInvert(dst->arrayMask)), info);

    return true;
}

AV_API Entity entityCreate(Scene scene, ComponentInfoRef info){

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

AV_API bool32 entityAddComponent(Scene scene, Entity entity, ComponentInfoRef info){
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
        performConstructorMasked(scene, getEntityLocalIndex(scene, entity), getEntityChunk(scene, entity), type, mask, info);
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

AV_API bool32 entityRemoveComponent(Scene scene, Entity entity, ComponentType component, uint32 index){
    EntityType* type = getType(scene, entity);
    if(type==NULL){
        return false;
    }
    if(!MASK_HAS_COMPONENT(type->mask, component)) return false;

    if(!MASK_HAS_COMPONENT(type->arrayMask, component)){
        if(index != 0) return false;
        return entityRemoveComponentType(scene, entity, component);
    }
    uint32 componentIndex = getComponentIndex(type, component);
    // get size of array
    EntityChunk* chunk = getEntityChunk(scene, entity);
    uint32 localIndex = getEntityLocalIndex(scene, entity);

    ComponentArray* array = chunk->components[componentIndex].array + localIndex;
    if(array->count == 2){
        if(index >= 2) return false;


        ComponentMask newArrayMask = type->arrayMask;
        MASK_REMOVE_COMPONENT(newArrayMask, component);
        type = findEntityType(scene, type->mask, newArrayMask);
        if(type==NULL){
            type = createEntityType(scene, type->mask, newArrayMask);
            if(type==NULL){
                return false;
            }
        }

        destroyArrayComponent(scene, entity, array, component, index);
        return entityChangeType(scene, entity, type, NULL);
    }

    destroyArrayComponent(scene, entity, array, component, index);
    return true;
}

AV_API bool32 entityRemoveComponentType(Scene scene, Entity entity, ComponentType component){
    EntityType* type = getType(scene, entity);
    if(type==NULL){
        return false;
    }
    if(!MASK_HAS_COMPONENT(type->mask, component)) return false;
    ComponentMask newMask = type->mask;
    ComponentMask newArrayMask = type->mask;
    MASK_REMOVE_COMPONENT(newMask, component);
    MASK_REMOVE_COMPONENT(newArrayMask, component);

    type = findEntityType(scene, newMask, newArrayMask);
    if(type==NULL){
        type = createEntityType(scene, newMask, newArrayMask);
        if(type==NULL){
            return false;
        }
    }
    return entityChangeType(scene, entity, type, NULL);

}

AV_API bool32 entityDestroy(Scene scene, Entity entity){
    if(scene==NULL) return false;
    if(entity==INVALID_ENTITY) return false;

    return destroyEntity(scene, entity);
}

AV_API uint32 entityGetComponentCount(Scene scene, Entity entity, ComponentType component){
    if(scene==NULL) return 0;
    if(entity==INVALID_ENTITY) return 0;
    EntityType* type = getType(scene, entity);
    if(type==NULL) return 0;
    if(!MASK_HAS_COMPONENT(type->mask, component)) return 0;
    if(!MASK_HAS_COMPONENT(type->arrayMask, component)) return 1;

    EntityChunk* chunk = getEntityChunk(scene, entity);
    uint32 index = getEntityLocalIndex(scene, entity);
    uint32 componentIndex = getComponentIndex(type, component);
    ComponentArray* array = chunk->components[componentIndex].array + index;
    return array->count;
}

AV_API void* entityGetComponent(Scene scene, Entity entity, ComponentType component, uint32 index){
    EntityType* type = getType(scene, entity);
    if(type==NULL) return NULL;

    if(!MASK_HAS_COMPONENT(type->mask, component)) return NULL;

    EntityChunk* chunk = getEntityChunk(scene, entity);
    uint32 localIndex = getEntityLocalIndex(scene, entity);
    uint32 compIndex = getComponentIndex(type, component);
    if(compIndex==INVALID_COMPONENT) return NULL;

    uint32 size = getComponentSize(component);

    byte* offset = NULL;
    if(MASK_HAS_COMPONENT(type->arrayMask, component)){
        ComponentArray* array = chunk->components[compIndex].array + localIndex;
        if(index >= array->count) return NULL;

        localIndex = array->index[index];
        offset = (byte*)array->data;
    }else{
        if(index != 0) return NULL;
        offset = (byte*) chunk->components[compIndex].single;
    }

    return offset + localIndex*size;
}


AV_API void* entityGetComponentFast(Scene scene, Entity entity, ComponentType component, uint32 index){
    EntityType* type = getType(scene, entity);
    EntityChunk* chunk = getEntityChunk(scene, entity);
    uint32 localIndex = getEntityLocalIndex(scene, entity);
    uint32 compIndex = getComponentIndex(type, component);
    uint32 size = getComponentSize(component);
    byte* offset = NULL;
    if(MASK_HAS_COMPONENT(type->arrayMask, component)){
        ComponentArray* array = chunk->components[compIndex].array + localIndex;
        localIndex = array->index[index];
        offset = (byte*)array->data;
    }else{
        offset = (byte*) chunk->components[compIndex].single;
    }
    return offset + localIndex*size;
}
