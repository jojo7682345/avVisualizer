#include "ecsInternal.h"
#include "ecsStaging.h"

#include "containers/darray.h"
#include "containers/idMapping.h"

static ComponentRegistry componentRegistry = {0};


// the slot reserved is set to ENTITY_ID_RESERVED
// and should be imediately set to the correct value, 
// as the id is not checked for the reserved case (will segfault)
Entity allocateEntityID(Scene scene, LocalEntity value, bool8 staged){
    if(value==INVALID_ENTITY || scene==NULL) return INVALID_ENTITY;
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
    uint32 entityCount = atomic_load_explicit(&scene->entityCount, memory_order_acquire);
    for(uint32 checkCount = 0; checkCount < scene->entityCapacity; checkCount++){
        // start at the end of the components, as there is bound to be space ther, 
        // and only wrap back around if there isn't
        i = (checkCount + entityCount) % scene->entityCapacity;
        
        Entity expected = INVALID_ENTITY;
        if(atomic_compare_exchange_strong_explicit(scene->entityTable + i, &expected, value, memory_order_acq_rel, memory_order_acquire)){
            uint32 generation = (scene->entityGeneration[i]+1)&((~ENTITY_STAGED_BIT)>>24);
            scene->entityGeneration[i] = generation | (staged!=0 ? ENTITY_STAGED_BIT>>24 : 0);
            entity = GLOBAL_ENTITY(generation, i);
            atomic_fetch_add_explicit(&scene->entityCount, 1, memory_order_acq_rel);
            break;
        }
    }
    avRWLockReadUnlock(scene->entityIdLock);
    return entity;
}

void freeEntityID(Scene scene, Entity entity){
    if(scene==NULL || entity==INVALID_ENTITY) return;
    uint32 index = ENTITY_INDEX(entity);
    avRWLockReadLock(scene->entityIdLock);
    if(index >= scene->entityCapacity) {
        avRWLockReadUnlock(scene->entityIdLock);
        return;
    }
    scene->entityGeneration[index] &= ~(ENTITY_STAGED_BIT)>>24;
    Entity oldEntity = atomic_exchange_explicit(scene->entityTable + index, INVALID_ENTITY, memory_order_release);
    if(oldEntity!=INVALID_ENTITY){ // slot was not alread freed
        atomic_fetch_sub_explicit(&scene->entityCount, 1, memory_order_acq_rel);
    }
    avRWLockReadUnlock(scene->entityIdLock);
}

void modifyEntityID(Scene scene, Entity entity, LocalEntity localEntity, bool32 isStaged){
    if(scene==NULL || entity==INVALID_ENTITY) return;
    uint32 index = ENTITY_INDEX(entity);
    avRWLockReadLock(scene->entityIdLock);
    if(index >= scene->entityCapacity){
        avRWLockReadUnlock(scene->entityIdLock);
        return;
    }
    scene->entityTable[index] = localEntity;
    scene->entityGeneration[index] = (~(ENTITY_STAGED_BIT)>>24) & scene->entityGeneration[index] | (isStaged << 7);

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

EntityChunk* getLocalEntityChunk(Entity localEntity){
    if(localEntity==INVALID_ENTITY) return NULL;
    return &chunkPool.chunks[ENTITY_CHUNK(localEntity)];
}


bool32 getEntityDetails(Scene scene, Entity entity, uint32* index, LocalEntity* localEntity, uint8* generation, bool8* staged){
    uint32 tmpIndex;
    LocalEntity tmpLocalEntity;
    uint8 tmpGeneration;
    bool8 tmpStaged;
    if(!index) index = &tmpIndex;
    if(!localEntity) localEntity = &tmpLocalEntity;
    if(!generation) generation = &tmpGeneration;
    if(!staged) staged = &tmpStaged;
    
    if(scene==NULL || entity==INVALID_ENTITY) return false;

    uint32 ind = ENTITY_INDEX(entity);
    uint32 gen =  ENTITY_GENERATION(entity);

    if(ind >= scene->entityCount) return false;

    avRWLockReadLock(scene->entityIdLock);
    LocalEntity locEntity = scene->entityTable[ind];
    uint8 tmpGen = scene->entityGeneration[ind];
    avRWLockReadUnlock(scene->entityIdLock);
    
    uint8 storedGen = tmpGen & ~(ENTITY_STAGED_BIT)>>24;
    uint8 storedStaged = (tmpGen & (ENTITY_STAGED_BIT>>24)) != 0;

    if(storedGen != gen) return false;
    
    *staged = storedStaged;
    *localEntity = locEntity;
    *index = ind;
    *generation = storedGen;
    return true;
}

static EntityChunk* getEntityChunk(Scene scene, Entity entity){
    LocalEntity localEntity;
    bool8 isStaged = false;
    if(!getEntityDetails(scene, entity, NULL, &localEntity, NULL, &isStaged)) return NULL;
    if(isStaged) return NULL;
    return getLocalEntityChunk(localEntity);
}

uint32 getLocalEntityLocalIndex(Entity localEntity){
    if(localEntity==INVALID_ENTITY) return 0;
    return ENTITY_LOCAL_INDEX(localEntity);
}

static uint32 getEntityLocalIndex(Scene scene, Entity entity){
    LocalEntity localEntity;
    bool8 isStaged = false;
    if(!getEntityDetails(scene, entity, NULL, &localEntity, NULL, &isStaged)) return -1;
    if(isStaged) return -1;
    return getLocalEntityLocalIndex(localEntity);
}

LocalEntity getEntityLocal(Scene scene, Entity entity){
    LocalEntity localEntity;
    bool8 isStaged;
    if(!getEntityDetails(scene, entity, NULL, &localEntity, NULL, &isStaged)) return INVALID_ENTITY;
    if(isStaged) return INVALID_ENTITY;
    return localEntity;
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
        chunk->components[index] = (Component) { avAllocate((size!=0 ? size*CHUNK_CAPACITY : 1), "") };
        avMemset(chunk->components[index], 0, size);
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

    uint32 index = 0;
    ITERATE_MASK(mask, component){
        uint32 size = getComponentSize(component);
        for(uint32 i = 0; i < chunk->count; i++){
            destroyComponent(type->scene, chunk->entities[i], (byte*)chunk->components[index] + size*i, component);
        }
        avFree(chunk->components[index]);
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

static EntityType* createEntityType(Scene scene, ComponentMask mask){

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
    MAPPING_CREATE(scene->stagingBuffers, AV_MAX_THREADS);
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
    for(uint32 i = 0; i < MAPPING_SIZE(scene->stagingBuffers); i++){
        stagingBufferDestroy(scene, scene->stagingBuffers[i].threadID);
    }
    MAPPING_DESTROY(scene->stagingBuffers);
    avFree(scene);

}

static bool32 buildMasks(ComponentInfo* info, ComponentMask* mask){
    avMemset(mask, 0, sizeof(ComponentMask));
    bool32 ret = true;
    uint32 maxCount = 4096;
    while(info){
        if(info->type >= MAX_COMPONENT_COUNT) {
            ret = false;
            continue;
        }
        if(MASK_HAS_COMPONENT(*mask, info->type)){
            return false;
        }else{
            MASK_ADD_COMPONENT(*mask, info->type);
        }
        info = info->next;
        maxCount--;
        if(maxCount==0) return false;
    }
    return ret;
}

static EntityType* findEntityType(Scene scene, ComponentMask mask){
    for(uint32 i = 0; i < scene->entityTypeCount; i++){
        EntityType* type = scene->entityTypes + i;
        if(componentMaskEquals(type->mask, mask)){
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

    uint32 maxSize = 0;
    {ITERATE_MASK(mask, component){
        if(getComponentSize(component) > maxSize){
            maxSize = getComponentSize(component);
        }
    }}
    if(maxSize>0){
        byte* componentBuffer = avAllocate(maxSize, "");
        uint32 index = 0;
        ITERATE_MASK(mask, component){
            uint32 size = getComponentSize(component);
            avMemcpy(componentBuffer, ((byte*)chunk->components[index]) + indexA*size, size);
            avMemcpy(((byte*)chunk->components[index]) + indexA*size, ((byte*)chunk->components[index]) + indexB*size, size);
            avMemcpy(((byte*)chunk->components[index]) + indexB*size, componentBuffer, size);
            index++;
        }
        avFree(componentBuffer);
    }
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

uint32 getComponentIndex(EntityType* type, ComponentType component){
    if(type==NULL) return MAX_COMPONENT_COUNT;
    if(component >= MAX_COMPONENT_COUNT) return MAX_COMPONENT_COUNT;
    return type->componentIndex[component];
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
    // component -> component = copy
    // none -> component = zero


    ComponentMask srcC = srcType->mask;
    ComponentMask dstC = dstType->mask;

    ComponentMask srcInvC = componentMaskInvert(srcC);
    ComponentMask dstInvC = componentMaskInvert(dstC);

    
    
    ComponentMask compToNone = componentMaskAnd(srcC, dstInvC);
    ComponentMask compToComp = componentMaskAnd(srcC, dstC);
    ComponentMask noneToComp = componentMaskAnd(dstC, srcInvC);

    ComponentMask rawCopy = compToComp;
    ComponentMask zeroMem = noneToComp;

    ComponentMask mask = componentMaskOr(srcC, dstC);

    ITERATE_MASK(rawCopy, component){
        uint32 size = getComponentSize(component);
        uint32 componentSrcIndex = getComponentIndex(srcType, component);
        uint32 componentDstIndex = getComponentIndex(dstType, component);
        byte* srcOffset = ((byte*)srcChunk->components[componentSrcIndex]) + srcIndex*size;
        byte* dstOffset = ((byte*)dstChunk->components[componentDstIndex]) + dstIndex*size;
        avMemcpy(dstOffset, srcOffset, size);
    }
    ITERATE_MASK(zeroMem, component){
        uint32 size = getComponentSize(component);
        uint32 componentSrcIndex = getComponentIndex(srcType, component);
        uint32 componentDstIndex = getComponentIndex(dstType, component);
        byte* srcOffset = ((byte*)srcChunk->components[componentSrcIndex]) + srcIndex*size;
        byte* dstOffset = ((byte*)dstChunk->components[componentDstIndex]) + dstIndex*size;
        avMemset(dstOffset, 0 , size);
    }

    dstChunk->entities[dstIndex] = src;
    srcChunk->entities[srcIndex] = INVALID_ENTITY;

    avRWLockReadLock(scene->entityIdLock); // should still be fast as this should only run on main thread
    scene->entityTable[ENTITY_INDEX(src)] = dst;
    // scene->entityTable[ENTITY_INDEX(dst)] = ENTITY(getChunkID(srcChunk), srcLocalIndex);
    avRWLockReadUnlock(scene->entityIdLock);

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

    uint32 index = 0;
    ITERATE_MASK(mask, component){
        uint32 size = getComponentSize(component);
        destroyComponent(type->scene, chunk->entities[i], (byte*)chunk->components[index] + i*size, component);        
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

static void performConstructorMasked(Scene scene, uint32 i, EntityChunk* chunk, EntityType* type, ComponentMask mask, ComponentInfo* info){
    if(scene==NULL || type==NULL || chunk==NULL) return;
    if(i >= chunk->count) return;

    mask = componentMaskAnd(type->mask, mask);

    uint32 entityIndex = chunk->localIndex[i];

    uint32 index = 0;
    ITERATE_MASK(mask, component){
        uint32 size = getComponentSize(component);
        avMemset((byte*)chunk->components[index] + size * entityIndex, 0, size);
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
        byte* base = (byte*) chunk->components[compIndex];
        
        void* dst = base + entityIndex * size;
        createComponent(scene, chunk->entities[entityIndex], dst, comp, info);
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

bool32 createEmptyEntity(Scene scene, Entity entity, ComponentMask mask){
    if(scene==NULL) return false;
    if(entity==INVALID_ENTITY) return false;
    bool8 isStaged = false;
    if(!getEntityDetails(scene, entity, NULL, NULL, NULL, &isStaged)){
        return false;
    }
    if(!isStaged) return false; //overwriting existing entity


    EntityType* type = findEntityType(scene, mask);

    if(type==NULL){
        type = createEntityType(scene, mask);
        if(type==NULL){
            return INVALID_ENTITY;
        }
    }
    uint32 localIndex = 0;
    EntityChunk* chunk = NULL;
    Entity localEntity = createLocalEntity(type, &chunk, &localIndex);
    chunk->entities[localIndex] = entity;
    if(localEntity==INVALID_ENTITY) return false;
    modifyEntityID(scene, entity, localEntity, false);
    return true;
}

static Entity createEntity(EntityType* type){
    if(type==NULL) return INVALID_ENTITY;
    
    uint32 localIndex = 0;
    EntityChunk* chunk = NULL;
    Entity localEntity = createLocalEntity(type, &chunk, &localIndex);
    if(localEntity==INVALID_ENTITY) return INVALID_ENTITY;

    Entity entity = allocateEntityID(type->scene, localEntity, false);
    
    chunk->entities[localIndex] = entity;
    return chunk->entities[localIndex];
}

static bool32 isComponentInfoCompatible(ComponentMask mask, ComponentInfo* info){
    ComponentMask createMask = {0};
    ComponentMask createArrayMask = {0};
    if(!buildMasks(info, &createMask)) return false;
    return true;
}

static bool32 entityChangeType(Scene scene, Entity entity, EntityType* dst, ComponentInfo* info){
    if(scene==NULL) return false;
    if(entity==INVALID_ENTITY) return false;
    if(dst==NULL) return false;
    
    EntityType* src = getType(scene, entity);
    if(src==dst) return true;

    if(componentMaskEquals(src->mask, dst->mask)){
        return true; //nothing needs to be done
    }

    ComponentMask transitionMask = componentMaskAnd(src->mask, dst->mask);
    ComponentMask createMask = componentMaskAnd(componentMaskInvert(transitionMask), dst->mask);
    ComponentMask destroyMask = componentMaskAnd(componentMaskInvert(transitionMask), src->mask);

    
    if(!isComponentInfoCompatible(createMask, info)){
        return false;
    }

    
    Entity dstEntity = createLocalEntity(dst, NULL, NULL);
    if(dstEntity==INVALID_ENTITY) return false;
    
    performDestructorMasked(scene, getEntityLocalIndex(scene, entity), getEntityChunk(scene, entity), src, destroyMask);
    moveEntity(scene, entity, dstEntity);
    performConstructorMasked(scene, getEntityLocalIndex(scene, entity), getEntityChunk(scene, entity), dst, createMask, info);

    return true;
}

void stagingBufferCreate(Scene scene, AvThreadID threadId){
    if(scene == NULL || threadId >= AV_MAX_THREADS) return;

    StagingBuffer buffer = {0};
    buffer.threadID = threadId;
    avAllocatorCreate(0, AV_ALLOCATOR_TYPE_DYNAMIC, &buffer.componentAllocator);
    avAllocatorCreate(0, AV_ALLOCATOR_TYPE_DYNAMIC, &buffer.componentHandleAllocator);
    buffer.entities = darrayCreate(StagedEntity);
    MAPPING_INSERT(scene->stagingBuffers, threadId, buffer);
}

void stagingBufferDestroy(Scene scene, AvThreadID threadId){
    if(darrayLength(scene->stagingBuffers[threadId].entities)){
        return; // entities are still in use
    }

    avAllocatorDestroy(&scene->stagingBuffers[threadId].componentAllocator);
    avAllocatorDestroy(&scene->stagingBuffers[threadId].componentHandleAllocator);
    darrayDestroy(scene->stagingBuffers[threadId].entities);
    MAPPING_REMOVE(scene->stagingBuffers, threadId);
}

StagingBufferHandle getStagingBuffer(Scene scene, AvThreadID threadId){
    if(scene==NULL || threadId >= AV_MAX_THREADS) return NULL;
    if(MAPPING_GET(scene->stagingBuffers, threadId)->threadID==AV_MAIN_THREAD_ID){
        stagingBufferCreate(scene, threadId);
        return MAPPING_GET(scene->stagingBuffers, threadId);
    }else{
        return MAPPING_GET(scene->stagingBuffers, threadId);
    }
}

AV_API Entity entityCreate(Scene scene, ComponentInfoRef info){
    AvThreadID threadId = avThreadGetID();
    if(threadId!=AV_MAIN_THREAD_ID){
        return stagedEntityCreate(scene, getStagingBuffer(scene, threadId), info);
    }


    ComponentMask mask = {0};
    ComponentMask arrayMask = {0};
    if(!buildMasks(info, &mask)){
        return INVALID_ENTITY;
    }

    EntityType* type = findEntityType(scene, mask);
    if(type==NULL){
        type = createEntityType(scene, mask);
        if(type==NULL){
            return INVALID_ENTITY;
        }
    }

    Entity entity = createEntity(type);

    performConstructor(scene, getEntityLocalIndex(scene, entity), getEntityChunk(scene, entity), type, info);

    return entity;
}

AV_API bool32 entityAddComponent(Scene scene, Entity entity, ComponentInfoRef info){
    AvThreadID threadId = avThreadGetID();
    if(threadId != AV_MAIN_THREAD_ID){
        return stagedEntityAddComponent(scene, getStagingBuffer(scene, threadId), entity, info);
    }

    EntityType* type = getType(scene, entity);
    if(type==NULL){
        return false;
    }

    ComponentMask mask = {0};
    ComponentMask arrayMask = {0};
    if(!buildMasks(info, &mask)){
        return false;
    }
    if(componentMaskEquals(mask, (ComponentMask){0})){
        return false; // cannot add none components
    }

    

    ComponentMask typeMask = type->mask;
    if(!componentMaskEquals(componentMaskAnd(typeMask, mask), (ComponentMask){0})){
        return false; //cannot add duplicate components;
    }
    ComponentMask newMask = componentMaskOr(typeMask, mask);

    type = findEntityType(scene, newMask);
    if(type==NULL){
        type = createEntityType(scene, newMask);
        if(type==NULL){
            return false;
        }
    }
    return entityChangeType(scene, entity, type, info);
}

AV_API bool32 entityRemoveComponent(Scene scene, Entity entity, ComponentType component){
    AvThreadID threadId = avThreadGetID();
    if(threadId != AV_MAIN_THREAD_ID){
        return stagedEntityRemoveComponent(scene, getStagingBuffer(scene, threadId), entity, component);
    }
    EntityType* type = getType(scene, entity);
    if(type==NULL){
        return false;
    }
    if(!MASK_HAS_COMPONENT(type->mask, component)) return false;
    ComponentMask newMask = type->mask;
    MASK_REMOVE_COMPONENT(newMask, component);

    type = findEntityType(scene, newMask);
    if(type==NULL){
        type = createEntityType(scene, newMask);
        if(type==NULL){
            return false;
        }
    }
    return entityChangeType(scene, entity, type, NULL);

}

AV_API bool32 entityDestroy(Scene scene, Entity entity){
    if(entity==INVALID_ENTITY) return false;
    AvThreadID threadId = avThreadGetID();
    if(threadId != AV_MAIN_THREAD_ID){
        return stagedEntityDestroy(scene, getStagingBuffer(scene, threadId), entity);
    }
    return destroyEntity(scene, entity);
}

AV_API bool32 entityHasComponent(Scene scene, Entity entity, ComponentType component){
    if(entity==INVALID_ENTITY) return false;
    bool8 isStaged = false;
    LocalEntity localEntity = INVALID_ENTITY;
    if(!getEntityDetails(scene, entity, NULL, &localEntity, NULL, &isStaged)) return false;
    if(isStaged){
        AvThreadID threadId = avThreadGetID();
        if(threadId == AV_MAIN_THREAD_ID) return false;
        return stagedEntityHasComponent(scene, getStagingBuffer(scene, threadId), entity, component);
    }

    EntityChunk* chunk = getLocalEntityChunk(localEntity);
    EntityType* type = getEntityType(scene, chunk->entityType);
    if(type==NULL) return false;
    if(!MASK_HAS_COMPONENT(type->mask, component)) return false;
    return true;
}

AV_API void* entityGetComponent(Scene scene, Entity entity, ComponentType component){
    AvThreadID threadId = avThreadGetID();
    if(threadId!=AV_MAIN_THREAD_ID) return NULL;

    EntityType* type = getType(scene, entity);
    if(type==NULL) return NULL;
    

    if(!MASK_HAS_COMPONENT(type->mask, component)) return NULL;

    EntityChunk* chunk = getEntityChunk(scene, entity);
    uint32 localIndex = getEntityLocalIndex(scene, entity);
    uint32 compIndex = getComponentIndex(type, component);
    if(compIndex==INVALID_COMPONENT) return NULL;

    uint32 size = getComponentSize(component);

    byte* offset = NULL;
    
    offset = (byte*) chunk->components[compIndex];

    return offset + localIndex*size;
}


void* entityGetComponentFast(Scene scene, Entity entity, ComponentType component){
    AvThreadID threadId = avThreadGetID();
    if(threadId!=AV_MAIN_THREAD_ID) return NULL;
    EntityType* type = getType(scene, entity);
    EntityChunk* chunk = getEntityChunk(scene, entity);
    uint32 localIndex = getEntityLocalIndex(scene, entity);
    uint32 compIndex = getComponentIndex(type, component);
    uint32 size = getComponentSize(component);
    byte* offset = NULL;
    offset = (byte*) chunk->components[compIndex];
    return offset + localIndex*size;
}


void sceneApply(Scene scene){
    if(scene==NULL) return;
    AvThreadID threadId = avThreadGetID();
    if(threadId != AV_MAIN_THREAD_ID) return;

    for(uint32 i = 0; i < MAPPING_SIZE(scene->stagingBuffers); i++){
        //if(scene->stagingBuffers[i].threadID==AV_MAIN_THREAD_ID)continue; 
    }
}
