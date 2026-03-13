#pragma once
#include "defines.h"

#include <stdarg.h>

#define COMPONENT_MASK_SIZE 8
#define MAX_COMPONENT_COUNT (COMPONENT_MASK_SIZE * 64)
typedef struct ComponentMask{
    uint64 bits[COMPONENT_MASK_SIZE];
} ComponentMask;
#define MASK_HAS_COMPONENT(mask, componentId) (((componentId) < MAX_COMPONENT_COUNT)&&((mask).bits[(componentId) >> 6] & (1ULL<<((componentId) & (63))))!=0)
#define MASK_ADD_COMPONENT(mask, componentId) if((componentId) < MAX_COMPONENT_COUNT) ((mask).bits[(componentId) >> 6] |= 1ULL << ((componentId) & 63))
#define MASK_REMOVE_COMPONENT(mask, componentId) if((componentId) < MAX_COMPONENT_COUNT) ((mask).bits[(componentId) >> 6] &= ~((1ULL<<((componentId) & (63)))))
static inline bool32 checkMask(ComponentMask mask, ComponentMask componentMask) {
    for(uint32 i = 0; i < COMPONENT_MASK_SIZE; i++)
    {
        if((mask.bits[i] & componentMask.bits[i]) != componentMask.bits[i])
            return 0;
    }
    return 1;
}

// #define ITERATE_MASK(mask, index) \
//     uint32 index = 0;\
//     for(uint32 __index__ = 0; __index__ < COMPONENT_MASK_SIZE; __index__++)\
//     for(uint32 __bit__ = 0; __bit__ < 64; __bit__++) \
//     if((mask).bits[__index__])\
//     if((mask).bits[__index__] & (1ULL<<__bit__) && ((index=(__index__*64+__bit__)) || 1)) 


#define ITERATE_MASK(mask, index) \
    uint32 index = 0; \
    for(uint32 __index__ = 0; __index__ < COMPONENT_MASK_SIZE; __index__++) \
    for(uint64 __bits__ = (mask).bits[__index__]; __bits__; __bits__ &= (__bits__ - 1)) \
    if((index = (__index__ * 64 + __builtin_ctzll(__bits__))) || 1)

typedef uint16 ComponentID;
#define INVALID_COMPONENT_ID ((uint16)-1)
AV_API ComponentMask componentMaskMake_(ComponentID first, ...);
#define componentMaskMake(...) componentMaskMake_(__VA_ARGS__ __VA_OPT__(,) MAX_COMPONENT_COUNT)

// pools dont resize so pointers are stable
typedef void* Component;
typedef uint32 EntityType; // link to entityPool
typedef uint32 Entity;

#define ENTITY_POOL(entity) ((entity) >> 12)
#define ENTITY_ID(entity) ((entity & (0xfff)))
#define ENTITY(type, id) (((type) << 12)|((id)&0xfff))

typedef struct Scene* Scene;

#define INVALID_ENTITY_TYPE ((uint32)-1)

typedef void (*ComponentConstructor)(Scene scene, Entity entity, Component data, uint32 size, va_list args);
typedef void (*ComponentDestructor)(Scene scene, Entity entity, Component data, uint32 size);

AV_API bool32 registerComponent(ComponentID* component, uint32 size, ComponentConstructor constructor, ComponentDestructor destructor);

AV_API Scene sceneCreate();
AV_API void sceneDestroy(Scene scene);

AV_API EntityType entityTypeCreate(Scene scene, uint32 componentCount, ComponentID* components);
AV_API void entityTypeDestroy(Scene scene, EntityType type);

AV_API Entity entityCreate(Scene scene, EntityType type);
AV_API Component entityAddComponent(Scene scene, Entity entity, ComponentID component, ...);
AV_API Component entityGetComponent(Scene scene, Entity entity, ComponentID component);
AV_API bool32 entityRemoveComponent(Scene scene, Entity entity, ComponentID component);
AV_API bool32 entityDestroy(Scene scene, Entity entity);
AV_API EntityType entityGetType(Entity entity);

typedef void (*System)(Scene scene, const Entity entity, const uint32 componentCount, const ComponentID* componentIndex, const Component* components, const uint32* componentSizes);

AV_API void scenePerformForEntities(Scene scene, ComponentID component, System system);
AV_API void scenePerformForEntitiesMasked(Scene scene, ComponentMask mask, System system);

