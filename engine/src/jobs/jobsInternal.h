#pragma once

#include <stdatomic.h>
#include "jobBatchPool.h"
#include "jobQueue.h"
#include <AvUtils/avThreading.h>
#include <AvUtils/threading/avSemaphore.h>
#include <AvUtils/threading/avConditionVariable.h>
#include <AvUtils/avMemory.h>
#include <AvUtils/threading/avRwLock.h>
#include <AvUtils/avMath.h>
#include <AvUtils/avString.h>

#define LOCAL_QUEUE_RING_SIZE 1024
#define LOCAL_QUEUE_SIZE LOCAL_QUEUE_RING_SIZE*JOB_PRIORITY_COUNT
#define GLOBAL_QUEUE_RING_SIZE 4096
#define GLOBAL_QUEUE_SIZE GLOBAL_QUEUE_RING_SIZE*JOB_PRIORITY_COUNT
#define MAX_CORE_COUNT 32
#define JOB_INSTANCE_POOL_SIZE GLOBAL_QUEUE_SIZE + LOCAL_QUEUE_SIZE * MAX_CORE_COUNT / 2

typedef struct JobInstance {
    JobBatchID batch;
    byte* input;
    byte* output;
    uint32 inputSize;
    uint32 outputSize;
    //JobPriority priority;
    //JobPriority effectivePriority; // priority inheritance
    //JobEntry entry; // take this from the batchId
    //JobResultCallback onSuccess;
    //JobResultCallback onFailure;
    //byte* runState;
    //uint32 stateSize;
} JobInstance;

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



typedef struct WorkerThread {
    AvThread thread;
    LocalJobQueue localQueue;
    uint32 id;
    // keep batch around that we have already split of some jobs, so that we dont have to push it to the back of the queue again
    // threads that try to steal, first try to steal the batch instead of from the local queue to prevent congestion
    _Atomic JobBatchID workingOnBatch[JOB_PRIORITY_COUNT]; 
} WorkerThread;

struct JobFence {
    _Atomic uint32 workLeft;
};

typedef struct JobSystemState {
    WorkerThread* threads;
    uint32 threadCount;
    JobQueue jobQueue;
    AvSemaphore workPresent;
    //AvSemaphore priorityPresent[JOB_PRIORITY_COUNT];
    bool8 awaitingExit;
    _Atomic uint32 activeThreads;
    _Atomic JobID exchange[JOB_PRIORITY_COUNT];
    struct JobFence allWorkDoneFence;
} JobSystemState;




extern JobSystemState* state;

JobBatchID submitToMainQueue(JobPriority priority, JobBatchID id);