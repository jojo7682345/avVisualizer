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
    _Atomic uint32 refCount;

    _Atomic uint32 nextFree;
    JobBatchDescription batch;
    _Atomic uint32 remainingJobs;
    _Atomic uint32 dependencyCount;
    _Atomic uint32 dependentCount;
    JobBatchID dependents[JOB_MAX_DEPENDENTS];
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


struct JobBatchPoolSlot* getSlot(JobBatchID batch);
static void unpin(struct JobBatchPoolSlot* slot);


static bool32 tryPin(JobBatchID id, struct JobBatchPoolSlot** out){
    if(id == JOB_BATCH_NONE){
        avError("Invalid job batch handle");
        return false;
    }

    union JobBatchReferenceSlot ref = {.id = id};
    struct JobBatchPoolSlot* slot = &jobBatchPool.slots[ref.identifier];

    uint32 gen = atomic_load_explicit(&slot->generation, memory_order_acquire);

    if(gen != ref.generation){
        return false;
    }

    while(true){
        uint32 refs = atomic_load_explicit(&slot->refCount, memory_order_acquire);
        if(refs == 0){
            return false;
        }

        uint32 desired = refs + 1;
        if(atomic_compare_exchange_weak_explicit(&slot->refCount, &refs, desired, memory_order_acq_rel, memory_order_acquire)) {
            break;
        }
    }

    uint32 gen2 = atomic_load_explicit(&slot->generation, memory_order_acquire);

    if(gen2 != ref.generation){
        unpin(slot);
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
}

/**
 * @brief drops handle.
 * @note this must be the last occurance of this handle
 * 
 * @param batch 
 */
static void dropHandle(struct JobBatchPoolSlot* slot){
    onBatchComplete(slot);

    JobFence fence = slot->batch.fence;

    // Invalidate all outstanding IDs
    atomic_fetch_add_explicit(&slot->generation, 1, memory_order_acq_rel);
    atomic_fetch_sub_explicit(&jobBatchPool.activeJobBatchCount, 1, memory_order_relaxed);
    uint32 index = (uint32)(slot - jobBatchPool.slots);
    uint32 dependentCount = atomic_exchange_explicit(&slot->dependentCount, 0, memory_order_acq_rel);
    
    for(uint32 i = 0; i < dependentCount; i++){
        struct JobBatchPoolSlot* depSlot = getSlot(slot->dependents[i]);
        if(depSlot==NULL) continue;
        if(atomic_fetch_sub_explicit(&depSlot->dependencyCount, 1, memory_order_acq_rel)==1){
            if(submitToMainQueue(depSlot->batch.flags.priority, slot->dependents[i])==JOB_BATCH_NONE){
                avError("Failed to submit job to main queue");
            }
            //avDebug("Added dependent to job queue");
        }
        unpin(depSlot);
    }

    if(fence){
        if(atomic_fetch_sub(&fence->workLeft, 1)==0){
            avError("Fence corruption");
        }
    }

    //allow new batches to inhabit this slot
    freeListPush(&jobBatchPool, index);
}


static void unpin(struct JobBatchPoolSlot* slot){
    if(atomic_fetch_sub(&slot->refCount, 1)==1){
        dropHandle(slot);
    }
}


struct JobBatchPoolSlot* getSlot(JobBatchID batch){
    #ifndef NDEBUG
        if(batch==0) {
            avError("Tried to access invalid handle");
            return NULL;
        }
    #endif

        struct JobBatchPoolSlot* slot;
        if(!tryPin(batch, &slot)){
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
    atomic_store_explicit(&slot->dependentCount, 0, memory_order_relaxed);
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

static void releaseHandle(struct JobBatchPoolSlot* slot){
    if(atomic_fetch_sub_explicit(&slot->refCount, 1, memory_order_acq_rel) == 1) {
        dropHandle(slot);
    }
}

void freeJobBatch(JobBatchID batch){
    struct JobBatchPoolSlot* slot = getSlot(batch);
    if(slot==NULL) return;

    unpin(slot);
    releaseHandle(slot);
}

bool32 signalJobComplete(JobBatchID batch){
    struct JobBatchPoolSlot* slot = getSlot(batch);
    if(slot==NULL){
        return false;
    }
    uint32 remainingJobs = atomic_fetch_sub_explicit(&slot->remainingJobs, 1, memory_order_seq_cst) - 1;
    if(remainingJobs == 0){
        unpin(slot);
        return true;
    }
    unpin(slot);
    return false;
}

void getJobBatch(JobBatchID id, JobBatchDescription* batch){
    struct JobBatchPoolSlot* slot = getSlot(id);
    if(slot==NULL){
        return;
    }
    avMemcpy(batch, &slot->batch, sizeof(JobBatchDescription));
    unpin(slot);
}

void writeJobBatch(JobBatchID id, JobBatchDescription* batch){
    struct JobBatchPoolSlot* slot = getSlot(id);
    if(slot==NULL){
        return;
    }
    avMemcpy(&slot->batch, batch, sizeof(JobBatchDescription));
    unpin(slot);
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

