#include "../io.h"

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <AvUtils/avThreading.h>
#include <AvUtils/avMemory.h>
#include <AvUtils/avString.h>
#include <AvUtils/threading/avSemaphore.h>
#include <AvUtils/threading/avRwLock.h>

#include "containers/queues/mpmcQueue.h"
#define AV_LOG_CATEGORY "IO"
#include "logging.h"


static unsigned int nextPowerOf2(unsigned int n)
{
    unsigned int p = 1;
    if (n && !(n & (n - 1)))
        return n;

    while (p < n) 
        p <<= 1;
    
    return p;
}

enum IoMode {
    IO_MODE_READ,
    IO_MODE_WRITE,
    IO_MODE_APPEND,
    IO_MODE_WRITE_NEW,
};

typedef struct IoRequest {
    char path[1024];
    IoProcessFn process;
    IoCompleteFn onComplete;
    const void* data; 
    uint64 offset;
    uint64 size;
    enum IoMode mode;
    void* ctx;
    JobPriority processPriority;
    IoResult result;
    bool8 useFallback;
} IoRequest;

typedef struct IoRequestData {
    
    IoRequest request;
} IoRequestData;

typedef struct IoRequestPool{
    union{
        uint32 next;
        IoRequest request;
    }* freeList;
    _Atomic uint32 head;
} IoRequestPool;


typedef struct IoState {
    uint32 ioThreadCount;
    MpmcQueue requestQueue;
    uint32 concurrentRequestCount;

    AvThread* ioThreads;
    AvSemaphore workPresent;
    AvSemaphore threadsExited;
    _Atomic bool32 allowNewRequests;
    bool32 running;
    AvRwLock exitLock;
    void* queueMem;

    IoRequestPool requestPool;
    byte* inlineBuffers;
    uint32 inlineBufferSize;

    uint32* completeQueue;
    _Atomic bool8* completeQueueReady;
    _Atomic uint32 completeHead;
    uint32 completeTail;
} IoState;

static IoState* state;

static void submitComplete(uint32 index){

    uint32 idx = atomic_fetch_add_explicit(&state->completeHead, 1, memory_order_relaxed);
    uint32 slotIndex = idx % state->concurrentRequestCount;

    state->completeQueue[slotIndex] = index;
    atomic_store_explicit(&state->completeQueueReady[slotIndex], 1, memory_order_release);
}

static uint32 getNextComplete(){
    uint32 tail = state->completeTail  % state->concurrentRequestCount;
    if(!atomic_load_explicit(&state->completeQueueReady[tail], memory_order_acquire)){
        return (uint32) -1;
    }
    
    uint32 index = state->completeQueue[tail];

    atomic_store_explicit(&state->completeQueueReady[tail], 0, memory_order_relaxed);

    state->completeTail++;
    return index;
}


void initRequestPool(IoRequestPool* pool, uint32 size, IoRequest* backingArray){
    pool->freeList = (void*) backingArray;
    for(uint32 i = 0; i < size - 1; i++){
        pool->freeList[i].next = i+1;
    }
    pool->freeList[size - 1].next = (uint32)-1;
    atomic_store(&pool->head, 0);
}

uint32 requestPoolAllocate(IoRequestPool* pool){
    uint32 head;
    uint32 next;

    do {
        head = atomic_load_explicit(&pool->head, memory_order_acquire);

        if(head == (uint32)-1){
            return (uint32)-1;
        }

        next = pool->freeList[head].next;
    } while(!atomic_compare_exchange_weak_explicit(&pool->head, &head, next, memory_order_acq_rel, memory_order_acquire));
    return head;
}

void requestPoolFree(IoRequestPool* pool, uint32 index){
    uint32 head;
    do {
        head = atomic_load_explicit(&pool->head, memory_order_acquire);
        pool->freeList[index].next = head;
    } while(!atomic_compare_exchange_weak_explicit(&pool->head, &head, index, memory_order_acq_rel, memory_order_acquire));
}

static int32 ioThreadEntry(byte*,uint64 index);
bool32 initializeIoSystem(uint64* memoryRequirements, void* statePtr, void* configPtr){
    *memoryRequirements = sizeof(IoState);
    if(statePtr==NULL) return true;
    state = statePtr;
    
    IoSystemConfig* config = (IoSystemConfig*)configPtr;
    uint32 queueSize = nextPowerOf2(config->concurrentRequests);
    if(config->concurrentRequests < config->threadCount){
        config->concurrentRequests = config->threadCount;
        avWarn("Number of in-flight requests must be larger than the number of IO threads(%u)", config->threadCount);
    }
    if(config->concurrentRequests != queueSize){
        config->concurrentRequests = queueSize;
        avWarn("Number of in-flight requests must be power of 2, in-flight request now set at: %u", config->concurrentRequests);
    }
    if(config->concurrentRequests > 65536){
        avWarn("The maximum number of in-flight request is 65536");
        config->concurrentRequests = 65536;
    }

    state->ioThreadCount = config->threadCount;
    state->inlineBufferSize = config->inlineBufferSize;
    state->concurrentRequestCount = config->concurrentRequests;

    uint32 secSize = MPMC_QUEUE_SEQUENCE_BUFFER_SIZE(queueSize);

    
    uint32 queueMemSize = 
        secSize + 
        sizeof(uint32)*queueSize + 
        sizeof(AvThread)*state->ioThreadCount + 
        sizeof(IoRequest)*state->concurrentRequestCount + 
        state->inlineBufferSize * state->concurrentRequestCount +
        sizeof(uint32) * state->concurrentRequestCount +
        sizeof(bool8) * state->concurrentRequestCount;
    void* queueMem = avAllocate(queueMemSize, "");
    avMemset(queueMem, 0, queueMemSize);
    uint64 offset = 0;
    void* secMem = queueMem;
    offset += secSize;
    void* reqMemory = (byte*)queueMem + offset;
    offset += sizeof(uint32)*queueSize;
    state->ioThreads = (void*)((byte*)queueMem + offset);
    offset += + sizeof(AvThread)*state->ioThreadCount;
    void* requestMem = (byte*)queueMem + offset;
    offset += sizeof(IoRequest)*state->concurrentRequestCount;
    state->inlineBuffers = (byte*)queueMem + offset;
    offset += state->inlineBufferSize * state->concurrentRequestCount;
    state->completeQueue = (void*)((byte*)queueMem + offset);
    offset += sizeof(uint32) * state->concurrentRequestCount;
    state->completeQueueReady = (void*)((byte*)queueMem + offset);
    offset += sizeof(bool8) * state->concurrentRequestCount;


    state->queueMem = queueMem;
    mpmcInit(&state->requestQueue, queueSize, sizeof(uint32), reqMemory, secMem);
    initRequestPool(&state->requestPool, state->concurrentRequestCount, requestMem);

    avSemaphoreCreate(&state->workPresent, 0);
    avSemaphoreCreate(&state->threadsExited, 0);
    avRWLockCreate(&state->exitLock);
    
    atomic_store_explicit(&state->running, 1, memory_order_release);
    for(uint32 i = 0; i < state->ioThreadCount; i++){
        avThreadCreate(ioThreadEntry, &state->ioThreads[i]);
        char buffer[16];
        avStringPrintfToBuffer(buffer, sizeof(buffer), AV_CSTRA("IO thread %u"), i);
        avThreadSetName(state->ioThreads[i], buffer);
        avThreadStart(NULL, i, state->ioThreads[i]);
    }

    avInfo("number of IO threads: %u", state->ioThreadCount);
    avInfo("inline buffer size: %uKB", state->inlineBufferSize / 1024);
    avInfo("max in-flight requests: %u", state->concurrentRequestCount);
    avInfo("total reserved IO memory: %uMB", queueMemSize / (1024 * 1024));
}

void deinitializeIoSystem(void* statePtr){
    if(state==NULL) return;
    avRWLockWriteLock(state->exitLock);
    atomic_store_explicit(&state->running, 0, memory_order_release);
    for(uint32 i = 0; i < state->ioThreadCount; i++){
        avSemaphorePost(state->workPresent);
    }
    for(uint32 i = 0; i < state->ioThreadCount; i++){
        avSemaphoreWait(state->threadsExited);
    }
    for(uint32 i = 0; i < state->ioThreadCount; i++){
        avThreadDestroy(state->ioThreads[i]);
    }
    avRWLockWriteUnlock(state->exitLock);

    avSemaphoreDestroy(state->workPresent);
    avSemaphoreDestroy(state->threadsExited);
    avRWLockDestroy(state->exitLock);

    avFree(state->queueMem);
}

static JobControl ioProcess(byte* input, uint32 inputSize, byte* output, uint32 outputSize, JobContext* context){
    IoRequest* request = (IoRequest*)input;
    uint32 index = inputSize;

    JobControl ret = request->process(request->data, request->size, request->ctx, context);
    switch(ret.ret){
        default:
            return ret;
        case JOB_ERROR:
        case JOB_EXIT_UNDEFINED:
        case JOB_YIELD:
        case JOB_STATE_OVERRUN:
            request->result = IO_PROCESS_ERROR;
        case JOB_EXIT_NORMAL:
            submitComplete(index);
            break;
    }
    return ret;
} 

static void processRequest(IoRequest* request, uint32 index){
    request->result = IO_SUCCESS;
    // process io request
    int mode;
    switch(request->mode){
        case IO_MODE_READ:      mode = _O_BINARY | _O_RDONLY; break;
        case IO_MODE_WRITE:     mode = _O_BINARY | _O_WRONLY | _O_CREAT; break;
        case IO_MODE_APPEND:    mode = _O_BINARY | _O_WRONLY | _O_CREAT | _O_APPEND; break;
        case IO_MODE_WRITE_NEW: mode = _O_BINARY | _O_WRONLY | _O_CREAT | _O_EXCL; break;
        default: avError("invalid mode"); goto submitComplete;
    }

    int fd = open(request->path, mode);
    if(fd < 0){
        int error = errno;
        switch(error){
            case EACCES: request->result = IO_FILE_NO_ACCESS; break;
            case ENOENT: request->result = IO_FILE_NOT_FOUND; break;
            case EMFILE: request->result = IO_TOO_MANY_FILES_OPEN; break;
            case EEXIST: request->result = IO_FILE_ALREADY_EXISTS; break;
            default: request->result = IO_FILE_ERROR; break;
        }
        // skip to submit complete (sync)
        goto submitComplete;
    }
    if(request->offset!=0){
        if(lseek(fd, request->offset, SEEK_SET) < 0){
            request->result = IO_FILE_ERROR;
            close(fd);
            // skip to submit complete (sync)
            goto submitComplete;
        }
    }
    if(request->mode == IO_MODE_READ){
        if(request->size == 0){
            if(lseek(fd, 0, SEEK_END) < 0){
                request->result = IO_FILE_ERROR;
                close(fd);
                // skip to submit complete (sync)
                goto submitComplete;
            }
            uint64 size = tell(fd);
            request->size = size;
            if(lseek(fd, request->offset, SEEK_SET)){
                request->result = IO_FILE_ERROR;
                close(fd);
                // skip to submit complete (sync)
                goto submitComplete;
            }
        }
        if(request->size > state->inlineBufferSize){
            request->data = avAllocate(request->size, "IO read buffer fallback");
            request->useFallback = true;
        }else{ 
            request->data = state->inlineBuffers + state->inlineBufferSize * index;
            request->useFallback = false;
        }
        

        uint64 bytesToRead = request->size;
        byte* bytes = (void*) request->data;
        while(bytesToRead){
            int64 bytesRead = read(fd, bytes, bytesToRead);
            if(bytesToRead < 0){
                request->result = IO_FILE_ERROR;
                close(fd);
                //skip to submit complete
                goto submitComplete;
            }
            if(bytesRead == 0){
                request->size = bytesRead;
                break;
            }
            bytesToRead -= bytesRead;
            bytes += bytesRead;
        }
    }else {
        uint64 bytesToWrite = request->size;
        byte* bytes = (void*) request->data;
        
        while(bytesToWrite){
            int64 bytesWritten = write(fd, bytes, bytesToWrite);
            if(bytesWritten < 0){
                request->result = IO_FILE_ERROR;
                close(fd);
                // skip to submit complete
                goto submitComplete;
            }
            bytesToWrite -= bytesWritten;
            bytes += bytesWritten;
        }
        
    }
    close(fd);
    
    if(request->process){
        JobBatchDescription batch = {
            .size = 1,
            .entry = ioProcess,
            .flags.priority = request->processPriority,
            .flags.completeThisFrame = false,
            .inputData = request,
            .inputStride = index,
            .outputData = NULL,
            .outputStride = 0,
            .onComplete = NULL, // will be a custom submit to a specific IO queue
        };
        if(submitJobBatch(&batch, NULL)==JOB_BATCH_NONE){
            request->result = IO_PROCESS_ERROR;
            // skip to submit complete
            goto submitComplete;
        }
        return;
    }
submitComplete:
    if(request->onComplete){
        //submit complete
        submitComplete(index);
    }else{
        if(request->useFallback){
            avFree((void*)request->data);
        }
        requestPoolFree(&state->requestPool, index);
    }
}

static int32 ioThreadEntry(byte*,uint64 index){
    while(true){

        avSemaphoreWait(state->workPresent);
        uint32 index;
        if(!mpmcPop(&state->requestQueue, &index)){
            if(!atomic_load_explicit(&state->running, memory_order_relaxed)){
                break;
            }
        } 
        processRequest((IoRequest*)&state->requestPool.freeList[index], index);
    }
    avSemaphorePost(state->threadsExited);
    return 0;
}




static bool32 submitRequest(uint32 request){
    avRWLockReadLock(state->exitLock);
    if(!atomic_load_explicit(&state->running, memory_order_acquire)){
        avRWLockReadUnlock(state->exitLock);
        return false;
    }
    if(!mpmcPush(&state->requestQueue, &request)){
        avRWLockReadUnlock(state->exitLock);
        return false;
    }
    avSemaphorePost(state->workPresent);
    avRWLockReadUnlock(state->exitLock);
    return true;    
}

static IoRequest* prepareIoRequest(uint32* index, const char* path, void* ctx){
    uint64 pathLength = avCStringLength(path);
    if(pathLength > sizeof((IoRequest){0}.path)-1){
        return NULL;
    }
    uint32 idx = requestPoolAllocate(&state->requestPool);
    if(idx == (uint32)-1){
        return NULL;
    }
    *index = idx;
    IoRequest* request = (IoRequest*)&state->requestPool.freeList[idx].request;
    avMemcpy(request->path, path, pathLength);
    request->path[pathLength] = 0;
    request->ctx = ctx;
    return request;
}

bool32 submitIoRead(const char* path, IoProcessFn process, IoCompleteFn onComplete, void* ctx){
    uint32 index;
    IoRequest* request = prepareIoRequest(&index, path, ctx);
    if(request==NULL) return false;
    request->mode = IO_MODE_READ;
    request->process = process;
    request->onComplete = onComplete;
    return submitRequest(index);
}
bool32 submitIoWrite(const char* path, const void* data, uint64 dataSize, IoWriteFlag flags, IoCompleteFn onComplete, void* ctx){
    uint32 index;
    IoRequest* request = prepareIoRequest(&index, path, ctx);
    if(request==NULL) return false;
    request->data = data;
    request->size = dataSize;
    switch(flags){
        case IO_WRITE_FLAG_WRITE_NORMAL: request->mode = IO_MODE_WRITE; break;
        case IO_WRITE_FLAG_APPEND: request->mode = IO_MODE_APPEND; break;
        case IO_WRITE_FLAG_WRITE_NEW: request->mode = IO_MODE_WRITE_NEW;
        default: avError("Invalid IO write flag"); return false;
    }
    request->onComplete = onComplete;
    return submitRequest(index);
}

bool32 submitIoReadSection(const char* path, uint64 offset, uint64 size, IoProcessFn process, IoCompleteFn onComplete, void* ctx);
bool32 submitIoWriteSection(const char* path, const void* data, uint64 dataSize, uint64 offset, IoCompleteFn onComplete, void* ctx);

void ioSystemUpdate(){
    uint32 index = (uint32)-1;
    while((index = getNextComplete()) != (uint32)-1){

        IoRequest* request = &state->requestPool.freeList[index].request;
        
        request->onComplete(request->result, request->data, request->size, request->ctx);

        if(request->useFallback){
            avFree((void*)request->data);
        }
        requestPoolFree(&state->requestPool, index);
    }

}