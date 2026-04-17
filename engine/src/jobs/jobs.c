#include "jobs.h"
#include "jobQueue.h"
#define AV_LOG_CATEGORY "job system" 
#include "logging.h"

#include <AvUtils/avThreading.h>
#include <AvUtils/threading/avSemaphore.h>
#include <AvUtils/threading/avConditionVariable.h>
#include <AvUtils/avMemory.h>
#include <AvUtils/threading/avRwLock.h>
#include <AvUtils/avMath.h>
#include <AvUtils/avString.h>
typedef enum JobState {
    JOB_STATE_READY, // ready to be executed
    JOB_STATE_EXECUTING, // currently executing
    JOB_STATE_DONE, // complted executing
} JobState;


typedef struct JobInstance {
    _Atomic JobState state;
    JobPriority priority;
    JobBatchID batch;
//    JobPriority effectivePriority; // priority inheritance
    byte* input;
    byte* output;
    uint32 inputSize;
    uint32 outputSize;
    JobEntry entry;
    JobResultCallback onSuccess;
    JobResultCallback onFailure;
    byte* runState;
    uint32 stateSize;
    _Atomic WorkerThreadID workerThread;
} JobInstance;


#define LOCAL_QUEUE_RING_SIZE 1024
#define LOCAL_QUEUE_SIZE LOCAL_QUEUE_RING_SIZE*JOB_PRIORITY_COUNT
#define GLOBAL_QUEUE_RING_SIZE 4096
#define GLOBAL_QUEUE_SIZE GLOBAL_QUEUE_RING_SIZE*JOB_PRIORITY_COUNT
#define MAX_CORE_COUNT 32
#define JOB_INSTANCE_POOL_SIZE GLOBAL_QUEUE_SIZE + LOCAL_QUEUE_SIZE * MAX_CORE_COUNT / 2

typedef struct JobInstancePool {
    _Atomic uint32 jobCount;
    uint32 jobIndex[JOB_INSTANCE_POOL_SIZE];
    JobID jobReference[JOB_INSTANCE_POOL_SIZE];
    struct JobInstancePoolSlot {
        JobInstance instance;
        struct JobData {
            JobExecutionContext context;
            byte state[JOB_STATE_SIZE];
        } jobData;
    }jobs[JOB_INSTANCE_POOL_SIZE];
    AvRwLock lock;
} JobInstancePool;
JobInstancePool jobInstancePool = {0};

typedef struct JobBatchPool {
    uint32 jobBatchCount;
    uint32 jobBatchIndex[JOB_INSTANCE_POOL_SIZE];
    union JobBatchReferenceSlot {
        JobBatchID id;
        union {
            uint32 generation;
            uint32 identifier;
        };
    } jobBatchReference[JOB_INSTANCE_POOL_SIZE];
    struct JobBatchPoolSlot {
        JobBatchDescription batch;
        _Atomic uint32 remainingJobs;
    } jobBatches[JOB_INSTANCE_POOL_SIZE];
    AvRwLock lock;
} JobBatchPool;
JobBatchPool jobBatchPool = {0};

struct JobFence {
    _Atomic uint32 workLeft;
};

typedef struct WorkerThread {
    AvThread thread;
    LocalJobQueue localQueue;
    uint32 id;
    // keep batch around that we have already split of some jobs, so that we dont have to push it to the back of the queue again
    // threads that try to steal, first try to steal the batch instead of from the local queue to prevent congestion
    _Atomic JobBatchID workingOnBatch[JOB_PRIORITY_COUNT]; 
} WorkerThread;

typedef struct JobSystemState {
    WorkerThread* threads;
    uint32 threadCount;
    JobQueue jobQueue;
    AvSemaphore workPresent;
    //AvSemaphore priorityPresent[JOB_PRIORITY_COUNT];
    bool8 awaitingExit;
    _Atomic uint32 activeThreads;
} JobSystemState;

static JobSystemState* state = NULL;



int32 workerThreadEntry(byte* data, uint64 size);
bool32 jobSystemInitialize(uint64* memoryRequirement, void* statePtr, void* configPtr){
    *memoryRequirement = sizeof(JobSystemState);
    if(statePtr==NULL) return true;
    state = (JobSystemState*) statePtr;
    avMemset(state, 0, sizeof(JobSystemState));

    JobSystemConfig* config = (JobSystemConfig*) configPtr;
    
    state->threadCount = config->maxWorkerThreads + 1; // +1 for main thread that optionally might help in execution
    uint32 threadsSize = sizeof(WorkerThread) * state->threadCount;
    uint32 localQueueSize = sizeof(JobID) * LOCAL_QUEUE_SIZE * state->threadCount;
    uint32 globalQueueSize = sizeof(JobSlot) * GLOBAL_QUEUE_SIZE;
    state->threads = avAllocate(threadsSize + localQueueSize + globalQueueSize, "");
    WorkerThread* threads = state->threads;
    JobID* jobsBuffer = (JobID*)(threads + state->threadCount);
    JobSlot* jobbatchSlots = (JobSlot*)(jobsBuffer + LOCAL_QUEUE_SIZE * state->threadCount);
    for(uint32 i = 0; i < state->threadCount; i++){
        state->threads[i] = (WorkerThread){0};
        if(i != 0){ 
            avThreadCreate(workerThreadEntry, &threads[i].thread);
            char buffer[16] = {0};
            avStringPrintfToBuffer(buffer, 16, AV_CSTRA("Worker %u"), i);
            avThreadSetName(threads[i].thread, buffer);
        }
        localJobQueueInit(&threads[i].localQueue, LOCAL_QUEUE_RING_SIZE, jobsBuffer + LOCAL_QUEUE_SIZE*i);
    }
    jobQueueInit(&state->jobQueue, GLOBAL_QUEUE_RING_SIZE, jobbatchSlots);

    avRWLockCreate(&jobBatchPool.lock);
    avRWLockCreate(&jobInstancePool.lock);
    avSemaphoreCreate(&state->workPresent, 0);
    atomic_store(&state->activeThreads, 0);

    for(uint32 i = 0; i < JOB_INSTANCE_POOL_SIZE; i++){
        jobInstancePool.jobIndex[i] = i;
        jobInstancePool.jobReference[i] = i;
        jobBatchPool.jobBatchIndex[i] = i;
        jobBatchPool.jobBatchReference[i] = (union JobBatchReferenceSlot){.generation=0, .identifier=i};
    }

    // for(uint32 i = 0; i < JOB_PRIORITY_COUNT; i++){
    //     avSemaphoreCreate(&state->priorityPresent[i], 0);
    // }
    

    for(uint32 i = 1; i < state->threadCount; i++){
        avThreadStart(NULL, i, state->threads[i].thread);
    }

    return true;
}

void jobSystemDeinitialize(void* statePtr){
    if(state==NULL) return;
    
    state->awaitingExit = true;

    for(uint32 i = 0; i < state->threadCount; i++){
        avSemaphorePost(state->workPresent);
    }

    

    // for(uint32 i = 0; i < JOB_PRIORITY_COUNT; i++){
    //     avSemaphoreDestroy(state->priorityPresent[i]);
    // }
    
    for(uint32 i = 1; i < state->threadCount; i++){
        avThreadDestroy(state->threads[i].thread);
    }

    avSemaphoreDestroy(state->workPresent);
    avRWLockDestroy(jobInstancePool.lock);
    avRWLockDestroy(jobBatchPool.lock);

    avFree(state->threads);


    if(jobInstancePool.jobCount){
        avError("%u jobs are still active", jobInstancePool.jobCount);
    }
    if(jobBatchPool.jobBatchCount){
        avError("%u batches are still active", jobBatchPool.jobBatchCount);
    }
}

// JobInstancePool -> single job instances
// JobBatchPool -> job batches


// main thread has per priority queue

// worker thread has local per priority queue

// worker thread:
// 1: checks local queue for highest priority
// 2: checks global queue for highest priority
// 3: checks other threads for highest priority (stealing)
// 4: take highest priority from global queue
// 5: repeat for lower priorities

// worker thread (job yields):
// 1: checks global thread for highest priorty
// 1a: checks for priority upto priority of yielding job
// 2: if higher priority job found, take it
// 3: if no higher priority job, push yielding job to end of local queue

// submiting jobs:
// jobs are submitted in batches in global queue
// all jobs of a batch are same priority
// once jobs move to local queues they are split up into single jobs

// stealing:
// stealing jobs from threads is allowed up to 2 remaining jobs
// 

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

JobBatchID allocateJobBatch(JobBatchDescription* description){
    avRWLockReadLock(jobBatchPool.lock);
    uint32 index = atomic_fetch_add(&jobBatchPool.jobBatchCount, 1);
    if(index >= JOB_INSTANCE_POOL_SIZE){
        atomic_store_explicit(&jobBatchPool.jobBatchCount, JOB_INSTANCE_POOL_SIZE, memory_order_relaxed);
        avRWLockReadUnlock(jobBatchPool.lock);
        return JOB_BATCH_NONE;
    }
    avMemset(jobBatchPool.jobBatches + index, 0, sizeof(struct JobBatchPoolSlot));
    avMemcpy(&(jobBatchPool.jobBatches + index)->batch, description, sizeof(JobBatchDescription));
    jobBatchPool.jobBatches[index].remainingJobs = description->size;
    JobBatchID id = jobBatchPool.jobBatchReference[index].id;
    atomic_thread_fence(memory_order_release);
    avRWLockReadUnlock(jobBatchPool.lock);
    return id;
}

bool32 signalJobComplete(JobBatchID batch){
    avRWLockReadLock(jobBatchPool.lock);
    uint32 identifier = ((union JobBatchReferenceSlot)batch).identifier;
    uint32 generation = ((union JobBatchReferenceSlot)batch).generation;
    uint32 index = jobBatchPool.jobBatchIndex[identifier];
    if(generation != jobBatchPool.jobBatchReference[index].generation){
        avError("Accessed expired handle");
        avRWLockReadUnlock(jobBatchPool.lock);
        return false;
    }
    uint32 remainingJobs = atomic_fetch_sub_explicit(&jobBatchPool.jobBatches[index].remainingJobs, 1, memory_order_seq_cst) - 1;
    avRWLockReadUnlock(jobBatchPool.lock);
    if(remainingJobs == 0){
        return true;
    }
    return false;
}

void getJobBatch(JobBatchID id, JobBatchDescription* batch){
    avRWLockReadLock(jobBatchPool.lock);
    uint32 identifier = ((union JobBatchReferenceSlot)id).identifier;
    uint32 generation = ((union JobBatchReferenceSlot)id).generation;
    uint32 index = jobBatchPool.jobBatchIndex[identifier];
    if(generation != jobBatchPool.jobBatchReference[index].generation){
        avError("Accessed expired handle");
        avRWLockReadUnlock(jobBatchPool.lock);
        return;
    }
    JobBatchDescription* target = &jobBatchPool.jobBatches[index].batch;
    avMemcpy(batch, target, sizeof(JobBatchDescription));
    avRWLockReadUnlock(jobBatchPool.lock);
}

void onBatchComplete(JobBatchID batch){
    avRWLockReadLock(jobBatchPool.lock);
    avDebug("JobBatch %u is complete", batch);
    JobBatchDescription description;
    getJobBatch(batch, &description);
    avRWLockReadUnlock(jobBatchPool.lock);

    if(description.fence){
        atomic_fetch_sub(&description.fence->workLeft, 1);
    }
}

void freeJobBatch(JobBatchID batch){
    onBatchComplete(batch);

    avRWLockWriteLock(jobBatchPool.lock);
    atomic_thread_fence(memory_order_acquire);
    uint32 identifier = ((union JobBatchReferenceSlot)batch).identifier;
    uint32 generation = ((union JobBatchReferenceSlot)batch).generation;
    uint32 index = jobBatchPool.jobBatchIndex[identifier];
    if(generation != jobBatchPool.jobBatchReference[index].generation){
        avError("Accessed expired handle");
        avRWLockWriteUnlock(jobBatchPool.lock);
        return;
    }
    jobBatchPool.jobBatchReference[index].generation++;

    uint32 lastIndex = jobBatchPool.jobBatchCount - 1;
    if(index == lastIndex){
        jobBatchPool.jobBatchCount--;
        atomic_thread_fence(memory_order_release);
        avRWLockWriteUnlock(jobBatchPool.lock);
        return;
    }
    union JobBatchReferenceSlot lastId = jobBatchPool.jobBatchReference[lastIndex];
    union JobBatchReferenceSlot currentId = jobBatchPool.jobBatchReference[index];
    jobBatchPool.jobBatchReference[index] = lastId;
    jobBatchPool.jobBatchReference[lastIndex] = currentId;
    jobBatchPool.jobBatchIndex[lastId.id] = index;
    jobBatchPool.jobBatchIndex[identifier] = lastIndex;
    avMemswap(jobBatchPool.jobBatches + index, jobBatchPool.jobBatches + lastIndex, sizeof(struct JobBatchPoolSlot));
    jobBatchPool.jobBatchCount--;
    atomic_thread_fence(memory_order_release);
    avRWLockWriteUnlock(jobBatchPool.lock);

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
        freeJobBatch(batch);
    }
}

void writeJobBatch(JobBatchID id, JobBatchDescription* batch){
    avRWLockReadLock(jobBatchPool.lock);
    uint32 identifier = ((union JobBatchReferenceSlot)id).identifier;
    uint32 generation = ((union JobBatchReferenceSlot)id).generation;
    uint32 index = jobBatchPool.jobBatchIndex[identifier];
    if(generation != jobBatchPool.jobBatchReference[index].generation){
        avError("Accessed expired handle");
        avRWLockReadUnlock(jobBatchPool.lock);
        return;
    }
    JobBatchDescription* target = &jobBatchPool.jobBatches[index].batch;
    avMemcpy(target, batch, sizeof(JobBatchDescription));
    avRWLockReadUnlock(jobBatchPool.lock);
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

    uint32 jobsToConsume = AV_MIN(maxJobs, jobBatch.size);
    for(uint32 i = 0; i < jobsToConsume; i++){
        JobInstance jobBuffer;
        jobBuffer.entry = jobBatch.entry;
        jobBuffer.input = jobBatch.inputData + jobBatch.inputStride * i;
        jobBuffer.output = jobBatch.outputData + jobBatch.outputStride * i;
        jobBuffer.inputSize = jobBatch.outputStride;
        jobBuffer.outputSize = jobBatch.outputStride;
        jobBuffer.onFailure = jobBatch.onFailure;
        jobBuffer.onSuccess = jobBatch.onSuccess;
        jobBuffer.priority = jobBatch.priority;
        jobBuffer.state = JOB_STATE_READY;
        jobBuffer.workerThread = workerThreadId;
        jobBuffer.batch = batch;
        jobs[i] = allocateJob(&jobBuffer);
    }
    jobBatch.inputData += jobBatch.inputStride * jobsToConsume;
    jobBatch.outputData += jobBatch.outputStride * jobsToConsume;
    jobBatch.size -= jobsToConsume;
    if(jobBatch.size==0){
        return (struct batchSplit) {.count=jobsToConsume, .remainingCount = 0};
    }

    writeJobBatch(batch, &jobBatch);
    return (struct batchSplit) {.count=jobsToConsume, .remainingCount = jobBatch.size};
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

JobID stealJobFromOtherThread(WorkerThreadID threadId, uint32* stealIndex, JobPriority priority, LocalJobQueue* localQueue){
    JobID job = JOB_NONE;
    for(uint32 i = 0; i < state->threadCount; i++){
        uint32 index = ((*stealIndex) + i) % state->threadCount;
        if(index == threadId) continue;

        // first try to steal the working on batch
        JobBatchID batch = atomic_exchange_explicit(&state->threads[index].workingOnBatch[priority], JOB_BATCH_NONE, memory_order_acq_rel);
        if(batch!=JOB_BATCH_NONE){
            JobID jobs[LOCAL_QUEUE_RING_SIZE] = {JOB_NONE};
            uint32 jobCount = processJobBatchIntoBuffer(batch, threadId, priority, jobs, LOCAL_QUEUE_RING_SIZE);
            return pushJobsToLocalQueue(jobs, jobCount, priority, localQueue);
        }

        // try to steal from local queue
        job = localJobQueueSteal(&state->threads[index].localQueue, priority);
        if(job != JOB_NONE){
            *stealIndex = (index + 1) % state->threadCount;
            break;
        }
    }
    // if(job!=JOB_NONE){
    //     avDebug("Job stolen");
    // }
    return job;
}

bool32 waitForWork(WorkerThreadID threadId){
    WorkerThread* thread = &state->threads[threadId];
    uint32 activeThreads = atomic_fetch_sub(&state->activeThreads, 1)-1;
    //avLog(AV_DEBUG_INFO, "Worker thread %u awaiting work. %u active threads", threadId, activeThreads);
    if(threadId==0){
        return true;
    }
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

void executeJob(JobID job, WorkerThreadID threadId, JobPriority priority, LocalJobQueue* localQueue, JobQueue* globalQueue){
executeJob:
    //avDebug("Worker thread %u is executing job", threadId);
    JobInstance instance;
    struct JobData jobData;
    getJobInstance(job, &instance, &jobData);
    JobContext context = {
        .state = jobData.state,
    };
    context.stateSize = JOB_STATE_SIZE;
    context.threadId = threadId;
runJobSection:
    context.priority = priority;
    context.stateOffset = 0;
    context.shouldYield = priority!=JOB_PRIORITY_MAX; // highest priority should never yield as there is nothing that could interrupt it
    context.exec.section = jobData.context.section;
    context.exec.subsection = jobData.context.subsection;

    JobControl control = instance.entry(instance.input, instance.inputSize, instance.output, instance.outputSize, &context);
    
    switch(control.ret){
        default:
        case JOB_EXIT_NONE:
        case JOB_EXIT_STATE_OVERRUN:
            avFatal("error while running job");
            freeJob(job);
            break;
        case JOB_EXIT_FAILURE:
            //TODO: handle success
            //avLog(AV_DEBUG_INFO, "Job exited unsuccessfully");
            freeJob(job);
            break;
        case JOB_EXIT_SUCCESS:
            //TODO: handle failure
            //avLog(AV_DEBUG_SUCCESS, "Job exited successfully");
            freeJob(job);
            break;
        case JOB_YIELD:
            JobID higherPriorityJob = JOB_NONE;
            if(threadId != 0){ // we want the main thread to stop working after each job so that it can check fences
                for(uint32 i = JOB_PRIORITY_MAX; i < priority; i++){
                    higherPriorityJob = getJobFromGlobalQueue(globalQueue, localQueue, i, threadId);
                    if(higherPriorityJob!=JOB_NONE) break;
                }
            }
            if(higherPriorityJob!=JOB_NONE){
                pushbackJob(job, &jobData, localQueue, priority); // we can ignore return as there is always room in the queue the job came from
                job = higherPriorityJob;
                goto executeJob;
            }
            jobData.context.section = control.nextSection;
            if(control.priorityHint > priority){ // the new priority is lower than the current priority 
                if(!pushbackJob(job, &jobData, localQueue, control.priorityHint)){
                    localJobQueuePush(localQueue, priority, job); // there is always space in current priority queue
                }
            }else{
                priority = control.priorityHint;
                goto runJobSection;
            }
            break;
    }
}

int32 workerThreadEntry(byte* data, uint64 size){
    WorkerThreadID threadId = (WorkerThreadID) size;
    WorkerThread* thread = &state->threads[threadId];
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

        executeJob(job, threadId, priority, localQueue, globalQueue);
    }
    avLog(AV_DEBUG_INFO, "Worker thread %u exiting", threadId);
    return 0;
}

void jobFenceCreate(JobFence* fence){
    if(avThreadGetID() != AV_MAIN_THREAD_ID){
        avError("Can only create fence from main thread");
        return;
    }
    *fence = avAllocate(sizeof(struct JobFence), "");
    avMemset(*fence, 0, sizeof(struct JobFence));
    atomic_store(&(*fence)->workLeft, 0); 
}
void jobFenceDestroy(JobFence fence){
    if(avThreadGetID() != AV_MAIN_THREAD_ID){
        avError("Can only destroy fence from main thread");
        return;
    }
    if(atomic_load(&fence->workLeft)!=0){
        jobFenceWait(fence);
    }
    avFree(fence);
}
void jobFenceWait(JobFence fence){
    if(avThreadGetID() != AV_MAIN_THREAD_ID){
        avError("Can only wait on fence from main thread");
        return;
    }
    while(atomic_load(&fence->workLeft)!=0){

        //Do usefull work

    }
}
static void attachWorkToFence(JobFence fence){
    atomic_fetch_add(&fence->workLeft, 1);
}

bool32 submitJobBatch(JobBatchDescription* batch, JobFence fence){
    if(fence != NULL){
        if(avThreadGetID() != AV_MAIN_THREAD_ID){
            avError("Tried to submit job batch with fence from worker thread");
            return false;
        }
        batch->fence = fence;
        attachWorkToFence(fence);
    }
    JobBatchID id = allocateJobBatch(batch);
    if(id == JOB_BATCH_NONE) {
        avError("Failed to allocate job batch");
        return false;
    }
    if(!jobQueuePush(&state->jobQueue, batch->priority, id)){
        avError("Failed to submit job to queue");
        return false;
    }
    avSemaphorePost(state->workPresent);
    return true;
}
