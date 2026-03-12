#include "event.h"
#include "containers/darray.h"
#include <AvUtils/avMemory.h>
#include <AvUtils/threading/avMutex.h>
#include <AvUtils/threading/avRwLock.h>

#define EVENT_BATCH_SIZE 128
#define EVENT_POOL_SIZE 512
//#define EVENT_PIPELINE_SIZE 32
#define MAX_EVENT_ID 16384

#define INVALID_BATCH 0xFFFF

typedef struct EventBatch {
        Event events[EVENT_BATCH_SIZE];
}EventBatch;

typedef struct EventPool {
    union{
        EventBatch batches[EVENT_POOL_SIZE];
        struct __attribute__((aligned (sizeof(EventBatch)))) {
            EventID empty;
            uint32 nextEmptySlot;
        } slots[EVENT_POOL_SIZE];
    };
    uint32 nextEmpty;
} EventPool;

typedef struct EventPipeline {
    EventID id;
    uint16 currentBatch;
    union{
        uint8 currentSize;
        uint8 eventsPending;
    };
    AvMutex mutex;
    EventStageFn* stages;
    uint8 stageCount;
    uint16* stageIndex;
} EventPipeline;

typedef struct EventSystem {
    EventPool eventPool;
    uint32 maxID;
    uint16* registeredEvents; 
    EventPipeline* pipelines;
    bool8 eventsPending;
    AvMutex eventPoolMutex;
    AvRwLock eventLock;
} EventSystem;

static EventSystem* state = NULL;

struct DispatchInfo {
    EventBatch batch;
    uint32 eventCount;
    uint32 stagesOffset;
    uint32 stageCount;
    EventID id;
};
static struct DispatchInfo* dispatchBuffer = NULL;
static EventStageFn* eventStageBuffer = NULL;

bool8 eventSystemInitialize(uint64* memory_requirement, void* statePtr, void* configPtr){
    EventSystemConfig* config = (EventSystemConfig*)configPtr;
    *memory_requirement = sizeof(EventSystem);
    if(statePtr==NULL){
        return true;
    }
    if(config->maxIDs == 0){
        return false;
    }
    avMemset(statePtr, 0, sizeof(EventSystem));
    state = statePtr;

    state->maxID = config->maxIDs;
    state->registeredEvents = avAllocate(sizeof(uint16)*state->maxID, "");
    avMemset(state->registeredEvents, -1, sizeof(uint16)*state->maxID);
    state->pipelines = darrayCreate(EventPipeline);

    for(uint32 i = 1; i < EVENT_POOL_SIZE; i++){
        state->eventPool.slots[i].empty = EVENT_ID_INVALID;
        state->eventPool.slots[i].nextEmptySlot = i - 1;
    }
    state->eventPool.slots[0].empty = EVENT_ID_INVALID;
    state->eventPool.slots[0].nextEmptySlot = (uint32)-1;
    state->eventPool.nextEmpty = EVENT_POOL_SIZE-1;

    avMutexCreate(&state->eventPoolMutex);
    avRWLockCreate(&state->eventLock);
    dispatchBuffer = darrayCreate(struct DispatchInfo);
    eventStageBuffer = darrayCreate(EventStageFn);
    return true;
}

void eventSystemShutdown(void* statePtr){
    if(state==NULL){
        return;
    }
    avFree(state->registeredEvents);
    for(uint32 i = 0; i < darrayLength(state->pipelines); i++){
        avMutexDestroy(state->pipelines[i].mutex);
    }
    darrayDestroy(state->pipelines);
    avRWLockDestroy(state->eventLock);
    avMutexDestroy(state->eventPoolMutex);
    darrayDestroy(dispatchBuffer);
    darrayDestroy(eventStageBuffer);
}

bool32 registerEventID(EventID id){
    if(id==EVENT_ID_INVALID || id >= state->maxID){
        return false;
    }
    avRWLockWriteLock(state->eventLock);
    if(state->registeredEvents[id]!=(uint16)-1){
        avRWLockWriteUnlock(state->eventLock);
        return false; // already registered
    }

    uint16 pipelineID = darrayLength(state->pipelines);
    EventPipeline pipeline = {
        .id = id,
        .currentBatch = INVALID_BATCH,
        .stages = darrayCreate(EventStageFn),
        .stageIndex = darrayCreate(uint16),
    };
    state->registeredEvents[id] = pipelineID;
    avMutexCreate(&pipeline.mutex);
    darrayPush(state->pipelines, pipeline);
    avRWLockWriteUnlock(state->eventLock);
    return true;
}

EventStageID registerEventSink(EventID id, EventStageFn stage){
    return registerEventSinkAfter(id, stage, EVENT_STAGE_END);
}

static EventStageID insertStage(EventPipeline* pipeline, uint32 index, uint32 targetIndex, EventStageFn stage){
    darrayPush(pipeline->stages, stage);
    darrayPush(pipeline->stageIndex, index);
    if(targetIndex < pipeline->stageCount){ // target is not at the end
        //make space for new eventstage
        uint16 buffer[pipeline->stageCount];
        avMemcpy(buffer, pipeline->stageIndex + targetIndex, sizeof(uint16)*(pipeline->stageCount - targetIndex));
        avMemcpy(pipeline->stageIndex + targetIndex + 1, buffer, sizeof(uint16)*(pipeline->stageCount - targetIndex));
    }
    pipeline->stages[index] = stage;
    pipeline->stageCount++;
    pipeline->stageIndex[targetIndex] = index;
    return index;
}

static bool32 removeStage(EventPipeline* pipeline, uint32 index){
    if(pipeline->stageCount==0){
        return false;
    }
    if(index == pipeline->stageCount - 1){
        pipeline->stageCount--;
        darrayLengthSet(pipeline->stages, pipeline->stageCount);
        darrayLengthSet(pipeline->stageIndex, pipeline->stageCount);
        return true;
    }

    uint16 buffer[pipeline->stageCount];
    avMemcpy(buffer, pipeline->stageIndex + index + 1, sizeof(uint16)*(pipeline->stageCount - index - 1));
    avMemcpy(pipeline->stageIndex + index, buffer, sizeof(uint16)*(pipeline->stageCount - index - 1));
    pipeline->stageCount--;
    darrayLengthSet(pipeline->stages, pipeline->stageCount);
    darrayLengthSet(pipeline->stageIndex, pipeline->stageCount);
    return true;
}

static uint32 findStageIndex(EventPipeline* pipeline, EventStageID target){
    if(target == EVENT_STAGE_START){
        return 0;
    }
    if(target == EVENT_STAGE_END){
        return pipeline->stageCount - 1;
    }
    for(uint32 i = 0; i < pipeline->stageCount; i++){
        if(pipeline->stageIndex[i]==target){
            return i;
        }
    }
    return EVENT_REGISTER_FAILED_NO_TARGET;
}

static EventPipeline* getPipeline(EventID id){
    if(id >= state->maxID){
        return NULL;
    } 
    if(state->registeredEvents[id] == (uint16)-1){
        return NULL;
    }
    return &state->pipelines[state->registeredEvents[id]];
}

bool32 unregisterEventSink(EventID id, EventStageID target){
    registerEventID(id);
    avRWLockWriteLock(state->eventLock);
    EventPipeline* pipeline = getPipeline(id);
    if(pipeline==NULL){
        avRWLockWriteUnlock(state->eventLock);
        return false;
    }
    EventStageID index = findStageIndex(pipeline, target);
    if(index < 0){
        avRWLockWriteUnlock(state->eventLock);
        return false;
    }
    bool32 res = removeStage(pipeline, index);
    avRWLockWriteUnlock(state->eventLock);
    return res;
    
}

EventStageID registerEventSinkAfter(EventID id, EventStageFn stage, EventStageID target){
    registerEventID(id);
    avRWLockWriteLock(state->eventLock);
    EventPipeline* pipeline = getPipeline(id);
    if(pipeline==NULL){
        avRWLockWriteUnlock(state->eventLock);
        return EVENT_REGISTER_FAILED;
    }
    EventStageID index = pipeline->stageCount;
    EventStageID targetIndex = findStageIndex(pipeline, target)+1;
    if(targetIndex < 0){
        avRWLockWriteUnlock(state->eventLock);
        return targetIndex;
    }
    index = insertStage(pipeline, index, targetIndex, stage);
    avRWLockWriteUnlock(state->eventLock);
    return index;
}

EventStageID registerEventSinkBefore(EventID id, EventStageFn stage, EventStageID target){
    registerEventID(id);
    avRWLockWriteLock(state->eventLock);
    EventPipeline* pipeline = getPipeline(id);
    if(pipeline==NULL){
        avRWLockWriteUnlock(state->eventLock);
        return EVENT_REGISTER_FAILED;
    }
    EventStageID index = pipeline->stageCount;
    EventStageID targetIndex = findStageIndex(pipeline, target);
    if(targetIndex < 0){
        avRWLockWriteUnlock(state->eventLock);
        return targetIndex;
    }
    
    index = insertStage(pipeline, index, targetIndex, stage);
    avRWLockWriteUnlock(state->eventLock);
    return index;
}
EventStageID registerEventSinkBetween(EventID id, EventStageFn stage, EventStageID before, EventStageID after){
    if(after==before){
        return EVENT_REGISTER_FAILED;
    }
    registerEventID(id);
    avRWLockWriteLock(state->eventLock);
    EventPipeline* pipeline = getPipeline(id);
    if(pipeline==NULL){
        avRWLockWriteUnlock(state->eventLock);
        return EVENT_REGISTER_FAILED;
    }
    EventStageID index = pipeline->stageCount;

    EventStageID beforeIndex = findStageIndex(pipeline, before);
    if(beforeIndex < 0) return beforeIndex;
    EventStageID afterIndex = findStageIndex(pipeline, after)+1;
    if(afterIndex < 0) return afterIndex;

    if(afterIndex < beforeIndex){
        avRWLockWriteUnlock(state->eventLock);
        return EVENT_REGISTER_FAILED_WRONG_ORDER;
    }

    uint32 targetIndex = afterIndex;
    index = insertStage(pipeline, index, targetIndex, stage);
    avRWLockWriteUnlock(state->eventLock);
    return index;
}

uint32 allocateEventBatch(){
    avMutexLock(state->eventPoolMutex);
    uint32 batchId = state->eventPool.nextEmpty;
    if(batchId == (uint32)-1){
        avMutexUnlock(state->eventPoolMutex);
        return INVALID_BATCH;
    }
    state->eventPool.nextEmpty = state->eventPool.slots[batchId].nextEmptySlot;
    avMutexUnlock(state->eventPoolMutex);
    return batchId;
}

void freeEventBatch(uint16* batchId){
    if(*batchId >= EVENT_POOL_SIZE) return;
    avMutexLock(state->eventPoolMutex);
    state->eventPool.slots[*batchId].empty = EVENT_ID_INVALID;
    state->eventPool.slots[*batchId].nextEmptySlot = state->eventPool.nextEmpty;
    state->eventPool.nextEmpty = *batchId;
    avMutexUnlock(state->eventPoolMutex);
    *batchId = INVALID_BATCH;
}

static bool32 ensureBatchAvailable(EventPipeline* pipeline){
    if(pipeline->currentBatch==INVALID_BATCH){
        pipeline->currentBatch = allocateEventBatch();
        if(pipeline->currentBatch==INVALID_BATCH){
            return false;
        }
        pipeline->currentSize = 0;
    }
    return true;
}

static bool32 writeEvent(Event event, EventPipeline* pipeline){
    avMutexLock(pipeline->mutex);
    if(!ensureBatchAvailable(pipeline)){
        avMutexUnlock(pipeline->mutex);
        return false;
    }
    if(pipeline->currentSize >= EVENT_BATCH_SIZE){
        avMutexUnlock(pipeline->mutex);
        return false;
    }
    EventBatch* currentBatch = &state->eventPool.batches[pipeline->currentBatch];
    currentBatch->events[pipeline->currentSize++] = event;
    avMutexUnlock(pipeline->mutex);
}

static bool32 overwriteEvent(Event event, EventPipeline* pipeline){
    avMutexLock(pipeline->mutex);
    if(!ensureBatchAvailable(pipeline)){
        avMutexUnlock(pipeline->mutex);
        return false;
    }
    if(pipeline->currentSize >= EVENT_BATCH_SIZE){
        pipeline->currentSize--;
    }
    EventBatch* currentBatch = &state->eventPool.batches[pipeline->currentBatch];
    currentBatch->events[pipeline->currentSize++] = event;
    avMutexUnlock(pipeline->mutex);
}

static bool32 addEvent(Event event, bool32 (writeEventFn)(Event,EventPipeline*)){
    if(event.id >= state->maxID){
        return false;
    }
    // reading here
    avRWLockReadLock(state->eventLock);
    EventPipeline* pipeline = getPipeline(event.id);
    if(pipeline==NULL){
        avRWLockReadUnlock(state->eventLock);
        return false;
    }
    if(!writeEventFn(event, pipeline)){
        avRWLockReadUnlock(state->eventLock);
        return false;
    }
    state->eventsPending = true;
    avRWLockReadUnlock(state->eventLock);
    return true;
}

bool32 eventFire(Event event){
    return addEvent(event, writeEvent);
}

bool32 eventFireOverwrite(Event event){
    return addEvent(event, overwriteEvent);
}

static void dispatchEvents(struct DispatchInfo* dispatch){
    Event rerouteBuffer[EVENT_BATCH_SIZE];
    uint32 rerouteCount = 0;
    for(uint32 stage = 0; stage < dispatch->stageCount; stage++){
        (eventStageBuffer + dispatch->stagesOffset)[stage](dispatch->batch.events, dispatch->eventCount);

        // removed consumed events from the list
        uint32 eventIndex = 0;
        for(uint32 j = 0; j < dispatch->eventCount; j++){
            if(dispatch->batch.events->flags.hops > 8){
                continue;
            }
            if(dispatch->batch.events[j].flags.consumed==0) {
                continue;
            }
            if(dispatch->batch.events[j].id!=dispatch->id){// move event to new pipeline
                rerouteBuffer[rerouteCount++] = dispatch->batch.events[j];
                continue;
            }
            
            if(eventIndex!=j){
                dispatch->batch.events[eventIndex] = *(dispatch->batch.events + j);
            }
            eventIndex++;
        }
        dispatch->eventCount = eventIndex;
    }
    for(uint32 i = 0; i < rerouteCount; i++){
        rerouteBuffer[i].flags.hops++;
        eventFire(rerouteBuffer[i]);
    }
}



bool32 eventsDispatch(){
    while(state->eventsPending){
        darrayLengthSet(dispatchBuffer, 0); // clear batches from previous dispatch
        darrayLengthSet(eventStageBuffer, 0);
        avRWLockWriteLock(state->eventLock);
        for(uint32 i = 0; i < darrayLength(state->pipelines); i++){
            EventPipeline* pipeline = &state->pipelines[i];
            avMutexLock(pipeline->mutex);
            if(pipeline->eventsPending){
                struct DispatchInfo dispatch = {
                    .eventCount = pipeline->currentSize,
                    .stageCount = pipeline->stageCount,
                    .id = pipeline->id,
                };
                avMemcpy(&dispatch.batch, &state->eventPool.batches[pipeline->currentBatch], sizeof(EventBatch));
                
                dispatch.stagesOffset = darrayLength(eventStageBuffer);
                for(uint32 j = 0; j < pipeline->stageCount; j++){
                    darrayPush(eventStageBuffer, pipeline->stages[pipeline->stageIndex[j]]);
                }
                pipeline->eventsPending = 0;
                freeEventBatch(&pipeline->currentBatch);
                darrayPush(dispatchBuffer, dispatch);
            }
            avMutexUnlock(pipeline->mutex);
        }
        state->eventsPending = false;
        avRWLockWriteUnlock(state->eventLock);

        if(darrayLength(dispatchBuffer)){
            for(uint32 i = 0; i < darrayLength(dispatchBuffer); i++){
                dispatchEvents(&dispatchBuffer[i]);
            }
        }
    }
    
    return true;
}