#include "ecs.h"
#include <AvUtils/avMemory.h>


typedef struct ComponentEntry {
	uint32 size;
    ComponentConstructor constructor;
    ComponentDestructor destructor;
} ComponentEntry;

typedef struct ComponentRegistry {
	ComponentMask registeredComponents;
	ComponentEntry entries[MAX_COMPONENT_COUNT];
} ComponentRegistry;

static ComponentRegistry componentRegistry = {0};

typedef struct ComponentStorage {
    ComponentID id;
	uint32 stride;
    byte data[];
} ComponentStorage;
#define COMPONENT_AT(storage, index) ((Component)((storage)->data + (index)*(storage)->stride))
/*struct Entity_T {
    uint16 capacity;
    uint16 size;
    ComponentMask mask;
    ComponentRef components[];
}*/

#define ENTITY_POOL_SIZE 4096
typedef struct EntityPool {
    struct Scene* scene;
    EntityType type;
    ComponentMask mask; // mask of possible component of entities in this pool
    uint16 entityCount; // numer of active entities
    uint16 componentCount;
    struct Entity_T* entities;
    bool8 entityActive[ENTITY_POOL_SIZE];
    uint16 componentIndex[MAX_COMPONENT_COUNT]; //componentType to componentIndex table
    ComponentStorage** components; // component storage

} EntityPool;

struct Scene {
    uint32 entityTypeCapacity;
    uint32 entityTypeCount;
    EntityPool** entityPools;
    uint32* entityTypeIndex;
    EntityType* entityTypeID;
};

#define ARRAY_INIT_CAPACITY 1
#define darrayPush(array, x)\
do{																									\
	if(array == NULL) {																				\
		DarrayHeader* header = avAllocate(sizeof(*(array))*ARRAY_INIT_CAPACITY + sizeof(DarrayHeader), "");	\
		header->count = 0;																			\
		header->capacity = ARRAY_INIT_CAPACITY;														\
		array = (void*)(header + 1);																\
	}																								\
	DarrayHeader* header = (DarrayHeader*)(array) - 1;												\
	if(header->count >= header->capacity){															\
		header->capacity *= 2;																		\
		header = avReallocate(header, sizeof(*(array))*header->capacity + sizeof(DarrayHeader), "");			\
		array = (void*)(header + 1);																\
	}																								\
	(array)[header->count++] = (x);																	\
} while(0)																							\

#define darrayLength(array) ((DarrayHeader*)(array) -1)->count
#define darrayFree(array) avFree((DarrayHeader*)(array) - 1)

struct Entity_T {
	ComponentMask mask;
	uint16 componentCount;
};

#define SCENE_INITIAL_SIZE 1
Scene sceneCreate(){
	Scene scene = avAllocate(sizeof(struct Scene), "");
	avMemset(scene, 0, sizeof(struct Scene));
	scene->entityPools			= avAllocate(sizeof(EntityPool*)	* SCENE_INITIAL_SIZE, "");
	scene->entityTypeID			= avAllocate(sizeof(EntityType)		* SCENE_INITIAL_SIZE, "");
	scene->entityTypeIndex		= avAllocate(sizeof(uint32)			* SCENE_INITIAL_SIZE, "");
	scene->entityTypeCapacity	= SCENE_INITIAL_SIZE;
	scene->entityTypeCount		= 0;
	for(uint32 i = 0; i < SCENE_INITIAL_SIZE; i++){
		scene->entityPools[i]		= NULL;
		scene->entityTypeID[i]		= i;
		scene->entityTypeIndex[i]	= i;
	}
	return scene;
}

static void entityPoolDestroy(Scene scene, EntityPool* pool);

void sceneDestroy(Scene scene){
	for(uint32 i = 0; i < scene->entityTypeCount; i++){
		entityPoolDestroy(scene, scene->entityPools[i]);
		avFree(scene->entityPools[i]);
	}
	avFree(scene->entityPools);
	avFree(scene->entityTypeIndex);
	avFree(scene->entityTypeID);
	avMemset(scene, 0, sizeof(struct Scene));
	avFree(scene);
}

static void increaseEntityPools(Scene scene){
	uint32 oldCapacity = scene->entityTypeCapacity;
	scene->entityTypeCapacity *= 2;
	scene->entityPools = avReallocate(scene->entityPools, scene->entityTypeCapacity * sizeof(EntityPool*), "");
	scene->entityTypeID = avReallocate(scene->entityTypeID, scene->entityTypeCapacity * sizeof(EntityType), "");
	scene->entityTypeIndex = avReallocate(scene->entityTypeIndex, scene->entityTypeCapacity * sizeof(uint32), "");
	for(uint32 i = oldCapacity; i < scene->entityTypeCapacity; i++){
		scene->entityPools[i] = NULL;
		scene->entityTypeID[i] = i;
		scene->entityTypeIndex[i] = i;
	}
}

static EntityPool* addPool(Scene scene, EntityType* type){
	uint32 index = scene->entityTypeCount;
	if(index >= scene->entityTypeCapacity){
		increaseEntityPools(scene);
	}
	scene->entityPools[index] = avAllocate(sizeof(EntityPool), "");
	*type = scene->entityTypeID[index];
	scene->entityTypeCount++;
	return scene->entityPools[index];
}

static EntityPool* removePool(Scene scene, EntityType type){
	if(scene==NULL) return NULL;
	if(scene->entityTypeCount == 0) return NULL;
	if(type >= scene->entityTypeCapacity) return NULL;
	uint32 index = scene->entityTypeIndex[type];
	if(index >= scene->entityTypeCount) return NULL;
	EntityPool* pool = scene->entityPools[index];
	if(pool == NULL) return NULL;

	uint32 lastIndex = scene->entityTypeCount - 1;

	//swap
	EntityType lastId = scene->entityTypeID[lastIndex];
	EntityPool* lastPool = scene->entityPools[lastIndex];

	scene->entityPools[index] = lastPool;
	scene->entityTypeID[index] = lastId;

	scene->entityPools[lastIndex] = pool;
	scene->entityTypeID[lastIndex] = type;

	scene->entityTypeIndex[lastId] = index;
	scene->entityTypeIndex[type] = lastIndex;

	//remove last
	scene->entityPools[lastIndex] = NULL;
	scene->entityTypeCount--;
	return pool;
}

static EntityPool* getPool(Scene scene, EntityType* type){
	if(scene==NULL){
		return NULL;
	}
	if(type==NULL){
		return NULL;
	}
	if(*type==INVALID_ENTITY_TYPE){
		return addPool(scene, type);
	}
	if(*type >= scene->entityTypeCapacity){
		return NULL;
	}
	if(scene->entityTypeIndex[*type]>=scene->entityTypeCount){
		return NULL;
	}
	return scene->entityPools[scene->entityTypeIndex[*type]];
}

static ComponentMask buildComponentMask(uint32 componentCount, ComponentID* components){
	ComponentMask mask = {0};
	for(uint32 i = 0; i < componentCount; i++){
		ComponentID id = components[i];
		if(id >= MAX_COMPONENT_COUNT){
			continue;
		}
		uint32 offset = id >> 6;
		mask.bits[offset] |= 1ULL << (id & 63);
	}
	return mask;
}

static inline uint32 getComponentCount(ComponentMask mask){
	uint32 count = 0;
	for(uint32 i = 0; i < COMPONENT_MASK_SIZE; i++){
		for(uint32 j = 0; j < 64; j++){
			count += (mask.bits[i] >> j) & 1;
		}
	}
	return count;
}

EntityType entityTypeCreate(Scene scene, uint32 componentCount, ComponentID* components){
	if(scene==NULL || componentCount >= MAX_COMPONENT_COUNT || componentCount == 0 || components==NULL){
		return INVALID_ENTITY_TYPE;
	}
	ComponentMask mask = buildComponentMask(componentCount, components);
	if(!checkMask(componentRegistry.registeredComponents, mask)){
		return INVALID_ENTITY_TYPE;
	}

	EntityType type = INVALID_ENTITY_TYPE;
	EntityPool* pool = getPool(scene, &type);
	if(pool==NULL){
		return INVALID_ENTITY_TYPE;
	}
	pool->scene = scene;
	pool->type = type;
	pool->mask = mask;
	pool->entityCount = 0;
	pool->entities = avAllocate(sizeof(struct Entity_T) * ENTITY_POOL_SIZE, "");
	for(uint32 i = 0; i < ENTITY_POOL_SIZE; i++){
		pool->entityActive[i] = false;
		avMemset(pool->entities +  i, 0, sizeof(struct Entity_T));
	}
	for(uint32 i = 0; i < MAX_COMPONENT_COUNT; i++){
		pool->componentIndex[i] = (uint16) INVALID_ENTITY_TYPE;
	}
	pool->componentCount = getComponentCount(pool->mask);
	pool->components = avAllocate(sizeof(ComponentStorage*) * pool->componentCount, "");
	uint32 i = 0;
	ITERATE_MASK(pool->mask, component){
		uint32 componentSize = componentRegistry.entries[component].size;
		pool->components[i] = avAllocate(sizeof(ComponentStorage) + componentSize*ENTITY_POOL_SIZE, "");
		avMemset(pool->components[i], 0, sizeof(ComponentStorage) + componentSize*ENTITY_POOL_SIZE);
		if(component >= MAX_COMPONENT_COUNT) continue;
		pool->componentIndex[component] = i;
		pool->components[i]->id = component;
		pool->components[i]->stride = componentSize;
		i++;
	}

	return type;
}

static void entityPoolDestroy(Scene scene, EntityPool* pool){
	uint32 componentCount = getComponentCount(pool->mask);
	for(uint32 j = 0; j < componentCount; j++){
		ComponentStorage* storage = pool->components[j];
		ComponentDestructor destructor =  componentRegistry.entries[storage->id].destructor;
		if(destructor==NULL){
			avFree(storage);
			continue;
		}
		for(uint32 i = 0; i < ENTITY_POOL_SIZE; i++){
			if(pool->entityActive[i]==false)continue;
			struct Entity_T* entity = &pool->entities[i];
			if(MASK_HAS_COMPONENT(entity->mask, storage->id)){
				destructor(scene, ENTITY(pool->type, i), storage->data + storage->stride*i, storage->stride);
			}
		}
		avFree(storage);
	}
	avFree(pool->components);
	avFree(pool->entities);
}
void entityTypeDestroy(Scene scene, EntityType type){
	EntityPool* pool = removePool(scene, type);
	if(pool==NULL){
		return;
	}
	entityPoolDestroy(scene, pool);
}

bool32 registerComponent(ComponentID* component, uint32 size, ComponentConstructor constructor, ComponentDestructor destructor){
	if(component==NULL) return false;
	//if(size==0) return false;
	if(*component==INVALID_COMPONENT_ID) {
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

Entity entityCreate(Scene scene, EntityType type){
	if(type==INVALID_ENTITY_TYPE){
		return INVALID_ENTITY_TYPE;
	}
	EntityPool* pool = getPool(scene, &type);
	if(pool==NULL){
		return INVALID_ENTITY_TYPE;
	}
	if(pool->entityCount >= ENTITY_POOL_SIZE){
		return INVALID_ENTITY_TYPE;
	}
	for(uint32 i = 0; i < ENTITY_POOL_SIZE; i++){
		if(pool->entityActive[i]==false){
			pool->entityActive[i] = true;
			pool->entityCount++;
			return ENTITY(type, i);
		}
	}
	return INVALID_ENTITY_TYPE;
}

static Component getComponentInfo(Scene scene, Entity entityID, ComponentID component, struct Entity_T** entity, ComponentStorage** storage, bool32 expectComponent){
	ComponentStorage* tmpStorage = NULL;
	struct Entity_T* tmpEntity = NULL;
	if(entity==NULL) entity = &tmpEntity;
	if(storage==NULL) storage = &tmpStorage;

	EntityType type = ENTITY_POOL(entityID);
	EntityPool* pool = getPool(scene, &type);
	if(pool == NULL){
		return NULL;
	}
	if(component >= MAX_COMPONENT_COUNT){
		return NULL;
	}
	if(!MASK_HAS_COMPONENT(pool->mask, component)){
		return NULL;
	}
	uint32 id = ENTITY_ID(entityID);
	if(id >= ENTITY_POOL_SIZE){
		return NULL;
	}
	if(!pool->entityActive[id]){
		return NULL;
	}
	*entity = &pool->entities[id];
	if(MASK_HAS_COMPONENT((*entity)->mask, component)!=expectComponent){
		return NULL;
	}
	uint32 componentIndex = pool->componentIndex[component];
	(*storage) = pool->components[componentIndex];
	return (*storage)->data + (*storage)->stride*id;
}


Component entityAddComponent(Scene scene, Entity entityID, ComponentID component, ...){
	struct Entity_T* entity = NULL;
	ComponentStorage* storage = NULL;
	Component componentData = getComponentInfo(scene, entityID, component, &entity, &storage, false);
	if(componentData==NULL){
		return NULL;
	}
	MASK_ADD_COMPONENT(entity->mask, component);
	entity->componentCount++;
	
	avMemset(componentData, 0, storage->stride);
	ComponentConstructor constructor = componentRegistry.entries[component].constructor;
	if(constructor){
		va_list args;
		va_start(args, component);
		constructor(scene, entityID, componentData, storage->stride, args);
		va_end(args);
	}
	return componentData;

}
Component entityGetComponent(Scene scene, Entity entityID, ComponentID component){
	return getComponentInfo(scene, entityID, component, NULL, NULL, true);
}
bool32 entityRemoveComponent(Scene scene, Entity entityID, ComponentID component){
	struct Entity_T* entity = NULL;
	ComponentStorage* storage = NULL;
	Component componentData = getComponentInfo(scene, entityID, component, &entity, &storage, true);
	if(componentData==NULL){
		return false;
	}
	MASK_REMOVE_COMPONENT(entity->mask, component);
	ComponentDestructor destructor =  componentRegistry.entries[storage->id].destructor;
	if(destructor){
		destructor(scene, entityID, componentData, storage->stride);
	}
	entity->componentCount--;
	//avMemset(componentData, 0, storage->stride);
	return true;
}

bool32 entityDestroy(Scene scene, Entity entity){
	EntityType type = ENTITY_POOL(entity);
	EntityPool* pool = getPool(scene, &type);
	if(pool == NULL){
		return false;
	}
	if(pool->entityCount == 0){
		return false;
	}

	uint32 id = ENTITY_ID(entity);
	if(id >= ENTITY_POOL_SIZE){
		return false;
	}
	if(pool->entityActive[id]==false){
		return false;
	}

	for(uint32 i = 0; i < pool->componentCount; i++){
		ComponentStorage* storage = pool->components[i];
		if(MASK_HAS_COMPONENT(pool->entities[id].mask, storage->id) && componentRegistry.entries[storage->id].destructor){
			componentRegistry.entries[storage->id].destructor(scene, entity, storage->data + storage->stride*id, storage->stride);
		}
	}

	pool->entityActive[id] = false;
	avMemset(&pool->entities[id], 0, sizeof(struct Entity_T));
	pool->entityCount--;
	return true;
}

void scenePerformForEntities(Scene scene, ComponentID component, System system){
	if(scene==NULL){
		return;
	}
	for(uint32 i = 0; i < scene->entityTypeCount; i++){
		EntityPool* pool = scene->entityPools[i];

		if(MASK_HAS_COMPONENT(pool->mask, component)==false){
			continue;
		}
		uint32 componentIndex = pool->componentIndex[component];
		uint32 componentSize = pool->components[componentIndex]->stride;
		Component componentPtr = pool->components[componentIndex]->data;
		for(uint32 j = 0; j < ENTITY_POOL_SIZE; j++){
			if(pool->entityActive[j]==false) continue;
			struct Entity_T* entity = pool->entities + j;
			if(entity->componentCount < 1) continue;
			if(MASK_HAS_COMPONENT(entity->mask, component)==false) continue;
			Component localComponent = ((byte*)componentPtr) + j * componentSize;
			system(scene, ENTITY(pool->type, j), 1, &component, &localComponent, &pool->components[componentIndex]->stride);
		}
	}
}
void scenePerformForEntitiesMasked(Scene scene, ComponentMask mask, System system){
	if(scene==NULL){
		return;
	}
	uint32 componentCount;
	if((componentCount = getComponentCount(mask))==0){
		return;
	}
	ComponentID componentIndex[componentCount];
	Component components[componentCount];
	Component localComponents[componentCount];
	uint32 componentSizes[componentCount];

	for(uint32 i = 0; i < scene->entityTypeCount; i++){
		EntityPool* pool = scene->entityPools[i];

		if(checkMask(pool->mask, mask)==false){
			continue;
		}

		// build component index
		uint32 writeIndex = 0;
		for(uint32 j = 0; j < pool->componentCount; j++){
			ComponentStorage* storage = pool->components[j];
			if(!MASK_HAS_COMPONENT(mask, storage->id)) continue;
			
			componentIndex[writeIndex] = storage->id;
			componentSizes[writeIndex] = storage->stride;
			components[writeIndex] = storage->data;
			writeIndex++;
		}

		for(uint32 j = 0; j < ENTITY_POOL_SIZE; j++){
			if(pool->entityActive[j]==false) continue;
			struct Entity_T* entity = pool->entities + j;
			if(checkMask(entity->mask, mask)==false) continue;
			for(uint32 k = 0; k < componentCount; k++){
				localComponents[k] = ((byte*)components[k]) +  componentSizes[k]*j;
			}

			system(scene, ENTITY(pool->type, j), componentCount, componentIndex, localComponents, componentSizes);
		}

	}
}

ComponentMask componentMaskMake_(ComponentID first, ...){
	va_list args;
	va_start(args, first);
	ComponentMask mask = {0};
	ComponentID id = first;
	while(id<MAX_COMPONENT_COUNT){
		MASK_ADD_COMPONENT(mask, id);
		id = (ComponentID)va_arg(args, uint32);
	}
	return mask;
}

EntityType entityGetType(Entity entity){
	return ENTITY_POOL(entity);
}