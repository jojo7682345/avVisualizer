#include "jobs.h"
#include "jobQueue.h"
#include "logging.h"

#include <AvUtils/avThreading.h>
#include <AvUtils/threading/avSemaphore.h>
#include <AvUtils/threading/avConditionVariable.h>
#include <AvUtils/avMemory.h>
#include <AvUtils/threading/avRwLock.h>
#include <AvUtils/avMath.h>
typedef enum JobState {
    JOB_STATE_READY, // ready to be executed
    JOB_STATE_EXECUTING, // currently executing
    JOB_STATE_DONE, // complted executing
} JobState;

typedef uint32 WorkerThreadID;

typedef struct JobInstance {
    _Atomic JobState state;
    JobPriority priority;
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
    _Atomic uint16 jobCount;
    uint16 jobIndex[JOB_INSTANCE_POOL_SIZE];
    JobID jobReference[JOB_INSTANCE_POOL_SIZE];
    struct JobInstancePoolSlot {
        JobInstance instance;
        struct JobData {
            JobContext context;
            byte state[JOB_STATE_SIZE];
        } jobData;
    }jobs[JOB_INSTANCE_POOL_SIZE];
    AvRwLock lock;
} JobInstancePool;
JobInstancePool jobInstancePool = {0};
int a = sizeof(JobInstancePool);
typedef uint32 JobBatchID;
typedef struct JobBatchPool {
    uint32 jobBatchCount;
    uint32 jobBatchIndex[JOB_INSTANCE_POOL_SIZE];
    JobBatchID jobBatchReference[JOB_INSTANCE_POOL_SIZE];
    struct {
        _Atomic remaningDependencies;   
        JobBatchDescription batch;
    }jobBatches[JOB_INSTANCE_POOL_SIZE];
    AvRwLock lock;
} JobBatchPool;
JobBatchPool jobBatchPool = {0};



typedef struct WorkerThread {
    AvThread thread;
    LocalJobQueue localQueue;
    uint32 id;
    AvMutex mutex;
} WorkerThread;

typedef struct JobSystemState {
    WorkerThread* threads;
    uint32 threadCount;
    JobQueue jobQueue;
    AvConditionVariable workPresent;
    AvSemaphore priorityPresent[JOB_PRIORITY_COUNT];
} JobSystemState;

static JobSystemState* state = NULL;



int32 workerThreadEntry(byte* data, uint64 size);
bool32 jobSystemInitialize(uint64* memoryRequirement, void* statePtr, void* configPtr){
    *memoryRequirement = sizeof(JobSystemState);
    if(statePtr==NULL) return true;
    state = (JobSystemState*) statePtr;

    JobSystemConfig* config = (JobSystemConfig*) configPtr;

    
    state->threads = avAllocate((sizeof(WorkerThread)+ sizeof(JobID)*LOCAL_QUEUE_SIZE)*config->maxWorkerThreads + sizeof(JobID)*GLOBAL_QUEUE_SIZE, "");
    state->threadCount = config->maxWorkerThreads;
    for(uint32 i = 0; i < config->maxWorkerThreads; i++){
        state->threads[i] = (WorkerThread){0};
        avThreadCreate(workerThreadEntry, &state->threads[i].thread);
        localJobQueueInit( &state->threads[i].localQueue, LOCAL_QUEUE_RING_SIZE, (JobID*)(state->threads + config->maxWorkerThreads) + LOCAL_QUEUE_SIZE*i);
        avMutexCreate(&state->threads[i].mutex);
    }
    jobQueueInit(state->jobQueue, GLOBAL_QUEUE_SIZE, ((JobID*)(state->threads+state->threadCount)+LOCAL_QUEUE_SIZE*state->threadCount));

    avRWLockCreate(&jobBatchPool.lock);
    avRWLockCreate(&jobInstancePool.lock);
    avConditionVariableCreate(&state->workPresent);

    for(uint32 i = 0; i < JOB_INSTANCE_POOL_SIZE; i++){
        jobInstancePool.jobIndex[i] = i;
        jobInstancePool.jobReference[i] = i;
        jobBatchPool.jobBatchIndex[i] = i;
        jobBatchPool.jobBatchReference[i] = i;
    }

    for(uint32 i = 0; i < JOB_PRIORITY_COUNT; i++){
        avSemaphoreCreate(&state->priorityPresent[i], 0);
    }


    return true;
}


void jobSystemDeinitialize(void* statePtr){
    if(state==NULL) return;
    for(uint32 i = 0; i < state->threadCount; i++){
        avThreadDestroy(state->threads[i].thread);
    }
    avFree(state->threads);
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
        atomic_store(&jobInstancePool.jobCount, JOB_INSTANCE_POOL_SIZE);
        return JOB_NONE; // full
    }
    avMemcpy(jobInstancePool.jobs + index, instance, sizeof(JobInstance));
    JobID id = jobInstancePool.jobReference[index];
    avRWLockReadUnlock(jobInstancePool.lock);
    return id;
}

void freeJob(JobID job){
    avRWLockWriteLock(jobInstancePool.lock);
    atomic_thread_fence(memory_order_acquire);
    uint32 index = jobInstancePool.jobIndex[job];
    uint32 lastIndex = jobInstancePool.jobCount-1;
    if(index==lastIndex) {
        jobInstancePool.jobCount--;
        atomic_thread_fence(memory_order_release);
        avRWLockWriteUnlock(jobInstancePool.lock);
        return;
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
}

void getJobBatch(JobBatchID id, JobBatchDescription* batch){
    avRWLockReadLock(jobBatchPool.lock);
    uint32 index = jobBatchPool.jobBatchIndex[id];
    JobBatchDescription* target = &jobBatchPool.jobBatches[index].batch;
    avMemcpy(batch, target, sizeof(JobBatchDescription));
    avRWLockReadUnlock(jobBatchPool.lock);
}

void writeJobBatch(JobBatchID id, JobBatchDescription* batch){
    avRWLockReadLock(jobBatchPool.lock);
    uint32 index = jobBatchPool.jobBatchIndex[id];
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

struct batchSplit{uint32 count; uint32 remainingCount} splitJobBatch(JobID* jobs, uint32 maxJobs, JobBatchID batch, WorkerThreadID workerThreadId){
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
        jobs[i] = allocateJob(&jobBuffer);
    }
    jobBatch.inputData += jobBatch.inputStride * jobsToConsume;
    jobBatch.outputData += jobBatch.outputStride * jobsToConsume;
    jobBatch.size -= jobsToConsume;
    if(jobBatch.size==0){
        freeJobBatch(batch);
        return (struct batchSplit) {.count=jobsToConsume, .remainingCount = 0};
    }

    writeJobBatch(batch, &jobBatch);
    return (struct batchSplit) {.count=jobsToConsume, .remainingCount = jobBatch.size};
}

// the local queue needs to be topped up by the global queue(is empty)
JobID getJobFromGlobalQueue(JobQueue* globalQueue, LocalJobQueue* localQueue, JobPriority priority, WorkerThreadID workerThreadId){
    
    uint32 jobCount = 0;
    JobID jobs[LOCAL_QUEUE_RING_SIZE] = {JOB_NONE};

    while(jobCount < LOCAL_QUEUE_RING_SIZE){
        if(!avSemaphoreTryWait(state->priorityPresent[priority])){
            break; // none of this priority present
        }
        JobBatchID jobBatchId = jobQueuePull(globalQueue, priority);
        if(jobBatchId==JOB_NONE){
            break;
        }

        struct batchSplit split = splitJobBatch(jobs, LOCAL_QUEUE_RING_SIZE-jobCount, jobBatchId, workerThreadId);
        jobCount += split.count;
        if(split.remainingCount != 0){
            jobQueuePush(globalQueue, priority, jobBatchId);
        }else{
            if(jobCount != LOCAL_QUEUE_RING_SIZE){
                avFatal("Logic error in job counting, %u", jobCount);
            }
        }
    }

    if(jobCount == 0){
        return JOB_NONE;
    }
    if(jobCount > 1){
        if(!localJobQueuePushBatch(localQueue, priority, jobs + 1, jobCount - 1)){
            avFatal("Logic error in job counting"); // localQueue should be empty
        }
    }
    return jobs[0]; // we don't have to push the first job onto the queue as we can immediately use it
}

JobID stealJobFromOtherThread(WorkerThreadID threadId, uint32* stealIndex, JobPriority priority){
    JobID job = JOB_NONE;
    for(uint32 i = 0; i < state->threadCount; i++){
        uint32 index = ((*stealIndex) + i) % state->threadCount;
        if(index == threadId) continue;
        job = localJobQueueSteal(&state->threads[index].localQueue, JOB_PRIORITY_VERY_HIGH);
        if(job != JOB_NONE){
            *stealIndex = (index + 1) % state->threadCount;
            break;
        }
    }
    return job;
}

void threadWaitForWork(WorkerThreadID threadId){
    WorkerThread* thread = &state->threads[threadId];
    avMutexLock(thread->mutex);
    avConditionVariableWait(state->workPresent, thread->mutex);
    avMutexUnlock(thread->mutex);
}

bool32 pushbackJob(JobID job, struct JobData* jobData, LocalJobQueue localQueue, JobPriority priority){
    storeJobData(job, jobData);
    return localJobQueuePush(localQueue, priority, job);
}

bool32 pushJobToMainQueue(JobID job, JobInstance instance, JobQueue globalQueue, JobPriority priority){
    JobBatchDescription jobBatch = {
        // TODO: convert to jobBatch and push to mainQueue
    };
}

int32 workerThreadEntry(byte* data, uint64 size){
    WorkerThreadID threadId = (WorkerThreadID*) data;
    WorkerThread* thread = &state->threads[threadId];
    LocalJobQueue* localQueue = &thread->localQueue;
    JobQueue* globalQueue = &state->jobQueue;
    uint32 stealIndex = (threadId+1) % state->threadCount;
    bool32 running = true;
    threadWaitForWork(threadId);
    while(running){
        
        JobID job = JOB_NONE;
        JobPriority priority = JOB_PRIORITY_MAX;
        for(; priority <= JOB_PRIORITY_MAX; priority++){
            job = localJobQueuePull(localQueue, priority);
            if(job!=JOB_NONE) break;
            job = getJobFromGlobalQueue(globalQueue, localQueue, priority, thread->id);
            if(job!=JOB_NONE) break;
            job = stealJobFromOtherThread(threadId, &stealIndex, priority);
            if(job!=JOB_NONE) break;
        }

        if(job==JOB_NONE){
            // no work to be done
            threadWaitForWork(threadId);
            continue;
        }
executeJob:
        JobInstance instance;
        struct JobData jobData;
        getJobInstance(job, &instance, &jobData);
        jobData.context.state = jobData.state;
runJobSection:
        jobData.context.priority = priority;
        jobData.context.stateOffset = 0;
        jobData.context.shouldYield = jobData.context.priority!=JOB_PRIORITY_MAX; // highest priority should never yield as there is nothing that could interrupt it


        JobControl control = instance.entry(instance.input, instance.inputSize, instance.output, instance.outputSize, &jobData.context);

        switch(control.ret){
            default:
            case JOB_EXIT_NONE:
            case JOB_EXIT_STATE_OVERRUN:
                avFatal("error while running job");
                break;
            case JOB_EXIT_FAILURE:
                //TODO: handle success
                break;
            case JOB_EXIT_SUCCESS:
                //TODO: handle failure
                break;
            case JOB_YIELD:
                JobID higherPriorityJob = JOB_NONE;
                for(uint32 i = JOB_PRIORITY_MAX; i < priority; i++){
                    higherPriorityJob = getJobFromGlobalQueue(globalQueue, localQueue, i, threadId);
                    if(higherPriorityJob!=JOB_NONE) break;
                }
                if(higherPriorityJob!=JOB_NONE){
                    pushbackJob(job, &jobData, localQueue, priority); // we can ignore return as there is always room in the queue the job came from
                    job = higherPriorityJob;
                    goto executeJob;
                }
                jobData.context.section = control.nextSection;
                if(control.priorityHint > priority){ // the new priority is lower than the current priority
                    if(!pushbackJob(job, &jobData, localQueue, control.priorityHint)){
                        if(!pushJobToMainQueue(job, instance, globalQueue, control.priorityHint)){
                            avFatal("Ran out of room in main queue for priority %u", control.priorityHint);
                        }
                    }
                }else{
                    priority = control.priorityHint;
                    goto runJobSection;
                }
                break;
        }

    }
}



// Can only be submitted from the main thread, is used to submit work to the 
bool32 submitJobBatch(JobBatchDescription batch){
    if(avThreadGetID() != AV_MAIN_THREAD_ID) return;

    


}
