#include "../jobs.h"

#include "jobQueue.h"
#include "jobsInternal.h"

#define AV_LOG_CATEGORY "job system" 
#include "logging.h"


JobInstancePool jobInstancePool = {0};
JobSystemState* state = NULL;

int32 workerThreadEntry(byte* data, uint64 size);
AV_API bool8 jobSystemInitialize(uint64* memoryRequirement, void* statePtr, void* configPtr){
    *memoryRequirement = sizeof(JobSystemState);
    if(statePtr==NULL) return true;
    state = (JobSystemState*) statePtr;
    avMemset(state, 0, sizeof(JobSystemState));

    JobSystemConfig* config = (JobSystemConfig*) configPtr;
    
    state->threadCount = config->maxWorkerThreads;
    uint32 threadsSize = sizeof(WorkerThread) * state->threadCount;
    uint32 localQueueSize = sizeof(JobID) * LOCAL_QUEUE_SIZE * state->threadCount;
    uint32 globalQueueSize = sizeof(JobSlot) * GLOBAL_QUEUE_SIZE;
    state->threads = avAllocate(threadsSize + localQueueSize + globalQueueSize, "");
    WorkerThread* threads = state->threads;
    JobID* jobsBuffer = (JobID*)(threads + state->threadCount);
    JobSlot* jobbatchSlots = (JobSlot*)(jobsBuffer + LOCAL_QUEUE_SIZE * state->threadCount);
    for(uint32 i = 0; i < state->threadCount; i++){
        state->threads[i] = (WorkerThread){0};
        avThreadCreate(workerThreadEntry, &threads[i].thread);
        char buffer[16] = {0};
        avStringPrintfToBuffer(buffer, 16, AV_CSTRA("Worker %u"), i);
        avThreadSetName(threads[i].thread, buffer);
        localJobQueueInit(&threads[i].localQueue, LOCAL_QUEUE_RING_SIZE, jobsBuffer + LOCAL_QUEUE_SIZE*i);
        for(uint32 j = 0; j < JOB_PRIORITY_COUNT; j++){
            atomic_store_explicit(&threads[i].workingOnBatch[j], JOB_BATCH_NONE, memory_order_release);
        }
    }
    jobQueueInit(&state->jobQueue, GLOBAL_QUEUE_RING_SIZE, jobbatchSlots);

    jobBatchPoolInit();

    avRWLockCreate(&jobInstancePool.lock);
    avSemaphoreCreate(&state->workPresent, 0);
    atomic_store(&state->activeThreads, 0);

    for(uint32 i = 0; i < JOB_INSTANCE_POOL_SIZE; i++){
        jobInstancePool.jobIndex[i] = i;
        jobInstancePool.jobReference[i] = i;
    }

    for(uint32 i = 0; i < JOB_PRIORITY_COUNT; i++){
        atomic_store(&state->exchange[i], JOB_NONE);
    }
    

    for(uint32 i = 0; i < state->threadCount; i++){
        avThreadStart(NULL, i, state->threads[i].thread);
    }

    return true;
}

void finishAndExit(){
    jobFenceWait(&state->allWorkDoneFence);
    state->awaitingExit = true;
    for(uint32 i = 0; i < state->threadCount; i++){
        avSemaphorePost(state->workPresent);
    }
}

AV_API void jobSystemDeinitialize(void* statePtr){
    if(state==NULL) return;

    finishAndExit();

    for(uint32 i = 0; i < state->threadCount; i++){
        avThreadDestroy(state->threads[i].thread);
    }
    avDebug("All work is completed");

    avSemaphoreDestroy(state->workPresent);
    avRWLockDestroy(jobInstancePool.lock);
    jobBatchPoolDeinit();

    avFree(state->threads);

    if(jobInstancePool.jobCount){
        avError("%u jobs are still active", jobInstancePool.jobCount);
    }
}

JobID allocateJob(JobInstance* instance){
    avRWLockReadLock(jobInstancePool.lock);
    uint32 index = atomic_fetch_add(&jobInstancePool.jobCount, 1);
    if(index >= JOB_INSTANCE_POOL_SIZE){
        atomic_store_explicit(&jobInstancePool.jobCount, JOB_INSTANCE_POOL_SIZE, memory_order_relaxed);
        avRWLockReadUnlock(jobInstancePool.lock);
        return JOB_NONE; // full
    }
    avMemset(jobInstancePool.jobs + index, 0, sizeof(struct JobInstancePoolSlot));
    avMemcpy(&(jobInstancePool.jobs + index)->instance, instance, sizeof(JobInstance));
    JobID id = jobInstancePool.jobReference[index];
    avRWLockReadUnlock(jobInstancePool.lock);
    return id;
}

void freeJob(JobID job){
    avRWLockWriteLock(jobInstancePool.lock);
    atomic_thread_fence(memory_order_acquire);
    uint32 index = jobInstancePool.jobIndex[job];
    uint32 lastIndex = jobInstancePool.jobCount-1;
    JobBatchID batch = jobInstancePool.jobs[index].instance.batch;
    if(index==lastIndex) {
        jobInstancePool.jobCount--;
        atomic_thread_fence(memory_order_release);
        avRWLockWriteUnlock(jobInstancePool.lock);
        goto signalComplete;
    }
    JobID lastId = jobInstancePool.jobReference[lastIndex];
    jobInstancePool.jobReference[lastIndex] = job;
    jobInstancePool.jobReference[index] = lastId;
    jobInstancePool.jobIndex[job] = lastIndex;
    jobInstancePool.jobIndex[lastId] = index;
    avMemswap(jobInstancePool.jobs + index, jobInstancePool.jobs + lastIndex, sizeof(struct JobInstancePoolSlot));
    jobInstancePool.jobCount--;
    atomic_thread_fence(memory_order_release);
    avRWLockWriteUnlock(jobInstancePool.lock);
signalComplete:
    if(signalJobComplete(batch)){
        // all work from batch is completed
        
        processBatchCompletion(batch);
        
    }
}


void getJobInstance(JobID job, JobInstance* instance, struct JobData* state){
    avRWLockReadLock(jobInstancePool.lock);
    uint32 index = jobInstancePool.jobIndex[job];
    JobInstance* target = &jobInstancePool.jobs[index].instance;
    struct JobData* targetState = &jobInstancePool.jobs[index].jobData;
    avMemcpy(instance, target, sizeof(JobInstance));
    avMemcpy(state, targetState, sizeof(struct JobData));
    avRWLockReadUnlock(jobInstancePool.lock);
}

void storeJobData(JobID job, struct JobData* data){
    avRWLockReadLock(jobInstancePool.lock);
    uint32 index = jobInstancePool.jobIndex[job];
    JobInstance* target = &jobInstancePool.jobs[index].instance;
    struct JobData* targetState = &jobInstancePool.jobs[index].jobData;
    avMemcpy(targetState, data, sizeof(struct JobData));
    avRWLockReadUnlock(jobInstancePool.lock);
}

struct batchSplit{uint32 count; uint32 remainingCount; } splitJobBatch(JobID* jobs, uint32 maxJobs, JobBatchID batch, WorkerThreadID workerThreadId){
    JobBatchDescription jobBatch;
    getJobBatch(batch, &jobBatch);

    uint32 remainingJobs = jobBatch.size - jobBatch.index;
    uint32 jobsToConsume = AV_MIN(maxJobs, remainingJobs);
    for(uint32 j = 0; j < jobsToConsume; j++){
        uint32 i = jobBatch.index + j;
        JobInstance jobBuffer;
        jobBuffer.input = jobBatch.inputData + jobBatch.inputOffset + jobBatch.inputStride * i;
        jobBuffer.output = jobBatch.outputData + jobBatch.outputOffset + jobBatch.outputStride * i;
        jobBuffer.inputSize = jobBatch.inputStride;
        jobBuffer.outputSize = jobBatch.outputStride;
        jobBuffer.batch = batch;
        jobBuffer.index = i;
        jobs[j] = allocateJob(&jobBuffer);
    }
    jobBatch.index += jobsToConsume;
    if(jobBatch.size==jobBatch.index){
        return (struct batchSplit) {.count=jobsToConsume, .remainingCount = 0};
    }

    writeJobBatch(batch, &jobBatch);
    return (struct batchSplit) {.count=jobsToConsume, .remainingCount = jobBatch.size - jobBatch.index};
}

static void wakeAdditionalWorkers(uint32 availableJobs){
    uint32 activeThreads = atomic_load(&state->activeThreads);
    uint32 idle = state->threadCount - activeThreads;
    uint32 available = availableJobs;
    if(idle > 0 && available > 1){
        uint32 wakeCount = AV_MIN(idle, available - 1);
        for(uint32 i = 0; i < wakeCount; i++){
            avSemaphorePost(state->workPresent);
        }
    }
}

static uint32 processJobBatchIntoBuffer(JobBatchID batch, WorkerThreadID workerThreadId, JobPriority priority, JobID* outJobs, uint32 capacity){
    uint32 jobCount = 0;
    struct batchSplit split = splitJobBatch(outJobs, capacity, batch, workerThreadId);
    jobCount = split.count; 

    // If batch was not fully consumed, store remainder as "working on"
    if (split.remainingCount != 0) {
        wakeAdditionalWorkers(split.remainingCount);
        if (atomic_exchange_explicit(&state->threads[workerThreadId].workingOnBatch[priority], batch, memory_order_acq_rel) != JOB_BATCH_NONE) {
            avFatal("Invalid state: workingOnBatch already set");
        }
    }
    return jobCount;
}



JobID pushJobsToLocalQueue(JobID* jobs, uint32 jobCount, JobPriority priority, LocalJobQueue* localQueue){
    if(jobCount == 0){
        return JOB_NONE;
    }
    if(jobCount > 1){
        if(!localJobQueuePushBatch(localQueue, priority, jobs + 1, jobCount - 1)){
            avFatal("Logic error in job counting"); // localQueue should be empty
        }

        wakeAdditionalWorkers(jobCount-1);
    }
    return jobs[0]; // we don't have to push the first job onto the queue as we can immediately use it
}

JobID getJobFromWorkinOnBatch(WorkerThreadID threadId, JobPriority priority, LocalJobQueue* localQueue){
    WorkerThread* thread = &state->threads[threadId];

    JobBatchID batch = atomic_exchange_explicit(&thread->workingOnBatch[priority], JOB_BATCH_NONE, memory_order_acq_rel);
    if(batch==JOB_BATCH_NONE){
        return JOB_NONE;
    }
    JobID jobs[LOCAL_QUEUE_RING_SIZE] = {JOB_NONE};
    uint32 jobCount = processJobBatchIntoBuffer(batch, threadId, priority, jobs, LOCAL_QUEUE_RING_SIZE);
    return pushJobsToLocalQueue(jobs, jobCount, priority, localQueue);
}

// the local queue needs to be topped up by the global queue(is empty)
JobID getJobFromGlobalQueue(JobQueue* globalQueue, LocalJobQueue* localQueue, JobPriority priority, WorkerThreadID workerThreadId){
    
    uint32 jobCount = 0;
    JobID jobs[LOCAL_QUEUE_RING_SIZE] = {JOB_NONE};

    while(jobCount < LOCAL_QUEUE_RING_SIZE){
        // if(!avSemaphoreTryWait(state->priorityPresent[priority])){
        //     break; // none of this priority present
        // }
        JobBatchID jobBatchId = jobQueuePull(globalQueue, priority);
        if(jobBatchId==JOB_BATCH_NONE){
            break;
        }

        uint32 remainingCount = LOCAL_QUEUE_RING_SIZE-jobCount;

        uint32 added = processJobBatchIntoBuffer(jobBatchId, workerThreadId, priority, jobs + jobCount, remainingCount);
        jobCount += added;
    }

    return pushJobsToLocalQueue(jobs, jobCount, priority, localQueue);
}

JobID stealBatchFromOtherThread(WorkerThreadID threadId, uint32* stealIndex, JobPriority priority, LocalJobQueue* localQueue){
    for(uint32 i = 0; i < state->threadCount; i++){
        uint32 index = ((*stealIndex) + i) % state->threadCount;
        if(index == threadId) continue;

        // first try to steal the working on batch
        JobBatchID batch = atomic_exchange_explicit(&state->threads[index].workingOnBatch[priority], JOB_BATCH_NONE, memory_order_acq_rel);
        if(batch!=JOB_BATCH_NONE){
            JobID jobs[LOCAL_QUEUE_RING_SIZE] = {JOB_NONE};
            uint32 jobCount = processJobBatchIntoBuffer(batch, threadId, priority, jobs, LOCAL_QUEUE_RING_SIZE);
            *stealIndex = (index + 1) % state->threadCount;
            return pushJobsToLocalQueue(jobs, jobCount, priority, localQueue);
        }
    }
    return JOB_NONE;
}

JobID stealJobFromOtherThreadsLocalQueue(WorkerThreadID threadId, uint32* stealIndex, JobPriority priority){
    JobID job = JOB_NONE;
    for(uint32 i = 0; i < state->threadCount; i++){
        uint32 index = ((*stealIndex) + i) % state->threadCount;
        if(index == threadId) continue;
        job = localJobQueueSteal(&state->threads[index].localQueue, priority);
        if(job != JOB_NONE){
            *stealIndex = (index + 1) % state->threadCount;
            break;
        }
    }
    return job;
}

JobID stealJobFromOtherThread(WorkerThreadID threadId, uint32* stealIndex, JobPriority priority, LocalJobQueue* localQueue){
    JobID job = JOB_NONE;
    job = stealBatchFromOtherThread(threadId, stealIndex, priority, localQueue);
    if(job != JOB_NONE) return job;
    job = stealJobFromOtherThreadsLocalQueue(threadId, stealIndex, priority);
    return job;
}

bool32 waitForWork(WorkerThreadID threadId){
    WorkerThread* thread = &state->threads[threadId];
    uint32 activeThreads = atomic_fetch_sub(&state->activeThreads, 1)-1;
    //avLog(AV_DEBUG_INFO, "Worker thread %u awaiting work. %u active threads", threadId, activeThreads);
    avSemaphoreWait(state->workPresent);
    atomic_fetch_add(&state->activeThreads, 1);
    if(state->awaitingExit){
        return true;
    }
    return false;
}

bool32 pushbackJob(JobID job, struct JobData* jobData, LocalJobQueue* localQueue, JobPriority priority){
    storeJobData(job, jobData);
    return localJobQueuePush(localQueue, priority, job);
}

JobID selectJob(WorkerThreadID threadId, JobQueue* globalQueue, LocalJobQueue* localQueue, uint32* stealIndex, JobPriority* priorityPtr){
    JobPriority priority = JOB_PRIORITY_MAX;
    JobID job = JOB_NONE;
    for(; priority <= JOB_PRIORITY_MIN; priority++){
        job = atomic_exchange(&state->exchange[priority], JOB_NONE);
        if(job!=JOB_NONE) break;
        job = localJobQueuePull(localQueue, priority);
        if(job!=JOB_NONE) break;
        job = getJobFromWorkinOnBatch(threadId, priority, localQueue);
        if(job!=JOB_NONE) break;
        job = getJobFromGlobalQueue(globalQueue, localQueue, priority, threadId);
        if(job!=JOB_NONE) break;
        job = stealJobFromOtherThread(threadId, stealIndex, priority, localQueue);
        if(job!=JOB_NONE) break;
    }
    if(job!=JOB_NONE) *priorityPtr = priority;
    return job;
}

JobID selectJobForMainThread(uint32* stealIndex, JobPriority* priorityPtr, JobPriority lowestPriority){
    JobPriority priority = JOB_PRIORITY_MAX;
    JobID job = JOB_NONE;
    for(; priority <= lowestPriority; priority++){
        job = atomic_exchange(&state->exchange[priority], JOB_NONE);
        if(job!=JOB_NONE) break;
        job = stealJobFromOtherThreadsLocalQueue((uint32)-1, stealIndex, priority);
        if(job!=JOB_NONE) break;
    }
    if(job!=JOB_NONE) *priorityPtr = priority;
    return job;
}

static JobID tryGetHigherPriorityJob(JobPriority* currentPriority, WorkerThreadID threadId, LocalJobQueue* localQueue, JobQueue* globalQueue){
    for (uint32 i = JOB_PRIORITY_MAX; i < *currentPriority; i++) {
        JobID job = getJobFromGlobalQueue(globalQueue, localQueue, i, threadId);
        if (job != JOB_NONE) {
            *currentPriority = i;
            return job;
        }
    }
    return JOB_NONE;
}

static void prepareJob(JobID job, JobInstance* instance, struct JobData* jobData, JobBatchDescription* batch, JobContext* context, WorkerThreadID threadId){
    getJobInstance(job, instance, jobData);
    JobBatchID batchId = instance->batch;
    getJobBatch(batchId, batch);
    *context = (JobContext){0};
    context->state = jobData->state;
    context->stateSize = JOB_STATE_SIZE;
    context->threadId = threadId;
    context->batch = batchId;
    context->index = instance->index;
}

static void updateJob(JobPriority priority, struct JobData jobData, JobContext* context){
    context->priority = priority;
    context->stateOffset = 0;
    context->shouldYield = priority!=JOB_PRIORITY_MAX; // highest priority should never yield as there is nothing that could interrupt it
    context->exec.section = jobData.context.section;
    context->exec.subsection = jobData.context.subsection;
}

static bool32 runJob(JobID job, JobInstance instance, JobBatchDescription batch, JobContext* context, struct JobData* jobData){
    JobControl control = batch.entry(instance.input, instance.inputSize, instance.output, instance.outputSize, context);
    jobData->context.section = control.nextSection;
    switch(control.ret){
        default:
        case JOB_ERROR:
        case JOB_EXIT_UNDEFINED:
        case JOB_STATE_OVERRUN:
            avFatal("error while running job");
            freeJob(job);
            return true;
        case JOB_EXIT_NORMAL:
            //avLog(AV_DEBUG_SUCCESS, "Job exited successfully");
            freeJob(job);
            return true;
        case JOB_YIELD:
            return false;
    }
}

static struct JobInfo { 
    JobID job; 
    JobPriority priority; 
} executeJob(JobID job, WorkerThreadID threadId, JobPriority priority, LocalJobQueue* localQueue, JobQueue* globalQueue){
    JobInstance instance;
    JobBatchDescription batch;
    struct JobData jobData;
    JobContext context;
    prepareJob(job, &instance, &jobData, &batch, &context, threadId);
    
    while(true) {
        updateJob(priority, jobData, &context);

        if(runJob(job, instance, batch, &context, &jobData)) return (struct JobInfo) {.job=JOB_NONE, .priority=0};
        JobPriority newPriority = priority;
        JobID higherPriorityJob = tryGetHigherPriorityJob(&newPriority, threadId, localQueue, globalQueue);
        if(higherPriorityJob!=JOB_NONE){
            pushbackJob(job, &jobData, localQueue, priority); // we can ignore return as there is always room in the queue the job came from
            job = higherPriorityJob;
            priority = newPriority;
            return (struct JobInfo){.job = job, .priority = newPriority};
        }
    }
}

AV_API void jobFenceWait(JobFence fence){
    if(avThreadGetID() != AV_MAIN_THREAD_ID){
        avError("Can only wait on fence from main thread");
        return;
    }
    uint32 stealIndex = 0;
    JobID job = JOB_NONE;
    bool32 inProgress = false;
    JobPriority priority = JOB_PRIORITY_MAX;
    struct JobData jobData;
    while(atomic_load(&fence->workLeft)!=0){
        if(job==JOB_NONE){
            priority = JOB_PRIORITY_MAX;
            job = selectJobForMainThread(&stealIndex, &priority, JOB_PRIORITY_MEDIUM);
            if(job==JOB_NONE){
                avThreadYield();
                continue;
            }
        }else{
            JobInstance instance;
            JobBatchDescription batch;
            JobContext context;
            prepareJob(job, &instance, &jobData, &batch, &context, state->threadCount);
            updateJob(priority, jobData, &context);
            inProgress = true;
            if(runJob(job, instance, batch, &context, &jobData)) {
                job = JOB_NONE;
                inProgress = false;
                continue;
            }
        }
    }
    if(inProgress){
        storeJobData(job, &jobData);
        atomic_thread_fence(memory_order_release);
        JobID slotJob = atomic_exchange(&state->exchange[priority], job);
        if(slotJob != JOB_NONE){
            avError("Exchange still contains job");
            return;
        }
    }
}

int32 workerThreadEntry(byte* data, uint64 size){
    WorkerThreadID threadId = (WorkerThreadID) size;
    WorkerThread* thread = &state->threads[threadId];
    thread->id = avThreadGetID();
    LocalJobQueue* localQueue = &thread->localQueue;
    JobQueue* globalQueue = &state->jobQueue;
    uint32 stealIndex = (threadId+1) % state->threadCount;
    bool32 running = true;
    atomic_fetch_add(&state->activeThreads, 1);
    while(running){
        
        JobPriority priority;
        JobID job = selectJob(threadId, globalQueue, localQueue, &stealIndex, &priority);
        

        if(job==JOB_NONE){
            // no work to be done
            if(waitForWork(threadId)) break;
            continue;
            //avDebug("Worker thread %u, is woken", threadId);
        }
        while(job!=JOB_NONE){
            struct JobInfo info = executeJob(job, threadId, priority, localQueue, globalQueue);
            priority = info.priority;
            job = info.job;
        }
    }
    avLog(AV_DEBUG_INFO, "Worker thread %u exiting", threadId);
    return 0;
}

AV_API void jobFenceCreate(JobFence* fence){
    if(avThreadGetID() != AV_MAIN_THREAD_ID){
        avError("Can only create fence from main thread");
        return;
    }
    *fence = avAllocate(sizeof(struct JobFence), "");
    avMemset(*fence, 0, sizeof(struct JobFence));
    atomic_store(&(*fence)->workLeft, 0); 
}

AV_API void jobFenceInitRaw(JobFence* fence, void* mem){
    avMemset(mem, 0, sizeof(struct JobFence));
    *fence = mem;
    atomic_store(&(*fence)->workLeft, 0); 
}

AV_API void jobFenceDestroy(JobFence fence){
    if(avThreadGetID() != AV_MAIN_THREAD_ID){
        avError("Can only destroy fence from main thread");
        return;
    }
    if(atomic_load(&fence->workLeft)!=0){
        jobFenceWait(fence);
    }
    avFree(fence);
}

static void attachWorkToFence(JobFence fence){
    atomic_fetch_add(&fence->workLeft, 1);
}

AV_API JobBatchID submitJobBatch(JobBatchDescription* batch, JobFence fence){
    if(batch->size==0){
        return JOB_BATCH_NONE;
    }
    if(fence != NULL){
        if(avThreadGetID() != AV_MAIN_THREAD_ID){
            avError("Tried to submit job batch with fence from worker thread");
            return JOB_BATCH_NONE;
        }
        batch->fence = fence;
        attachWorkToFence(fence);
    }
    JobBatchID id = allocateJobBatch(batch, NULL, NULL);
    if(id == JOB_BATCH_NONE) {
        avError("Failed to allocate job batch");
        return JOB_BATCH_NONE;
    }
    attachWorkToFence(&state->allWorkDoneFence);
    if(batch->flags.completeThisFrame){
        attachWorkToFence(&state->frameFence);
    }
    return submitToMainQueue(batch->flags.priority, id);
}

JobBatchID submitToMainQueue(JobPriority priority, JobBatchID id){
    if(!jobQueuePush(&state->jobQueue, priority, id)){
        avError("Failed to submit job to queue");
        atomic_fetch_sub(&state->allWorkDoneFence.workLeft, 1);
        return JOB_BATCH_NONE;
    }
    avSemaphorePost(state->workPresent);
    return id;
}

AV_API JobBatchID submitJobBatchWithDependencies(JobBatchDescription* batch, uint32 dependencyCount, JobBatchID* dependencies, JobFence fence){
    if(batch->size==0){
        return JOB_BATCH_NONE;
    }
    if(dependencyCount==0){
        return submitJobBatch(batch, fence);
    }
    if(fence != NULL){
        if(avThreadGetID() != AV_MAIN_THREAD_ID){
            avError("Tried to submit job batch with fence from worker thread");
            return JOB_BATCH_NONE;
        }
        batch->fence = fence;
        attachWorkToFence(fence);
    }
    attachWorkToFence(&state->allWorkDoneFence);
    JobBatchID id = allocateJobBatch(batch, dependencyCount, dependencies);
    if(id == JOB_BATCH_NONE){
        avError("Failed to allocate job batch");
        return JOB_BATCH_NONE;
    }
    return id; // No need to do anything else as allocate jobBatch handles submission to the global queue
}

void processBatchCompletion(JobBatchID id){
    JobBatchDescription batch;
    getJobBatch(id, &batch);

    // if(batch.onComplete){
    //     avRWLockReadLock(state->resultLock);

    //     uint32 index = atomic_fetch_add_explicit(&state->resultBufferIndex, 1, memory_order_acq_rel);
    //     if(index >= JOB_RESULT_BUFFER_SIZE){
    //         avFatal("Job result buffer overrun");
    //         avRWLockReadUnlock(state->resultLock);
    //         goto doFreeJob;
    //     }

    //     state->resultBuffer[index] = id;

    //     avRWLockReadUnlock(state->resultLock);
    //     return;
    // }else
    {
        doFreeJob:
        freeJobBatch(id);
        atomic_fetch_sub(&state->allWorkDoneFence.workLeft, 1);
    }
}

// void processResults(){
//     avRWLockWriteLock(state->resultLock);
//     atomic_thread_fence(memory_order_acquire);

//     for(uint32 i = 0; i < state->resultBufferIndex; i++){
//         JobBatchDescription batch;
//         JobBatchID id = state->resultBuffer[i];
//         getJobBatch(id, &batch);
//         if(!batch.onComplete){
//             avError("logic error");
//             continue;
//         }
//         batch.onComplete(batch.inputData, batch.inputStride, batch.outputData, batch.outputStride);
//         freeJobBatch(id);
//         atomic_fetch_sub(&state->allWorkDoneFence.workLeft, 1);
//     }
//     state->resultBufferIndex = 0;
//     atomic_thread_fence(memory_order_release);
//     avRWLockWriteUnlock(state->resultLock);

// }

// AV_API void jobSystemUpdate(){
//     if(avThreadGetID()!=AV_MAIN_THREAD_ID) {
//         avError("Can only process results on main thread");
//         return;
//     };

//     jobFenceWait(&state->frameFence);
//     processResults();
// }

AV_API uint32 jobSystemGetWorkerCount(){
    return state->threadCount;
}
AV_API void jobSystemQuerryWorkerThreads(uint32* threadCount, AvThreadID* threadIds){
    if(!state) return;
    *threadCount = jobSystemGetWorkerCount();
    if(threadIds==NULL) return;
    for(uint32 i = 0; i < *threadCount; i++){
        threadIds[i] = state->threads[i].id;
    }
}