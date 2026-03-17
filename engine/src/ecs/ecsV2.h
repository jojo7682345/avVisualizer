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
#define ITERATE_MASK(mask, index) \
    uint32 index = 0; \
    for(uint32 __index__ = 0; __index__ < COMPONENT_MASK_SIZE; __index__++) \
    for(uint64 __bits__ = (mask).bits[__index__]; __bits__; __bits__ &= (__bits__ - 1)) \
    if((index = (__index__ * 64 + __builtin_ctzll(__bits__))) || 1)

#define componentMaskMake(...) componentMaskMake_(__VA_ARGS__ __VA_OPT__(,) MAX_COMPONENT_COUNT)
AV_API ComponentMask componentMaskMake_(ComponentType first, ...);
AV_API bool32 componentMaskContains(ComponentMask mask, ComponentMask componentMask);
AV_API bool32 componentMaskEquals(ComponentMask maskA, ComponentMask maskB);
AV_API ComponentMask componentMaskAnd(ComponentMask maskA, ComponentMask maskB);
AV_API ComponentMask componentMaskOr(ComponentMask maskA, ComponentMask maskB);
AV_API ComponentMask componentMaskInvert(ComponentMask mask);

typedef struct Scene* Scene;
typedef void* ComponentData;
typedef struct ComponentInfo {
    ComponentType type;
    struct ComponentInfo* next;
} ComponentInfo;
typedef void (*ComponentConstructor)(Scene scene, Entity entity, ComponentData data, uint32 size, ComponentInfo* info);
typedef void (*ComponentDestructor)(Scene scene, Entity entity, ComponentData data, uint32 size);

AV_API bool32 registerComponent(ComponentType* component, uint32 size, ComponentConstructor constructor, ComponentDestructor destructor);

AV_API Scene sceneCreate();
AV_API void sceneDestroy(Scene scene);

AV_API Entity entityCreate(Scene scene, ComponentInfo* info);
AV_API bool32 entityAddComponent(Scene scene, Entity entity, ComponentInfo* info);
AV_API bool32 entityRemoveComponent(Scene scene, Entity entity, ComponentType type, uint32 index);
AV_API bool32 entityRemoveComponentType(Scene scene, Entity entity, ComponentType type);
AV_API bool32 entityDestroy(Scene scene, Entity entity);