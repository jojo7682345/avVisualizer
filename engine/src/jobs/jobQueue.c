#include "jobQueue.h"
#include <AvUtils/avMath.h>
void jobQueueRingInit(JobQueueRing* queue, uint32 size, JobSlot* backingArray){
    queue->size = size;
    queue->mask = size - 1;
    queue->head = 0;
    queue->tail = 0;
    queue->data = backingArray;
    for(uint32 i = 0; i < size; i++){
        atomic_store(&queue->data[i].sequence, i);
    }
}

bool32 jobQueueRingPush(JobQueueRing* queue, JobID job){
    JobSlot* slot;
    uint32 pos;
    while(1){
        pos = atomic_load_explicit(&queue->tail, memory_order_relaxed);
        slot = &queue->data[pos & queue->mask];

        uint32 seq = atomic_load_explicit(&slot->sequence, memory_order_acquire);
        int32 diff = (int32)seq - (int32)pos;

        if(diff==0){
            if(atomic_compare_exchange_weak_explicit(&queue->tail, &pos, pos + 1, memory_order_relaxed, memory_order_relaxed)){
                break;
            }
        }else if(diff < 0){
            return false;
        }
        // retry
    }

    slot->data = job;
    atomic_store_explicit(&slot->sequence, pos + 1, memory_order_release);
    return true;
}

JobID jobQueueRingPull(JobQueueRing* queue){
    JobSlot* slot;
    uint32 pos;
    while(1){
        pos = atomic_load_explicit(&queue->head, memory_order_relaxed);
        slot = &queue->data[pos & queue->mask];

        uint32 seq = atomic_load_explicit(&slot->sequence, memory_order_acquire);
        int32 diff = (int32)seq - (int32)(pos+1);
        if(diff == 0){
            if(atomic_compare_exchange_weak_explicit(&queue->head, &pos, pos + 1, memory_order_relaxed, memory_order_relaxed)){
                break;
            }
        }else if(diff < 0){
            return JOB_NONE;
        }
        //retry
    }

    JobID job = slot->data;
    atomic_store_explicit(&slot->sequence, pos + queue->mask + 1, memory_order_release);
    return job;
}



void localJobQueueRingInit(LocalJobQueueRing* queue, uint32 size, JobID* backingArray){
    queue->size = size;
    queue->mask = size - 1;
    queue->top = 0;
    queue->bottom = 0;
    queue->data = backingArray;
}

bool32 localJobQueueRingPush(LocalJobQueueRing* queue, JobID job){
    uint32 bottom = queue->bottom;
    uint32 top = atomic_load_explicit(&queue->top, memory_order_acquire);
    if((uint32)(bottom - top) >= queue->size){
        return false;
    }
    queue->data[bottom & queue->mask] = job;
    atomic_thread_fence(memory_order_release);
    queue->bottom = bottom+1;
    return true;
}

bool32 localJobQueueRingPushBatch(LocalJobQueueRing* queue, JobID* jobs, uint32 count){
    uint32 bottom = queue->bottom;
    uint32 top = atomic_load_explicit(&queue->top, memory_order_acquire);
    if((uint32)(bottom - top) >= queue->size - AV_MIN(count, queue->size)){
        return false;
    }
    for(uint32 i = 0; i < count; i++){
        queue->data[(bottom + i) & queue->mask] = jobs[i];
    }
    atomic_thread_fence(memory_order_release);
    queue->bottom = bottom+count;
    return true;
}

JobID localJobQueueRingPull(LocalJobQueueRing* queue){
    uint32 bottom = queue->bottom;
    bottom--;
    queue->bottom = bottom;

    atomic_thread_fence(memory_order_seq_cst);

    uint32 top = atomic_load_explicit(&queue->top, memory_order_acquire);
    if((int32)(top-bottom) <= 0){
        queue->bottom =  top;
        return JOB_NONE;
    }

    JobID job = queue->data[bottom & queue->mask];

    if(top == bottom){
        if(!atomic_compare_exchange_strong_explicit(&queue->top, &top, top + 1, memory_order_acq_rel, memory_order_relaxed)){
            job = JOB_NONE;
        }
        atomic_store_explicit(&queue->bottom, bottom+1, memory_order_relaxed);
    }

    return job;
}

JobID localJobQueueRingSteal(LocalJobQueueRing* queue){
    uint32 top = atomic_load_explicit(&queue->top, memory_order_acquire);
    atomic_thread_fence(memory_order_acquire);
    uint32 bottom = queue->bottom;

    if((int32)(bottom - top) <= 0){
        return JOB_NONE;
    }
    JobID job = queue->data[top & queue->mask];
    if(!atomic_compare_exchange_strong_explicit(&queue->top, &top, top + 1, memory_order_acq_rel, memory_order_acquire)){
        return JOB_NONE;
    }
    return job;
}

void jobQueueInit(JobQueue* queue, uint32 size, JobSlot* backingArray){
    for(uint32 i = 0; i < JOB_PRIORITY_COUNT; i++){
        jobQueueInit((JobQueueRing*)queue + i, size, backingArray + size*i);
    }
}
bool32 jobQueuePush(JobQueue* queue, JobPriority priority, JobID job){
    return jobQueueRingPush((JobQueueRing*)queue  + priority, job);
}
JobID jobQueuePull(JobQueue* queue, JobPriority priority){
    return jobQueueRingPull((JobQueueRing*)queue + priority);
}

void localJobQueueInit(LocalJobQueue* queue, uint32 size, JobID* backingArray){
    for(uint32 i = 0; i < JOB_PRIORITY_COUNT; i++){
        localJobQueueRingInit((LocalJobQueueRing*)queue + i, size, backingArray + size*i);
    }
}

bool32 localJobQueuePush(LocalJobQueue* queue, JobPriority priority, JobID job){
    return localJobQueueRingPush((LocalJobQueueRing*)queue + priority, job); 
}

bool32 localJobQueuePushBatch(LocalJobQueue* queue, JobPriority priority, JobID* jobs, uint32 count){
    return localJobQueueRingPushBatch((LocalJobQueueRing*)queue + priority, jobs, count);
}

JobID localJobQueuePull(LocalJobQueue* queue, JobPriority priority){
    return localJobQueueRingPull((LocalJobQueueRing*)queue + priority);
}

JobID localJobQueueSteal(LocalJobQueue* queue, JobPriority priority){
    return localJobQueueRingSteal((LocalJobQueueRing*)queue + priority);
}