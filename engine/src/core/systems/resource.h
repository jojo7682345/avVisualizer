#pragma once
#include "defines.h"
#include "ecs.h"


typedef uint32 EngineResource;

typedef enum EngineResourceScope {
    RESOURCE_SCOPE_FRAME, // TRANSIENT (MEMORY THAT CAN BE REUSED WITHIN A FRAME MAYBE)
    RESOURCE_SCOPE_SCENE,
    RESOURCE_SCOPE_GLOBAL,
} EngineResourceScope;

typedef struct EngineResourceFlags {
    bool32 resizable : 1;

} EngineResourceFlags;


EngineResource registerEngineResource(uint64 size, EngineResourceScope scope, Scene scene);
void* accessEngineResource(EngineResource resource, Scene scene, uint64* size);
bool32 resizeEngineResource(EngineResource resource, Scene scene, )