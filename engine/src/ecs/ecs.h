#pragma once

#include "defines.h"

#include "componentMask.h"
#include "jobs/jobs.h"

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

typedef uint32 EcsSystemID;

AV_API void sceneSetSystemsOrder(Scene scene, uint32 systemsCount, EcsSystemID* systems);

typedef struct SelectionAccessCriteria {
    ComponentMask requiredRead; // entities must contain
    ComponentMask requiredWrite; // entities must contain
    ComponentMask excluded; // entities must not contain (internally checked)
} SelectionAccessCriteria;

typedef enum SystemExecution {
    SYSTEM_EXECUTE_ASYNC,
    SYSTEM_EXECUTE_SEMI_SYNCHRONOUS,
    SYSTEM_EXECUTE_SYNCHRONOUS
} SystemExecution;

typedef struct SystemChunk {
    uint32 chunkId;
    uint32 entityCount;
    const Entity* entities;
    ComponentMask components;
    const ComponentData* componentData;
} SystemChunk;

typedef JobControl (*SystemProcessFn)(Scene scene, void* ctx, uint32 entityCount, const Entity* entities, const ComponentData* components, JobContext* context);

// JobControl exampleSystemProcess(Scene scene, void* ctx, uint32 entityCount, Entity* entities, const ComponentData* components, JobContext* context);

// all submited jobBathces must use the provided fence (dependencies may be used)
// and return the jobBatchID of the last running job
typedef JobBatchID (*SystemDispatchFn)(Scene scene, void* ctx, SystemProcessFn process, uint32 chunkCount, SystemChunk* chunks, JobFence fence, uint32 dependencyCount, JobBatchID* dependencies);

// dispatches jobs for each chunk (and build accessors) (use the fence provided)
JobBatchID dispatchChunks(Scene scene, EcsSystemID systemID, uint32 chunkCount, SystemChunk* chunks, JobFence fence, uint32 dependencyCount, JobBatchID* dependencies);
EcsSystemID createSystem(Scene scene, SelectionAccessCriteria selection, SystemExecution execution, SystemProcessFn process, void* ctx);
EcsSystemID createSystemCustom(Scene scene, SelectionAccessCriteria selection, SystemExecution execution, SystemProcessFn process, SystemDispatchFn dispatchOverride, void* ctx);

void enableSystem(Scene scene, EcsSystemID system, bool32 enable);
bool32 isSystemEnabled(Scene scene, EcsSystemID system);

void destroySystem(Scene scene, EcsSystemID system);


// returns batch id of last running jobBatch, and registers everything to the fence
JobBatchID sceneRunSystems(Scene scene, JobFence fence);