#include "ecsInternal.h"
#include "ecsStaging.h"
#include "ecsQuerries.h"

#include "containers/darray.h"
#include "containers/idMapping.h"
#include "containers/listpool.h"

#include <AvUtils/avMath.h>


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

AV_API bool32 registerComponent(ComponentType* component, uint32 size, ComponentConstructor constructor, ComponentDestructor destructor){
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
    if(size > componentRegistry.maxSize){
        componentRegistry.maxSize = size;
    }
	return true;
}

static bool32 isComponentRegistered(ComponentType component){
    return MASK_HAS_COMPONENT(componentRegistry.registeredComponents, component);
}

uint32 getComponentSize(ComponentType component){
    if(!isComponentRegistered(component)) return 0;
    return componentRegistry.entries[component].size;
}

uint32 getMaxComponentSize(){
    return componentRegistry.maxSize;
}

ComponentConstructor getComponentConstructor(ComponentType component){
    if(!isComponentRegistered(component)) return NULL;
    return componentRegistry.entries[component].constructor;
}

ComponentDestructor getComponentDestructor(ComponentType component){
    if(!isComponentRegistered(component)) return NULL;
    return componentRegistry.entries[component].destructor;
}


AV_API ComponentMask getRegisteredComponents(){
    return componentRegistry.registeredComponents;
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

EntityChunk* getChunk(uint32 chunkID){
    if(chunkID >= MAX_CHUNKS) {
        return NULL;
    }
    uint32 index = chunkPool.chunkIndex[chunkID];
    if(index >= chunkPool.chunkCount){
        return NULL;
    } 
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
    chunkPool.chunkReference[index] = lastID;
    chunkPool.chunkReference[lastIndex] = chunkID;
    chunkPool.chunkIndex[chunkID] = lastIndex;
    chunkPool.chunkIndex[lastID] = index;
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
    ITERATE_MASK(mask, component){
        uint32 size = getComponentSize(component);
        bool32 isArray = false;
        chunk->components[component] = (Component) { avAllocate((size!=0 ? size*CHUNK_CAPACITY : 1), "") };
        //avDebug("Alloc %u", chunkID);
        avMemset(chunk->components[component], 0, size);
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

static void destroyChunk(EntityType* type, uint32 chunkID){
    EntityChunk* chunk = getChunk(chunkID);
    if(chunk == NULL) return;
    if(type == NULL) return;
    
    ComponentMask mask = type->mask;

    ITERATE_MASK(mask, component){
        uint32 size = getComponentSize(component);
        for(uint32 i = 0; i < chunk->count; i++){
            destroyComponent(type->scene, chunk->entities[i], (byte*)chunk->components[component] + size*i, component);
        }
        //avDebug("Dealloc %u", chunkID);
        avFree(chunk->components[component]);
    }

    freeChunk(chunkID);
    type->chunkCount--;
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
    
    uint32 chunkCount = type->chunkCount;
    for(uint32 i = 0; i < chunkCount; i++){
        destroyChunk(type, type->chunks[i]);
        
    }
    type->chunkCount = 0;
    if(type->chunks) avFree(type->chunks);
    LIST_FREE(type->systems);
}

static void unregisterEntityTypeFromSystems(Scene scene, EntityType* type){
    LIST_FOR(type->systems, EcsSystemID, sys){
        System* system = MAPPING_GET(scene->systems, *sys);
        LIST_FOR_I(system->entityTypes, i){
            EntityTypeID entityId = LIST_GET(system->entityTypes, EntityTypeID, i);
            if(entityId == type->typeID){
                LIST_SWAP_POP(system->entityTypes, i);
                break;
            }
        }
    }
}

void unregisterSystemFromEntityTypes(Scene scene, EcsSystemID id){
    System* system = MAPPING_GET(scene->systems, id);
    if(system==NULL) {
        avError("Trying to access invalid handle");
        return;
    }
    LIST_FOR(system->entityTypes, EntityTypeID, t){
        EntityType* type = getEntityType(scene, *t);
        LIST_FOR_I(type->systems, i){
            EcsSystemID sysId = LIST_GET(type->systems, EcsSystemID, i);
            if(sysId = id){
                LIST_SWAP_POP(type->systems, i);
                break;
            }
        }
    }
}

static bool32 destroyEntityType(Scene scene, EntityType* type){
    unregisterEntityTypeFromSystems(scene, type);
    if(!destroyEntityTypeChunks(type)) return false;
    removeEntityType(type);
    return true;
}

static void registerSystems(Scene scene, EntityType* type){
    for(uint32 i = 0; i < MAPPING_SIZE(scene->systems); i++){
        System* system = scene->systems + i;
        if(!isQuerrySelected(system->selection, type->mask)){
            continue;
        }
        EcsSystemID id = MAPPING_ID(scene->systems, i);
        LIST_PUSH(type->systems, id);
        LIST_PUSH(system->entityTypes, type->typeID);
    }
}

void registerNewSystem(Scene scene, EcsSystemID id){
    System* system = MAPPING_GET(scene->systems, id);
    SelectionAccessCriteria selection = system->selection;
    for(uint32 i = 0; i < scene->entityTypeCount; i++){
        EntityType* type = scene->entityTypes + i;
        if(isQuerrySelected(selection, type->mask)){
            LIST_PUSH(type->systems, id);
            LIST_PUSH(system->entityTypes, type->typeID);
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
    type->typeID = id;
    type->scene = scene;

    LIST_INIT(type->systems, &scene->pool, EcsSystemID);

    registerSystems(scene, type);

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

    scene->systemChunkMem = darrayCreate(SystemChunk);
    initListPool(&scene->pool);
    MAPPING_CREATE(scene->systems, MAX_SYSTEMS);
    scene->systemOrder = darrayCreate(EcsSystemID);
    scene->frameDataCapacity = 0;
    scene->frameDataDescriptorCapacity = 0;
    scene->frameDataDescriptorCount = 0;
    scene->frameDataDescriptors = NULL;
    scene->frameDataMem = NULL;
    return scene;
}

void stagingBufferDestroy(Scene scene, AvThreadID threadId);
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
        stagingBufferDestroy(scene, scene->stagingBuffers[i].threadId);
    }
    MAPPING_DESTROY(scene->stagingBuffers);
    for(uint32 i = 0; i < MAPPING_SIZE(scene->systems); i++){
        destroySystemFromPtr(scene, scene->systems + i);
    }
    MAPPING_DESTROY(scene->systems);
    darrayDestroy(scene->systemChunkMem);
    darrayDestroy(scene->systemOrder);
    freeListPool(&scene->pool);
    
    if(scene->frameDataMem) avFree(scene->frameDataMem);
    if(scene->frameDataDescriptors) avFree(scene->frameDataDescriptors);
    
    avFree(scene);
}

AV_API void sceneSetSystemsOrder(Scene scene, uint32 systemsCount, EcsSystemID* systems){
    darrayLengthSet(scene->systemOrder, 0);
    for(uint32 i = 0; i < systemsCount; i++){
        darrayPush(scene->systemOrder, systems[i]);
    }
}

// static bool32 buildMasks(ComponentInfo* info, ComponentMask* mask){
//     avMemset(mask, 0, sizeof(ComponentMask));
//     bool32 ret = true;
//     uint32 maxCount = 4096;
//     while(info){
//         if(info->type >= MAX_COMPONENT_COUNT) {
//             ret = false;
//             continue;
//         }
//         if(MASK_HAS_COMPONENT(*mask, info->type)){
//             return false;
//         }else{
//             MASK_ADD_COMPONENT(*mask, info->type);
//         }
//         info = info->next;
//         maxCount--;
//         if(maxCount==0) return false;
//     }
//     return ret;
// }

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

bool32 moveEntity(Scene scene, Entity src, LocalEntity dst){
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
        byte* srcOffset = ((byte*)srcChunk->components[component]) + srcIndex*size;
        byte* dstOffset = ((byte*)dstChunk->components[component]) + dstIndex*size;
        avMemcpy(dstOffset, srcOffset, size);
    }
    ITERATE_MASK(zeroMem, component){
        uint32 size = getComponentSize(component);
        byte* srcOffset = ((byte*)srcChunk->components[component]) + srcIndex*size;
        byte* dstOffset = ((byte*)dstChunk->components[component]) + dstIndex*size;
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
            destroyEntityType(scene, srcType);
        }
    }

    return true;
}

void performDestructorMasked(Scene scene, uint32 i, EntityChunk* chunk, EntityType* type, ComponentMask mask){
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

static void createComponent(Scene scene, Entity entity, ComponentData data, ComponentType type, void* constructorData, uint32 constructorDataSize){
  
    ComponentConstructor constructor = getComponentConstructor(type);
    uint32 size = getComponentSize(type);
    if(constructor){
        constructor(scene, entity, data, size, constructorData, constructorDataSize);
    }
}

void performConstructorMasked(Scene scene, uint32 i, EntityChunk* chunk, EntityType* type, ComponentMask mask, void** constructorData, uint32* constructorDataSize){
    if(scene==NULL || type==NULL || chunk==NULL) return;
    if(i >= chunk->count) return;

    mask = componentMaskAnd(type->mask, mask);

    uint32 entityIndex = chunk->localIndex[i];

    uint32 index = 0;
    ITERATE_MASK(mask, component){
        uint32 size = getComponentSize(component);
        byte* base = (byte*) chunk->components[component];
        void* comp = base + entityIndex * size;
        avMemset(comp, 0, size);
        createComponent(scene, chunk->entities[entityIndex], comp, component, constructorData[component], constructorDataSize[component]);
        index++;
    }
}

// static void performConstructor(Scene scene, uint32 i, EntityChunk* chunk, EntityType* type, ComponentInfo* info){
//     if(type==NULL) return;
//     performConstructorMasked(scene, i, chunk, type, type->mask, info);
// }

bool32 removeEntity(Scene scene, Entity entity, EntityChunk* chunk, EntityType* type){
    uint32 lastIndex = chunk->count - 1;
    Entity lastEntity = getEntity(getChunkID(chunk), lastIndex);
    if(swapEntities(scene, entity, lastEntity)==false) return false;

    chunk->entities[lastIndex] = INVALID_ENTITY;
    freeEntityID(scene, entity);

    chunk->count--;

    if(chunk->count==0){
        destroyChunk(type, getChunkID(chunk));

        if(type->chunkCount==0){
            destroyEntityType(scene, type);
        }
    }
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
    
    removeEntity(scene, entity, chunk, type);

    return true;
}

LocalEntity createLocalEntity(EntityType* type, EntityChunk** chunkPtr, uint32* localIndexPtr){
    if(type==NULL) return INVALID_ENTITY;
    if(type->chunkCount==0){
        type->chunks = avAllocate(sizeof(uint32), "");
        type->chunkCapacity = 1;
        type->chunks[type->chunkCount++] = createChunk(type);
    }
    
    EntityChunk* chunk = NULL;
    for(uint32 i = 0; i < type->chunkCount; i++){
        EntityChunk* chnk = getChunk(type->chunks[i]);
        if(chnk==NULL) {
            avFatal("Logic error");
            return INVALID_ENTITY;
        };
        if(chnk->count < CHUNK_CAPACITY){
            chunk = chnk;
            break;
        }
    }

    if(chunk==NULL) {
        type->chunkCapacity *= 2;
        type->chunkCapacity = AV_MAX(type->chunkCapacity, 1);
        type->chunks = avReallocate(type->chunks, sizeof(uint32)*type->chunkCapacity, "");
        type->chunks[type->chunkCount++] = createChunk(type);
        chunk = getChunk(type->chunks[type->chunkCount-1]);
    }
    uint32 chunkID = getChunkID(chunk);
    uint32 localIndex = chunk->count++;

    LocalEntity localEntity =  ENTITY(chunkID, chunk->localID[localIndex]);
    if(localIndexPtr) *localIndexPtr = localIndex;
    if(chunkPtr) *chunkPtr = chunk;
    return localEntity;
}

bool32 createEmptyEntity(Scene scene, Entity entity, ComponentMask mask, EntityType** entityType, LocalEntity* locEntity, EntityChunk** chunkPtr){
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
    if(localEntity==INVALID_ENTITY)
        return false;
    chunk->entities[localIndex] = entity;
    modifyEntityID(scene, entity, localEntity, false);
    if(locEntity) *locEntity = localEntity;
    if(entityType) *entityType = type;
    if(chunkPtr) *chunkPtr = chunk;
    return true;
}

void stagingBufferCreate(Scene scene, AvThreadID threadId){
    if(scene == NULL || threadId >= AV_MAX_THREADS) return;

    CommandBuffer buffer = {0};
    buffer.threadId = threadId;
    commandBufferCreate(&buffer);
    MAPPING_INSERT(scene->stagingBuffers, threadId, buffer);
}

void stagingBufferDestroy(Scene scene, AvThreadID threadId){
    commandBufferDestroy(MAPPING_GET(scene->stagingBuffers, threadId));
    MAPPING_REMOVE(scene->stagingBuffers, threadId);
}

CommandBuffer* getStagingBuffer(Scene scene, AvThreadID threadId){
    if(scene==NULL || threadId >= AV_MAX_THREADS) return NULL;
    CommandBuffer* buffer = MAPPING_GET(scene->stagingBuffers, threadId);
    if(buffer==NULL){
        stagingBufferCreate(scene, threadId);
        return MAPPING_GET(scene->stagingBuffers, threadId);
    }
    return buffer;
}

EntityType* findOrCreateEntityType(Scene scene, ComponentMask mask){
    EntityType* type = findEntityType(scene, mask);
    if(type==NULL){
        type = createEntityType(scene, mask);
    }
    return type;
}

AV_API Entity entityCreate(Scene scene){
    AvThreadID threadId = avThreadGetID();
    if(threadId!=AV_MAIN_THREAD_ID){
        avWarn("Cannot create entity from other than main thread");
        return INVALID_ENTITY;
    }
    Entity entity = allocateEntityID(scene, 0, true);
    return entity;
}



AV_API bool32 entityAddComponent(Scene scene, Entity entity, ComponentType type, void* constructorData, uint32 constructorDataSize){
    AvThreadID threadId = avThreadGetID();
    cmdEntityAddComponent(scene, getStagingBuffer(scene, threadId), entity, type, constructorData, constructorDataSize);
    return true;
}

AV_API bool32 entityRemoveComponent(Scene scene, Entity entity, ComponentType component){
    AvThreadID threadId = avThreadGetID();
    cmdEntityRemoveComponent(scene, getStagingBuffer(scene, threadId), entity, component);
    return true;
}

AV_API bool32 entityDestroy(Scene scene, Entity entity){
    AvThreadID threadId = avThreadGetID();
    cmdEntityDestroy(scene, getStagingBuffer(scene, threadId), entity);
    return true;
}

AV_API bool32 entityHasComponent(Scene scene, Entity entity, ComponentType component){
    if(entity==INVALID_ENTITY) return false;
    bool8 isStaged = false;
    LocalEntity localEntity = INVALID_ENTITY;
    if(!getEntityDetails(scene, entity, NULL, &localEntity, NULL, &isStaged)) return false;
    if(isStaged){
        return false;
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

    uint32 size = getComponentSize(component);

    byte* offset = NULL;
    
    offset = (byte*) chunk->components[component];

    return offset + localIndex*size;
}


void* entityGetComponentFast(Scene scene, Entity entity, ComponentType component){
    AvThreadID threadId = avThreadGetID();
    if(threadId!=AV_MAIN_THREAD_ID) return NULL;
    EntityType* type = getType(scene, entity);
    EntityChunk* chunk = getEntityChunk(scene, entity);
    uint32 localIndex = getEntityLocalIndex(scene, entity);
    uint32 size = getComponentSize(component);
    byte* offset = NULL;
    offset = (byte*) chunk->components[component];
    return offset + localIndex*size;
}


void sceneApply(Scene scene){
    if(scene==NULL) return;
    AvThreadID threadId = avThreadGetID();
    if(threadId != AV_MAIN_THREAD_ID) {
        avWarn("Cannot apply scene from other than the main thread");
        return;
    };

    applyCommandBuffers(scene, scene->stagingBuffers, MAPPING_SIZE(scene->stagingBuffers));
}
