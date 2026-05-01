#include "ecsQuerries.h"
#include "containers/idMapping.h"
#include "containers/darray.h"

bool32 isQuerrySelected(SelectionAccessCriteria criteria, ComponentMask mask){
    ComponentMask required = componentMaskOr(criteria.requiredRead, criteria.requiredWrite);
    ComponentMask present = componentMaskAnd(required, mask);
    if(!componentMaskEquals(present, required)){
        return false;
    }
    if(!componentMaskIsEmpty(componentMaskAnd(criteria.excluded, mask))){
        return false;
    }
    return true;
}

static JobControl chunkDispatch(byte* input, uint32 inputSize, byte* output, uint32 outputSize, JobContext* context){
    System* system = (System*) input;
    SystemChunk* chunk = (SystemChunk*) output;
    JobControl control = system->process(system->scene, system->ctx, chunk->entityCount, chunk->entities, chunk->componentData, context);
    if(control.ret==JOB_EXIT_UNDEFINED){
        control.ret = JOB_EXIT_NORMAL;
    }
    return control;
}

JobBatchID dispatchChunksFromSystemPtr(Scene scene, System* system, uint32 chunkCount, SystemChunk* chunks, JobFence fence, uint32 dependencyCount, JobBatchID* dependencies){
    JobBatchDescription batch = {
        .size = chunkCount,
        .flags.priority = JOB_PRIORITY_MEDIUM,
        .flags.completeThisFrame = false, // this will be handled by the fence
        .inputData = system, // the system pointer will remain stable while the systems are running (so this is fine)
        .inputStride = 0, // give each job the same input
        .outputData = chunks, 
        .outputStride = sizeof(SystemChunk),
        .inputOffset = 0,
        .outputOffset = 0,
        .onComplete = NULL,
        .entry = chunkDispatch,
    };
    return submitJobBatchWithDependencies(&batch, dependencyCount, dependencies, fence);
}

JobBatchID dispatchChunks(Scene scene, EcsSystemID systemID, uint32 chunkCount, SystemChunk* chunks, JobFence fence, uint32 dependencyCount, JobBatchID* dependencies){
    System* system = MAPPING_GET(scene->systems, systemID);
    if(system==NULL) {
        avError("Tried to access invalid system");
        return JOB_BATCH_NONE;
    }
    return dispatchChunksFromSystemPtr(scene, system, chunkCount, chunks, fence, dependencyCount, dependencies);
}

EcsSystemID createSystem(Scene scene, SelectionAccessCriteria selection, SystemExecution execution, SystemProcessFn process, void* ctx){
    return createSystemCustom(scene, selection, execution, process, NULL, ctx);
}

EcsSystemID createSystemCustom(Scene scene, SelectionAccessCriteria selection, SystemExecution execution, SystemProcessFn process, SystemDispatchFn dispatchOverride, void* ctx){
    System system = {
        .ctx = ctx,
        .scene = scene,
        .enabled = true,
        .selection = selection,
        .execution = execution,
        .process = process,
        .dispatchOverride = dispatchOverride,
    };
    LIST_INIT(system.entityTypes, &scene->pool, EntityTypeID);
    EcsSystemID id = MAPPING_ADD(scene->systems, system);
    registerNewSystem(scene, id);
    return id;
}

void destroySystem(Scene scene, EcsSystemID system){
    System* sys = MAPPING_GET(scene->systems, system);
    if(sys == NULL){
        avError("tried to access invalid system");
        return;
    }
    unregisterSystemFromEntityTypes(scene, system);
    destroySystemFromPtr(scene, sys);
    MAPPING_REMOVE(scene->systems, system);
}

void destroySystemFromPtr(Scene scene, System* sys){
    LIST_FREE(sys->entityTypes);
}

void collectChunks(Scene scene, System* system, uint32* offset, uint32* count){
    *offset = darrayLength(scene->systemChunkMem);
    LIST_FOR(system->entityTypes, EntityTypeID, t){
        EntityType* type = getEntityType(scene, *t);
        for(uint32 i = 0; i < type->chunkCount; i++){
            EntityChunk* chunk = getChunk(type->chunks[i]);
            SystemChunk sysChunk = {
                .chunkId = type->chunks[i],
                .components = type->mask,
                .entities = chunk->entities,
                .entityCount = chunk->count,
                .componentData = chunk->components,
            };
            darrayPush(scene->systemChunkMem, sysChunk);
        }
    }
    *count = darrayLength(scene->systemChunkMem) - *offset;
}

static inline void collectDependencies(uint32* dependencyCount, JobBatchID* dependencies, ComponentMask readMask, ComponentMask writeMask, uint32* lastReader, uint32* lastWriter, uint32 index, JobBatchID* batches){
    *dependencyCount = 0; //reset dependencies
        
    ITERATE_MASK(writeMask, c){
        uint32 writeDep = lastWriter[c];
        uint32 readDep = lastReader[c];
        if(lastWriter[c]==(uint32)-1 && lastReader[c]==(uint32)-1) continue;
        
        bool32 foundRead = false;
        bool32 foundWrite = false;
        for(uint32 i = 0; i < *dependencyCount; i++){
            if(writeDep != (uint32)-1 && dependencies[i]==batches[writeDep]) {
                foundWrite = true;
            }
            if(readDep != (uint32)-1 && dependencies[i]==batches[readDep]){
                foundRead = true;
            }
        }
        if(writeDep != (uint32)-1 && !foundWrite){
            dependencies[(*dependencyCount)++] = batches[writeDep];
        }
        if(readDep != (uint32)-1 && readDep != writeDep &&!foundRead){
            dependencies[(*dependencyCount)++] = batches[readDep];
        }

        lastWriter[c] = index;
    }
    ITERATE_MASK(readMask, c){
        lastReader[c] = index;
    }
}

JobBatchID sceneRunSystems(Scene scene, JobFence systemFence, uint32* jobBatchCount, JobBatchID* dependencies){

    uint32 systemCount = darrayLength(scene->systemOrder);
    if(systemCount == 0) return;
    EcsSystemID* systemIds = scene->systemOrder;

    uint32 offsets[systemCount];
    uint32 counts[systemCount];
    JobBatchID batches[systemCount];

    uint32 lastWriter[MAX_COMPONENT_COUNT];
    uint32 lastReader[MAX_COMPONENT_COUNT];
    for (uint32 i = 0; i < MAX_COMPONENT_COUNT; i++) {
        lastWriter[i] = (uint32)-1;
        lastReader[i] = (uint32)-1;
    }

    JobBatchID dependencies[systemCount];
    uint32 dependencyCount = 0;

    darrayClear(scene->systemChunkMem); // reset memory

    for(uint32 index = 0; index < systemCount; index++){

        System* system = MAPPING_GET(scene->systems, systemIds[index]);
        if(system == NULL){
            avError("tried to access invalid system");
            return;
        }
        collectChunks(scene, system, offsets + index, counts + index);
        
        if(counts[index]==0) continue;

        ComponentMask readMask = system->selection.requiredRead;
        ComponentMask writeMask = system->selection.requiredWrite;

        collectDependencies(&dependencyCount, dependencies, readMask, writeMask, lastReader, lastWriter, index, batches);
    
        if(system->dispatchOverride){
            batches[index] = system->dispatchOverride(scene, system->ctx, system->process, counts[index], scene->systemChunkMem + offsets[index], systemFence, dependencyCount, dependencies);
        }else{
            batches[index] = dispatchChunksFromSystemPtr(scene, system, counts[index], scene->systemChunkMem + offsets[index], systemFence, dependencyCount, dependencies);
        }
    }
}
