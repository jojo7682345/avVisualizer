#pragma once
#include "../jobs.h"

typedef struct JobSlot {
    _Atomic uint32 sequence;
    JobBatchID data;
} JobSlot;

typedef struct JobQueueRing {
    uint32 size; // must be power of 2
    uint32 mask;
    _Atomic uint32 head;
    _Atomic uint32 tail;
    JobSlot* data;
} JobQueueRing;

typedef struct LocalJobQueueRing{
    uint32 size;
    uint32 mask;
    _Atomic uint32 top;
    uint32 bottom;
    JobID* data;
} LocalJobQueueRing;

typedef LocalJobQueueRing LocalJobQueue[JOB_PRIORITY_COUNT];
typedef JobQueueRing JobQueue[JOB_PRIORITY_COUNT];

void localJobQueueInit(LocalJobQueue* queue, uint32 size, JobID* backingArray);
bool32 localJobQueuePush(LocalJobQueue* queue, JobPriority priority, JobID job);
bool32 localJobQueuePushBatch(LocalJobQueue* queue, JobPriority priority, JobID* jobs, uint32 count);
JobID localJobQueuePull(LocalJobQueue* queue, JobPriority priority);
JobID localJobQueueSteal(LocalJobQueue* queue, JobPriority priority);

void jobQueueInit(JobQueue* queue, uint32 size, JobSlot* backingArray);
bool32 jobQueuePush(JobQueue* queue, JobPriority priority, JobBatchID job);
JobBatchID jobQueuePull(JobQueue* queue, JobPriority priority);