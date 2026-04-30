#pragma once

#include "defines.h"

#include "componentMask.h"

typedef uint16 ComponentType;
typedef uint32 Entity;
typedef uint32 EntityTypeID;




#define INVALID_ENTITY ((Entity)-1)
#define INVALID_COMPONENT (MAX_COMPONENT_COUNT)

#define MAX_SYSTEMS 4096


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

AV_API Entity entityCreate(Scene scene);
AV_API bool32 entityAddComponent(Scene scene, Entity entity, ComponentType type, void* constructorData, uint32 constructorDataSize);
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

