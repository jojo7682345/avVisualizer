#pragma once

#include "defines.h"


typedef uint16 ComponentType;
typedef uint32 Entity;
typedef uint32 EntityTypeID;


#define COMPONENT_MASK_SIZE 8
#define MAX_COMPONENT_COUNT (COMPONENT_MASK_SIZE * 64)
typedef struct ComponentMask{
    uint64 bits[COMPONENT_MASK_SIZE];
} ComponentMask;

#define INVALID_ENTITY ((Entity)-1)
#define INVALID_COMPONENT (MAX_COMPONENT_COUNT)

#define MASK_HAS_COMPONENT(mask, componentId) (((componentId) < MAX_COMPONENT_COUNT)&&((mask).bits[(componentId) >> 6] & (1ULL<<((componentId) & (63))))!=0)
#define MASK_ADD_COMPONENT(mask, componentId) if((componentId) < MAX_COMPONENT_COUNT) ((mask).bits[(componentId) >> 6] |= 1ULL << ((componentId) & 63))
#define MASK_REMOVE_COMPONENT(mask, componentId) if((componentId) < MAX_COMPONENT_COUNT) ((mask).bits[(componentId) >> 6] &= ~((1ULL<<((componentId) & (63)))))
// #define ITERATE_MASK(mask, index) \
//     uint32 index = 0; \
//     for(uint32 __index__ = 0; __index__ < COMPONENT_MASK_SIZE; __index__++) \
//     for(uint64 __bits__ = (mask).bits[__index__]; __bits__; __bits__ &= (__bits__ - 1)) \
//     if((index = (__index__ * 64 + __builtin_ctzll(__bits__))) || 1)

// #define ITERATE_MASK(mask, index) \
//     for(uint32 __index__ = 0; __index__ < COMPONENT_MASK_SIZE; __index__++) \
//     if((mask).bits[__index__])\
//     for(uint64 index = __builtin_ctzll((mask).bits[__index__]); index < MAX_COMPONENT_COUNT; index = __builtin_ctzll((mask).bits[__index__]>>(index+1)))

#define ITERATE_MASK(mask, index)                                      \
    for (uint32 __index__ = 0; __index__ < COMPONENT_MASK_SIZE; __index__++)        \
    for (uint64 __bits__ = (mask).bits[__index__]; __bits__; __bits__ &= (__bits__ - 1)) \
    for (uint32 index = (__index__ * 64 + __builtin_ctzll(__bits__)); index != ((uint32)-1); index = ((uint32)-1))

#define componentMaskMake(...) componentMaskMake_(__VA_ARGS__ __VA_OPT__(,) MAX_COMPONENT_COUNT)
AV_API ComponentMask componentMaskMake_(ComponentType first, ...);
AV_API bool32 componentMaskContains(ComponentMask mask, ComponentMask componentMask);
AV_API bool32 componentMaskEquals(ComponentMask maskA, ComponentMask maskB);
AV_API ComponentMask componentMaskAnd(ComponentMask maskA, ComponentMask maskB);
AV_API ComponentMask componentMaskOr(ComponentMask maskA, ComponentMask maskB);
AV_API ComponentMask componentMaskInvert(ComponentMask mask);
AV_API uint32 componentMaskCount(ComponentMask mask);
AV_API bool32 componentMaskIsEmpty(ComponentMask mask);

typedef struct Scene* Scene;
typedef void* ComponentData;
typedef struct ComponentInfo {
    ComponentType type;
    struct ComponentInfo* next;
} ComponentInfo;
typedef void (*ComponentConstructor)(Scene scene, Entity entity, ComponentData data, uint32 size, byte* constructorData, uint32 constructorDataSize);
typedef void (*ComponentDestructor)(Scene scene, Entity entity, ComponentData data, uint32 size);

AV_API bool32 registerComponent(ComponentType* component, uint32 size, ComponentConstructor constructor, ComponentDestructor destructor);

AV_API Scene sceneCreate();
AV_API void sceneDestroy(Scene scene);

typedef void* ComponentInfoRef;

AV_API Entity entityCreate(Scene scene, ComponentInfoRef info);
AV_API bool32 entityAddComponent(Scene scene, Entity entity, ComponentInfoRef info);
AV_API bool32 entityRemoveComponent(Scene scene, Entity entity, ComponentType type);
AV_API bool32 entityDestroy(Scene scene, Entity entity);

AV_API bool32 entityHasComponent(Scene scene, Entity entity, ComponentType type);

// NOTE: pointer does not remain valid after any changes to this or other entities
AV_API void* entityGetComponentRead(Scene scene, Entity entity, ComponentType type);
// NOTE: pointer does not remain valid after any changes to this or other entities
// All checks are removed for speed
AV_API void* entityGetComponentReadFast(Scene scene, Entity entity, ComponentType type);

// NOTE: pointer does not remain valid after any changes to this or other entities
AV_API void* entityGetComponentWrite(Scene scene, Entity entity, ComponentType type);
// NOTE: pointer does not remain valid after any changes to this or other entities
// All checks are removed for speed
AV_API void* entityGetComponentWriteFast(Scene scene, Entity entity, ComponentType type);


AV_API void sceneApply(Scene scene);

