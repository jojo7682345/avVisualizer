#include "logging.h"

#include <stdatomic.h>
#include <AvUtils/avThreading.h>
#include <AvUtils/threading/avConditionVariable.h>
#include <AvUtils/avMemory.h>
#include <AvUtils/avString.h>

#include "core/platform/platform.h"

typedef struct AvLogQueue {
    AvLogMessage* buffer;
    uint32 size;

    // producer side (atomic)
    _Atomic uint32 writeIndex;

    // consumer side (single thread)
    uint32 readIndex;
} AvLogQueue;

typedef struct AvLogger {
    AvLogQueue queue;

    AvLogSink sinks[AV_MAX_SINKS];
    uint32 sinkCount;

    AvLogLevel level;
    AvAssertLevel assertLevel;

    bool8 running;
    bool8 initialized;

    AvThread logThread;
    AvMutex condMutex;
    AvConditionVariable cond;
} AvLogger;

static AvLogger logger;

#define ANSI_COLOR_RED		"\x1b[31m"
#define ANSI_COLOR_GREEN	"\x1b[32m"
#define ANSI_COLOR_YELLOW	"\x1b[33m"
#define ANSI_COLOR_BLUE		"\x1b[34m"
#define ANSI_COLOR_MAGENTA	"\x1b[35m"
#define ANSI_COLOR_CYAN		"\x1b[36m"
#define ANSI_COLOR_WHITE	"\x1b[36m"
#define ANSI_COLOR_RESET	"\x1b[00m"
#define COLOR "%s"

const char* AV_COLOR_RED =      "";
const char* AV_COLOR_GREEN =    "";
const char* AV_COLOR_YELLOW =   "";
const char* AV_COLOR_BLUE =     "";
const char* AV_COLOR_MAGENTA =  "";
const char* AV_COLOR_CYAN =     "";
const char* AV_COLOR_WHITE =    "";
const char* AV_COLOR_RESET =    "";

static bool8 queueInit(AvLogQueue* queue, uint32 size){
    queue->buffer = (AvLogMessage*)avCallocate(size, sizeof(AvLogMessage), "");
    queue->size = size;
    atomic_store_explicit(&queue->writeIndex, 0, memory_order_release);
    queue->readIndex = 0;
    return true;
}

static bool8 queuePush(AvLogQueue* queue, const AvLogMessage* msg){
    uint32 write = atomic_fetch_add_explicit(&queue->writeIndex, 1, memory_order_relaxed);
    uint32 index = write & (queue->size - 1);

    queue->buffer[index] = *msg;
    return true;
}

static bool32 queuePop(AvLogQueue* queue, AvLogMessage* out){
    uint32 read = queue->readIndex;
    uint32 write = atomic_load_explicit(&queue->writeIndex, memory_order_acquire);

    if(read == write) return false;

    uint32 index = read & (queue->size -1);
    *out = queue->buffer[index];
    queue->readIndex++;
    return true;
}

static void dispatch(const AvLogMessage* msg){
    for(uint32 i = 0; i < logger.sinkCount; i++){
        logger.sinks[i].fn(msg, logger.sinks[i].userData);
    }
}

static int32 logThreadMain(byte* data, uint64 dataSize){
    (void) data;
    (void) dataSize;

    AvLogMessage msg;
    while(logger.running){
        bool32 hadWork = false;
        while(queuePop(&logger.queue, &msg)){
            dispatch(&msg);
            hadWork = true;
        }
        if(!hadWork){
            avMutexLock(logger.condMutex);
            avConditionVariableWait(logger.cond, logger.condMutex);
            avMutexUnlock(logger.condMutex);
        }
    }

    while(queuePop(&logger.queue, &msg)){
        dispatch(&msg);
    }

    return 0;
}

bool8 avLogInit(const AvLogConfig* config){
    if(logger.initialized) return true;
    if((config->queueSize & (config->queueSize-1)) != 0){
        return false;
    }

    if(config->useColors){
        AV_COLOR_RED = ANSI_COLOR_RED;
		AV_COLOR_GREEN = ANSI_COLOR_GREEN;
		AV_COLOR_YELLOW = ANSI_COLOR_YELLOW;
		AV_COLOR_BLUE = ANSI_COLOR_BLUE;
		AV_COLOR_MAGENTA = ANSI_COLOR_MAGENTA;
		AV_COLOR_CYAN = ANSI_COLOR_CYAN;
		AV_COLOR_CYAN = ANSI_COLOR_WHITE;
		AV_COLOR_RESET = ANSI_COLOR_RESET;
    }else{
        AV_COLOR_RED = "";
		AV_COLOR_GREEN = "";
		AV_COLOR_YELLOW = "";
		AV_COLOR_BLUE = "";
		AV_COLOR_MAGENTA = "";
		AV_COLOR_CYAN = "";
		AV_COLOR_WHITE = "";
		AV_COLOR_RESET = "";
    }

    avMemset(&logger, 0, sizeof(AvLogger));

    if(!queueInit(&logger.queue, config->queueSize)){
        return false;
    }

    logger.level = AV_LOG_LEVEL_ALL,
    logger.assertLevel = AV_ASSERT_LEVEL_NORMAL,
    logger.running = true;
    logger.initialized = true;

    avMutexCreate(&logger.condMutex);
    avConditionVariableCreate(&logger.cond);

    avThreadCreate(logThreadMain, &logger.logThread);
    avThreadStart(NULL, 0, logger.logThread);
    return true;
}

void avLogShutdown(){
    if(!logger.initialized) return;
    logger.running = false;

    avMutexLock(logger.condMutex);
    avConditionVariableSignal(logger.cond);
    avMutexUnlock(logger.condMutex);

    avThreadJoin(logger.logThread);

    if(logger.queue.buffer){
        avFree(logger.queue.buffer);
    }

    avThreadDestroy(logger.logThread);
    logger.initialized = false;
}

bool8 avLogAddSink(AvLogSinkFn fn, void* userData){
    if(logger.sinkCount >= AV_MAX_SINKS) return false;
    logger.sinks[logger.sinkCount++] = (AvLogSink){
        .fn = fn,
        .userData = userData,
    };
    return true;
}

#if defined(__has_builtin) && !defined(__ibmxl__)
#	if __has_builtin(__builtin_debugtrap)
#		define debug_break() __builtin_debugtrap()
#	elif __has_builtin(__debugbreak)
#		define debug_break() __debugbreak()
#	endif
#endif

// If not setup, try the old way.
#if !defined(debug_break)
#	if defined(__clang__) || defined(__gcc__)
/** @brief Causes a debug breakpoint to be hit. */
#		define debug_break() __builtin_trap()
#	elif defined(_MSC_VER)
#		include <intrin.h>
/** @brief Causes a debug breakpoint to be hit. */
#		define debug_break() __debugbreak()
#	else
// Fall back to x86/x86_64
#		define debug_break()  __asm__ volatile (" int3 ")
#	endif
#endif

#include <stdlib.h>

static void logInternal(AvResult result, uint64 line, const char* file, const char* func, const char* category, const char* fmt, va_list args){
    if(!logger.initialized) return;

    AvLogMessage msg;
    avMemset(&msg, 0, sizeof(msg));
    msg.result = result;
    msg.line = line;
    msg.file = file;
    msg.category = category;
    msg.timestamp = platformGetAbsoluteTime();
    msg.threadId = avThreadGetID();

    avStringPrintfToBufferVA(msg.text, sizeof(msg.text), AV_CSTR(fmt), args);

    queuePush(&logger.queue, &msg);

    avMutexLock(logger.condMutex);
    avConditionVariableSignal(logger.cond);
    avMutexUnlock(logger.condMutex);

    if(result >= logger.assertLevel){
        debug_break();
        avLogShutdown();
        abort();
    }
}

void avLogV(AvResult result, uint64 line, const char* file, const char* func, const char* category, const char* fmt, va_list args){
    logInternal(result, line, file, func, category, fmt, args);
}

void avLogEx(AvResult result, uint64 line, const char* file, const char* func, const char* category, const char* fmt, ...){
    va_list args;
    va_start(args, fmt);
    logInternal(result, line, file, func, category, fmt, args);
    va_end(args);
}

void avAssertEx(bool32 assert, const char* expression, uint64 line,  const char* file, const char* func, const char* category, const char* fmt, ...){
    (void)expression;
    if(!assert) return;
    va_list args;
    va_start(args, fmt);
    logInternal(assert, line, file, func, category, fmt, args);
    va_end(args);
}

#include <stdio.h>

void avLogConsoleSink(const AvLogMessage* msg, void* userData) {
    (void)userData;



    const char* level =
        (msg->result & AV_ERROR) ? "ERROR" :
        (msg->result & AV_WARNING) ? "WARN" :
        (msg->result & AV_INFO) ? "INFO" :
        (msg->result & AV_DEBUG) ? "DEBUG" : "LOG";

    const char* color =
        (msg->result & AV_ERROR) ? AV_COLOR_RED :
        (msg->result & AV_WARNING) ? AV_COLOR_YELLOW :
        (msg->result & AV_INFO) ? AV_COLOR_WHITE :
        (msg->result & AV_DEBUG) ? AV_COLOR_BLUE : AV_COLOR_GREEN;

    printf("["COLOR"%s"COLOR"] %g [%s] %s\n",
        color,
        level,
        AV_COLOR_RESET,
        msg->timestamp,
        msg->category,
        msg->text
    );
}

void avLogFileSink(const AvLogMessage* msg, void* userData) {
    FILE* f = (FILE*)userData;
    if (!f) return;

    fprintf(f, "[%llu] [%s:%llu] %s\n",
        (unsigned long long)msg->timestamp,
        msg->file,
        (unsigned long long)msg->line,
        msg->text
    );

    fflush(f);
}

void avLogVulkan(
    const char* renderer,
    AvValidationLevel type,
    AvLogLevel level,
    const char* fmt,
    ...
) {
    (void)type;

    va_list args;
    va_start(args, fmt);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    va_end(args);

    avLogEx(
        AV_DEBUG,
        0,
        __FILE__,
        __func__,
        renderer,
        "%s",
        buffer
    );
}