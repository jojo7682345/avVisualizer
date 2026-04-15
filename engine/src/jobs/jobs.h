#pragma once

#include "defines.h"
#include <stdatomic.h>

typedef uint32 JobID;
#define JOB_NONE ((JobID)-1)

typedef enum JobPriority {
    JOB_PRIORITY_MAX = 0,
    JOB_PRIORITY_VERY_HIGH = 0,
    JOB_PRIORITY_HIGH,
    JOB_PRIORITY_MEDIUM,
    JOB_PRIORITY_LOW,
    JOB_PRIORITY_VERY_LOW,


    JOB_PRIORITY_COUNT,
} JobPriority;

typedef struct JobBatchDescription {
    uint32 size;
    JobID id;
    JobPriority priority;
    byte* inputData;
    uint32 inputStride;
    byte* outputData;
    uint32 outputStride;
    uint32 stateSize;
    JobEntry entry;
    JobResultCallback onSuccess;
    JobResultCallback onFailure;
    uint32 dependencyCount;
    JobID* dependencies;
} JobBatchDescription;

typedef struct JobContext {
    uint32 section;
    uint32 subsection;
    JobPriority priority;
    _Atomic bool8 shouldYield;
    byte* state;
    uint32 stateOffset;
    uint32 stateSize;
} JobContext;

typedef enum JobControlReturn {
    JOB_EXIT_NONE,
    JOB_EXIT_SUCCESS,
    JOB_EXIT_FAILURE,
    JOB_YIELD,
    JOB_EXIT_STATE_OVERRUN,
} JobControlReturn;

typedef struct JobControl {
    JobControlReturn ret;
    uint32 nextSection;
    JobPriority priorityHint;
} JobControl;

// Job control
typedef JobControl (*JobEntry)(byte* input, uint32 inputSize, byte* output, uint32 outputSize, JobContext* context);
typedef void (*JobResultCallback)(byte* output, uint32 outputSize, JobContext* context);

typedef struct JobSystemConfig {
    uint32 maxWorkerThreads;
} JobSystemConfig;

#define JOB_STATE_SIZE 4096

#define JOB_START JobControl __job_control = {.ret=JOB_EXIT_NONE, }; switch(context->section){ case 0:{

#define JOB_SECTION(sectionNum) if(atomic_load_explicit(&context->shouldYield, memory_order_relaxed)) {__job_control.ret = JOB_YIELD;  __job_control.nextSection=context->section+1; break;} } case (sectionNum):{

#define JOB_END break; } default: __job_control.ret = JOB_EXIT_SUCCESS; break; } return __job_control;

#define JOB_EXIT_SUCCESS() do{ __job_control.ret = JOB_EXIT_SUCCESS; return __job_control; } while(0)
#define JOB_EXIT_FAILURE() do{ __job_control.ret = JOB_EXIT_FAILURE; return __job_control; } while(0)

#define JOB_LOCAL(type, name) type* name = (type*)context->state; context->state += sizeof(type); context->stateSize -= sizeof(type); if(context->state + sizeof(type) > context->stateSize) {return (JobControl){.ret=JOB_EXIT_STATE_OVERRUN}; }

#define JOB_LOCAL(type, name) \
    type* name = NULL; \
    do {\
        uint32 align = _Alignof(type);\
        uint32 offset = (context->stateOffset + (align - 1)) & ~(align - 1); \
        if(offset + sizeof(type) > context->stateSize) {\
            return (JobControl){.ret = JOB_EXIT_STATE_OVERRUN}; \
        } \
        name = (type*)(context->state + offset); \
        context->stateOffset = offset + sizeof(type); \
    } while(0)

#define JOB_LOOP(var, count) \
    while(1)\
        if(atomic_load_explicit(&context->shouldYield, memory_order_relaxed)){\
            __job_control.ret = JOB_YIELD; return __job_control;\
        }else if(context->subsection >= count) {\
            context->subsection = 0;\
            break;\
        }else for(uint32 var = context->subsection++, once = 1; once; once = 0)

#define JOB_WHILE(cond) \
        while(cond)\
            if(atomic_load_explicit(&context->shouldYield, memory_order_relaxed)){\
                __job_control.ret = JOB_YIELD; return __job_control;\
            }else

// JobControl exampleJob(byte* input, uint32 inputSize, byte* output, uint32 outputSize, JobContext context){
//     JOB_LOCAL(uint32, a);
//     // code that runs always
//     JOB_START
//     // section 0 code
//     JOB_SECTION(1)

//     JOB_LOOP(i, 10){

//     }

//     JOB_WHILE(*a < 10){

//     }

//     // section 1 code
//     JOB_SECTION(2)
//     // section 2 code
//     JOB_END
// }


