#pragma once
#include "jobs.h"

#define IO_DEFAULT_INLINE_BUFFER_SIZE (1024)*(256) // 256 KB
typedef struct IoSystemConfig {
    uint32 threadCount;
    uint32 concurrentRequests;
    uint32 inlineBufferSize;
} IoSystemConfig;

bool32 initializeIoSystem(uint64* memoryRequirements, void* statePtr, void* configPtr);
void deinitializeIoSystem(void* statePtr);

void ioSystemUpdate();

typedef enum IoResult {
    IO_SUCCESS,
    IO_FILE_ERROR,
    IO_FILE_NOT_FOUND,
    IO_TOO_MANY_FILES_OPEN,
    IO_FILE_ALREADY_EXISTS,
    IO_FILE_NO_ACCESS,
    IO_PROCESS_ERROR,
} IoResult;

typedef enum IoWriteFlag {
    IO_WRITE_FLAG_WRITE_NORMAL,
    IO_WRITE_FLAG_APPEND,
    IO_WRITE_FLAG_WRITE_NEW,
} IoWriteFlag;

typedef JobControl (*IoProcessFn)(const void* data, uint64 dataSize, void* ctx, JobContext* context);
typedef void (*IoCompleteFn)(IoResult result, const void* data, uint64 dataSize, void* ctx);

bool32 submitIoRead(const char* path, IoProcessFn process, IoCompleteFn onComplete, void* ctx);
bool32 submitIoReadPriority(const char* path, IoProcessFn process, IoCompleteFn onComplete, void* ctx, JobPriority processPriority);
bool32 submitIoWrite(const char* path, const void* data, uint64 dataSize, IoWriteFlag flags, IoCompleteFn onComplete, void* ctx);

bool32 submitIoReadSection(const char* path, uint64 offset, uint64 size, IoProcessFn process, IoCompleteFn onComplete, void* ctx);
bool32 submitIoReadSectionPriority(const char* path, uint64 offset, uint64 size, IoProcessFn process, IoCompleteFn onComplete, void* ctx, JobPriority processPriority);
bool32 submitIoWriteSection(const char* path, const void* data, uint64 dataSize, uint64 offset, IoCompleteFn onComplete, void* ctx);



// stream api will be specified here

