#include "../jobs.h"

#include <stdatomic.h>
#include <AvUtils/threading/avRwLock.h>
#include <AvUtils/avMemory.h>

#include "jobBatchPool.h"
#define AV_LOG_CATEGORY "job system" 
#include "logging.h"

union JobBatchReferenceSlot {
    JobBatchID id;
    struct {
        uint32 identifier;
        uint32 generation;
    };
};


struct JobBatchPoolSlot {
    _Atomic uint32 generation;
    _Atomic uint32 nextFree;
    JobBatchDescription batch;
    _Atomic uint32 remainingJobs;
    _Atomic uint32 dependencyCount;
    _Atomic uint32 dependentCount;
    JobBatchID dependents[JOB_MAX_DEPENDENTS];
    _Atomic uint32 refCount;
    _Atomic bool8 retired;
};

struct FreeListHead {
    uint32 index;
    uint32 tag;
};

struct JobBatchPool {
    _Atomic struct FreeListHead head;
    _Atomic uint32 activeJobBatchCount;
    struct JobBatchPoolSlot slots[JOB_BATCH_POOL_SIZE];
};
static struct JobBatchPool jobBatchPool = {0};

void jobBatchPoolInit(){
    for (uint32 i = 0; i < JOB_BATCH_POOL_SIZE - 1; i++) {
        atomic_store(&jobBatchPool.slots[i].nextFree, i + 1);
    }
    atomic_store(&jobBatchPool.slots[JOB_BATCH_POOL_SIZE - 1].nextFree, __UINT32_MAX__);
    struct FreeListHead head = (struct FreeListHead){ .index = 0, .tag = 0 };
    atomic_store(&jobBatchPool.head, head);
#ifndef NDEBUG // reserve id 0 for debugging, and throw an error when this id is used.
    JobBatchDescription description = {0};
    allocateJobBatch(&description, 0, NULL);
#endif
}

void jobBatchPoolDeinit(){
#ifndef NDEBUG
    uint32 count = atomic_load_explicit(&jobBatchPool.activeJobBatchCount, memory_order_relaxed)-1;
    if(count != 0){
        avError("%u batches are still active", count);
    }
#endif
}

static uint32 freeListPop(struct JobBatchPool* pool){
    struct FreeListHead oldHead;
    struct FreeListHead newHead;
    while(true){
        oldHead = atomic_load(&pool->head);

        if(oldHead.index == __UINT32_MAX__){
            return __UINT32_MAX__;
        }

        uint32 next = atomic_load(&pool->slots[oldHead.index].nextFree);
        newHead.index = next;
        newHead.tag = oldHead.tag + 1;

        if(atomic_compare_exchange_weak(&pool->head, &oldHead, newHead)){
            return oldHead.index;
        }
    }
}

void freeListPush(struct JobBatchPool* pool, uint32 index){
    struct FreeListHead oldHead;
    struct FreeListHead newHead;

    while(true){
        oldHead = atomic_load(&pool->head);
        atomic_store(&pool->slots[index].nextFree, oldHead.index);

        newHead.index = index;
        newHead.tag = oldHead.tag + 1;

        if(atomic_compare_exchange_weak(&pool->head, &oldHead, newHead)){
            return;
        }
    }
}

struct JobBatchPoolSlot* getSlot(JobBatchID batch){
    #ifndef NDEBUG
        if(batch==0) {
            avError("Tried to access invalid handle");
            return NULL;
        }
    #endif
        union JobBatchReferenceSlot ref = { .id = batch };
        uint32 index = ref.identifier;
        uint32 generation = ref.generation;
        struct JobBatchPoolSlot* slot = &jobBatchPool.slots[index];
        uint32 currentGen = atomic_load_explicit(&slot->generation, memory_order_acquire);
        if(currentGen != generation){
            avError("Accessed expired JobBatchID");
            return NULL;
        }
        return slot;
    }

JobBatchID allocateJobBatch(JobBatchDescription* description, uint32 depdendencyCount, JobBatchID* dependencies){
    uint32 index = freeListPop(&jobBatchPool);
    if(index == __UINT32_MAX__){
        return JOB_BATCH_NONE;
    }

    atomic_fetch_add_explicit(&jobBatchPool.activeJobBatchCount, 1, memory_order_relaxed);

    struct JobBatchPoolSlot* slot = &jobBatchPool.slots[index];

    uint32 generation = atomic_load_explicit(&slot->generation, memory_order_relaxed);

    avMemcpy(&slot->batch, description, sizeof(JobBatchDescription));
    description->index = 0;

    atomic_store_explicit(&slot->remainingJobs, description->size, memory_order_relaxed);
    atomic_store_explicit(&slot->dependencyCount, 0, memory_order_relaxed);
    atomic_store_explicit(&slot->refCount, 1, memory_order_relaxed);

    atomic_thread_fence(memory_order_release);

    union JobBatchReferenceSlot id = (union JobBatchReferenceSlot){
        .identifier = index,
        .generation = generation,
    };

    if(depdendencyCount){
        uint32 count = depdendencyCount;

        uint32 unresolvedCount = 0;
        atomic_store_explicit(&slot->dependencyCount, 1, memory_order_release);
        for(uint32 i = 0; i < count; i++){
            if(addDependent(dependencies[i], id.id)){
                unresolvedCount++;
            }
        }
        if(atomic_fetch_sub_explicit(&slot->dependencyCount, 1, memory_order_acq_rel)==1){
            if(submitToMainQueue(slot->batch.flags.priority, id.id)==JOB_BATCH_NONE){
                avError("Failed to submit job to main queue");
                return JOB_BATCH_NONE;
            }
        }
    }

    return id.id;
}

static bool32 tryPin(JobBatchID id, struct JobBatchPoolSlot** out){
    if(id==JOB_BATCH_NONE){
        avError("Invalid job batch handle");
        return false;
    }
    union JobBatchReferenceSlot ref = {.id = id};
    struct JobBatchPoolSlot* slot = &jobBatchPool.slots[ref.identifier];

    uint32 gen = atomic_load_explicit(&slot->generation, memory_order_acquire);
    if(gen != ref.generation){
        return false;
    }

    atomic_fetch_add(&slot->refCount, 1);

    uint32 gen2 = atomic_load_explicit(&slot->generation, memory_order_acquire);
    if(gen2 != ref.generation){
        atomic_fetch_sub(&slot->refCount, 1);
        return false;
    }
    *out = slot;
    return true;
}

void onBatchComplete(struct JobBatchPoolSlot* slot){
    uint32 index = (uint32)(slot - jobBatchPool.slots);
    uint32 generation = atomic_load_explicit(&slot->generation, memory_order_relaxed);
    union JobBatchReferenceSlot ref = (union JobBatchReferenceSlot){.generation=generation, .identifier=index};
    //avDebug("JobBatch %u:%u is complete", ref.generation, ref.identifier);
    if(slot->batch.fence){
        atomic_fetch_sub(&slot->batch.fence->workLeft, 1);
    }
}

/**
 * @brief drops handle.
 * @note this must be the last occurance of this handle
 * 
 * @param batch 
 */
static void dropHandle(struct JobBatchPoolSlot* slot){
    onBatchComplete(slot);

    // Invalidate all outstanding IDs
    atomic_fetch_add_explicit(&slot->generation, 1, memory_order_acq_rel);
    atomic_fetch_sub_explicit(&jobBatchPool.activeJobBatchCount, 1, memory_order_relaxed);
    uint32 index = (uint32)(slot - jobBatchPool.slots);
    freeListPush(&jobBatchPool, index);

    uint32 dependentCount = atomic_load_explicit(&slot->dependentCount, memory_order_acquire);
    for(uint32 i = 0; i < dependentCount; i++){
        struct JobBatchPoolSlot* depSlot = getSlot(slot->dependents[i]);
        if(atomic_fetch_sub_explicit(&depSlot->dependencyCount, 1, memory_order_acq_rel)==1){
            if(submitToMainQueue(depSlot->batch.flags.priority, slot->dependents[i])==JOB_BATCH_NONE){
                avError("Failed to submit job to main queue");
            }
            //avDebug("Added dependent to job queue");
        }
    }
}


static void tryFinalize(struct JobBatchPoolSlot* slot){
    uint32 expected = 0;
    if(!atomic_compare_exchange_strong(&slot->refCount, &expected, 0)){
        return;
    }
    if(!atomic_load(&slot->retired)){
        return;
    }

    dropHandle(slot);
}

static void unpin(struct JobBatchPoolSlot* slot){
    if(atomic_fetch_sub(&slot->refCount, 1)==1){
        tryFinalize(slot);
    }
}




void freeJobBatch(JobBatchID batch){
    union JobBatchReferenceSlot ref = { .id = batch };
    struct JobBatchPoolSlot* slot = &jobBatchPool.slots[ref.identifier];

    if(atomic_load(&slot->generation) != ref.generation){
        return;
    }

    if(atomic_fetch_sub(&slot->refCount, 1) != 1){
        return;
    }
    atomic_store_explicit(&slot->retired, true, memory_order_release);
    tryFinalize(slot);
}

bool32 signalJobComplete(JobBatchID batch){
    struct JobBatchPoolSlot* slot = getSlot(batch);
    if(slot==NULL){
        return false;
    }
    uint32 remainingJobs = atomic_fetch_sub_explicit(&slot->remainingJobs, 1, memory_order_seq_cst) - 1;
    if(remainingJobs == 0){
        return true;
    }
    return false;
}

void getJobBatch(JobBatchID id, JobBatchDescription* batch){
    struct JobBatchPoolSlot* slot = getSlot(id);
    if(slot==NULL){
        return;
    }
    avMemcpy(batch, &slot->batch, sizeof(JobBatchDescription));
}

void writeJobBatch(JobBatchID id, JobBatchDescription* batch){
    struct JobBatchPoolSlot* slot = getSlot(id);
    if(slot==NULL){
        return;
    }
    avMemcpy(&slot->batch, batch, sizeof(JobBatchDescription));
}

bool32 addDependent(JobBatchID target, JobBatchID dependent){
    struct JobBatchPoolSlot* targetSlot;
    if(!tryPin(target, &targetSlot)){
        return false;
    }

    struct JobBatchPoolSlot* depSlot;
    if(!tryPin(dependent, &depSlot)){
        unpin(targetSlot);
        return false;
    }

    uint32 idx = atomic_fetch_add(&targetSlot->dependentCount, 1);
    if(idx >= JOB_MAX_DEPENDENTS){
        unpin(targetSlot);
        unpin(depSlot);
        return false;
    }

    targetSlot->dependents[idx] = dependent;
    atomic_fetch_add(&depSlot->dependencyCount, 1);

    unpin(depSlot);
    unpin(targetSlot);
    return true;
}

// struct JobBatchPoolSlot {
//     JobBatchDescription batch;
//     _Atomic uint32 remainingJobs;

//     _Atomic uint32 dependencyCount;
//     JobBatchID dependents[JOB_MAX_DEPENDENTS];
//     uint32 dependentCount;
// };
// typedef struct JobBatchPool {
//     uint32 jobBatchCount;
//     uint32 jobBatchIndex[JOB_INSTANCE_POOL_SIZE];
//     union JobBatchReferenceSlot jobBatchReference[JOB_INSTANCE_POOL_SIZE];
//     struct JobBatchPoolSlot jobBatches[JOB_INSTANCE_POOL_SIZE];
//     AvRwLock lock;
// } JobBatchPool;
// static JobBatchPool jobBatchPool = {0};

// JobBatchID allocateJobBatch(JobBatchDescription* description, bool32 dependencies){
//     avRWLockReadLock(jobBatchPool.lock);
//     uint32 index = atomic_fetch_add(&jobBatchPool.jobBatchCount, 1);
//     if(index >= JOB_INSTANCE_POOL_SIZE){
//         atomic_store_explicit(&jobBatchPool.jobBatchCount, JOB_INSTANCE_POOL_SIZE, memory_order_relaxed);
//         avRWLockReadUnlock(jobBatchPool.lock);
//         return JOB_BATCH_NONE;
//     }
//     avMemset(jobBatchPool.jobBatches + index, 0, sizeof(struct JobBatchPoolSlot));
//     avMemcpy(&(jobBatchPool.jobBatches + index)->batch, description, sizeof(JobBatchDescription));
//     jobBatchPool.jobBatches[index].remainingJobs = description->size;
//     JobBatchID id = jobBatchPool.jobBatchReference[index].id;
//     if(dependencies) atomic_fetch_or(&jobBatchPool.jobBatches[index].dependencyCount, (1UL<<31)); // signals it is uninitialized
//     atomic_thread_fence(memory_order_release);
//     avRWLockReadUnlock(jobBatchPool.lock);
    
//     return id;
// }

// bool32 signalJobComplete(JobBatchID batch){
//     avRWLockReadLock(jobBatchPool.lock);
//     uint32 identifier = ((union JobBatchReferenceSlot)batch).identifier;
//     uint32 generation = ((union JobBatchReferenceSlot)batch).generation;
//     uint32 index = jobBatchPool.jobBatchIndex[identifier];
//     if(generation != jobBatchPool.jobBatchReference[index].generation){
//         avError("Accessed expired handle");
//         avRWLockReadUnlock(jobBatchPool.lock);
//         return false;
//     }
//     uint32 remainingJobs = atomic_fetch_sub_explicit(&jobBatchPool.jobBatches[index].remainingJobs, 1, memory_order_seq_cst) - 1;
//     avRWLockReadUnlock(jobBatchPool.lock);
//     if(remainingJobs == 0){
//         return true;
//     }
//     return false;
// }

// void getJobBatch(JobBatchID id, JobBatchDescription* batch){
//     avRWLockReadLock(jobBatchPool.lock);
//     uint32 identifier = ((union JobBatchReferenceSlot)id).identifier;
//     uint32 generation = ((union JobBatchReferenceSlot)id).generation;
//     uint32 index = jobBatchPool.jobBatchIndex[identifier];
//     if(generation != jobBatchPool.jobBatchReference[index].generation){
//         avError("Accessed expired handle");
//         avRWLockReadUnlock(jobBatchPool.lock);
//         return;
//     }
//     JobBatchDescription* target = &jobBatchPool.jobBatches[index].batch;
//     avMemcpy(batch, target, sizeof(JobBatchDescription));
//     avRWLockReadUnlock(jobBatchPool.lock);
// }

// void onBatchComplete(JobBatchID batch){
//     avRWLockReadLock(jobBatchPool.lock);
//     avDebug("JobBatch %u is complete", batch);
//     JobBatchDescription description;
//     getJobBatch(batch, &description);
//     avRWLockReadUnlock(jobBatchPool.lock);

//     if(description.fence){
//         atomic_fetch_sub(&description.fence->workLeft, 1);
//     }
//     atomic_fetch_sub(&state->allWorkDoneFence.workLeft, 1);
// }


// void writeJobBatch(JobBatchID id, JobBatchDescription* batch){
//     avRWLockReadLock(jobBatchPool.lock);
//     uint32 identifier = ((union JobBatchReferenceSlot)id).identifier;
//     uint32 generation = ((union JobBatchReferenceSlot)id).generation;
//     uint32 index = jobBatchPool.jobBatchIndex[identifier];
//     if(generation != jobBatchPool.jobBatchReference[index].generation){
//         avError("Accessed expired handle");
//         avRWLockReadUnlock(jobBatchPool.lock);
//         return;
//     }
//     JobBatchDescription* target = &jobBatchPool.jobBatches[index].batch;
//     avMemcpy(target, batch, sizeof(JobBatchDescription));
//     avRWLockReadUnlock(jobBatchPool.lock);
// }

// void freeJobBatch(JobBatchID batch){
//     onBatchComplete(batch);

//     avRWLockWriteLock(jobBatchPool.lock);
//     atomic_thread_fence(memory_order_acquire);
//     uint32 identifier = ((union JobBatchReferenceSlot)batch).identifier;
//     uint32 generation = ((union JobBatchReferenceSlot)batch).generation;
//     uint32 index = jobBatchPool.jobBatchIndex[identifier];
//     if(generation != jobBatchPool.jobBatchReference[index].generation){
//         avError("Accessed expired handle");
//         avRWLockWriteUnlock(jobBatchPool.lock);
//         return;
//     }
//     jobBatchPool.jobBatchReference[index].generation++;

//     uint32 lastIndex = jobBatchPool.jobBatchCount - 1;
//     if(index == lastIndex){
//         jobBatchPool.jobBatchCount--;
//         atomic_thread_fence(memory_order_release);
//         avRWLockWriteUnlock(jobBatchPool.lock);
//         return;
//     }
//     union JobBatchReferenceSlot lastId = jobBatchPool.jobBatchReference[lastIndex];
//     union JobBatchReferenceSlot currentId = jobBatchPool.jobBatchReference[index];
//     jobBatchPool.jobBatchReference[index] = lastId;
//     jobBatchPool.jobBatchReference[lastIndex] = currentId;
//     jobBatchPool.jobBatchIndex[lastId.id] = index;
//     jobBatchPool.jobBatchIndex[identifier] = lastIndex;
//     avMemswap(jobBatchPool.jobBatches + index, jobBatchPool.jobBatches + lastIndex, sizeof(struct JobBatchPoolSlot));
//     jobBatchPool.jobBatchCount--;
//     atomic_thread_fence(memory_order_release);
//     avRWLockWriteUnlock(jobBatchPool.lock);

// }

// bool32 addDependent(JobBatchID target, JobBatchID dependent){
//     avRWLockWriteLock(jobBatchPool.lock);
//     uint32 identifier = ((union JobBatchReferenceSlot)target).identifier;
//     uint32 generation = ((union JobBatchReferenceSlot)target).generation;
//     uint32 index = jobBatchPool.jobBatchIndex[identifier];
//     if(generation != jobBatchPool.jobBatchReference[index].generation || index >= jobBatchPool.jobBatchCount){
//         avRWLockWriteUnlock(jobBatchPool.lock); // batch already complete
//         return false;
//     }
//     struct JobBatchPoolSlot* targetSlot = &jobBatchPool.jobBatches[index];

//     identifier = ((union JobBatchReferenceSlot)dependent).identifier;
//     generation = ((union JobBatchReferenceSlot)dependent).generation;
//     index = jobBatchPool.jobBatchIndex[identifier];
//     if(generation != jobBatchPool.jobBatchReference[index].generation || index >= jobBatchPool.jobBatchCount){
//         avRWLockWriteUnlock(jobBatchPool.lock); // invalid batch
//         return false;
//     }
//     struct JobBatchPoolSlot* dependentSlot = &jobBatchPool.jobBatches[index];

//     if(targetSlot->dependentCount > JOB_MAX_DEPENDENTS){
//         // If this becomes an issue, recursively add dependent to one of the target dependencies
//         avRWLockWriteUnlock(jobBatchPool.lock);
//         return false; 
//     }
//     targetSlot->dependents[targetSlot->dependentCount++] = dependent;
//     atomic_fetch_add(&dependentSlot->dependencyCount, 1);
//     avRWLockWriteUnlock(jobBatchPool.lock);
//     return true;
// }

// void jobBatchPoolInit(){
//     avRWLockCreate(&jobBatchPool.lock);

//     for(uint32 i = 0; i < JOB_INSTANCE_POOL_SIZE; i++){
//         jobBatchPool.jobBatchIndex[i] = i;
//         jobBatchPool.jobBatchReference[i] = (union JobBatchReferenceSlot){.generation=0, .identifier=i};
//     }
// }

// void jobBatchPoolDeinit(){
//     avRWLockDestroy(jobBatchPool.lock);
//     if(jobBatchPool.jobBatchCount){
//         avError("%u batches are still active", jobBatchPool.jobBatchCount);
//     }
// }