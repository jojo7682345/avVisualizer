#pragma once
#include <AvUtils/avTypes.h>

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AvResult {
    AV_SUCCESS =    0x00000000 | 0,
    AV_TEST_SUCCESS = AV_SUCCESS | 1,

    AV_DEBUG =      0x000F0000 | 0,
    AV_DEBUG_SUCCESS = AV_DEBUG | 1,
	AV_DEBUG_CREATE = AV_DEBUG | 2,
	AV_DEBUG_DESTROY = AV_DEBUG | 3,
	AV_DEBUG_INFO = AV_DEBUG | 4,
	AV_VALIDATION_PRESENT = AV_DEBUG | 5,
	AV_TEST_DEBUG = AV_DEBUG | 6,
	AV_SHUTDOWN_REQUESTED = AV_DEBUG | 7,
	AV_SWAPCHAIN_RECREATION = AV_DEBUG | 8,
	AV_WINDOW_SIZE = AV_DEBUG | 9,

    AV_INFO =       0x00F00000 | 0,
    AV_TEST_INFO = AV_INFO | 1,
	AV_TIME = AV_INFO | 2,

    AV_WARNING =    0x0F000000 | 0,
    AV_OUT_OF_BOUNDS = AV_WARNING | 1,
	AV_VALIDATION_NOT_PRESENT = AV_WARNING | 2,
	AV_UNSPECIFIED_CALLBACK = AV_WARNING | 3,
	AV_UNUSUAL_ARGUMENTS = AV_WARNING | 4,
	AV_TEST_WARNING = AV_WARNING | 5,
	AV_TIMEOUT = AV_WARNING | 6,
	AV_INVALID_SYNTAX = AV_WARNING | 7,
	AV_UNABLE_TO_PARSE = AV_WARNING | 8,

    AV_ERROR =      0xF0000000 | 0,
    AV_NO_SUPPORT = AV_ERROR | 1,
	AV_INVALID_ARGUMENTS = AV_ERROR | 2,
	AV_TIMED_OUT = AV_ERROR | 3,
	AV_MEMORY_ERROR = AV_ERROR | 4,
	AV_CREATION_ERROR = AV_ERROR | 5,
	AV_TEST_ERROR = AV_ERROR | 6,
	AV_IO_ERROR = AV_ERROR | 7,
	AV_NOT_FOUND = AV_ERROR | 8,
	AV_NOT_IMPLEMENTED = AV_ERROR | 9,
	AV_NOT_INITIALIZED = AV_ERROR | 10,
	AV_ALREADY_INITIALIZED = AV_ERROR | 11,
	AV_ALREADY_EXISTS = AV_ERROR | 12,
	AV_PARSE_ERROR = AV_ERROR | 13,
	AV_RENDER_COMMAND_ERROR = AV_ERROR | 14,
	AV_RENDER_ERROR = AV_ERROR | 15,
	AV_PRESENT_ERROR = AV_ERROR | 16,
	AV_SWAPCHAIN_ERROR = AV_ERROR | 17,
	AV_INVALID_SIZE = AV_ERROR | 18,
	AV_DEVICE_MISMATCH = AV_ERROR | 19,

    AV_FATAL =      0xFFFF0000 | 0,
} AvResult;
#define AV_RESULT_LEVEL_MASK 0xF0000000

typedef enum AvLogLevel {
    AV_LOG_LEVEL_ALL     = AV_SUCCESS,
    AV_LOG_LEVEL_DEBUG   = AV_DEBUG,
    AV_LOG_LEVEL_INFO    = AV_INFO,
    AV_LOG_LEVEL_WARNING = AV_WARNING,
    AV_LOG_LEVEL_ERROR   = AV_ERROR,
    AV_LOG_LEVEL_NONE    = 0xFFFFFFFF
} AvLogLevel;

typedef enum AvAssertLevel {
    AV_ASSERT_LEVEL_NORMAL = AV_FATAL,
    AV_ASSERT_LEVEL_ERROR   = AV_ERROR,
    AV_ASSERT_LEVEL_PEDANTIC = AV_WARNING,
} AvAssertLevel;

typedef struct AvLogMessage{
    AvResult result;
    const char* file;
    const char* func;
    const char* category;
    uint64 line;
    uint32 threadId;
    double timestamp;
    char text[1024];
} AvLogMessage;

extern const char* AV_COLOR_RED;
extern const char* AV_COLOR_GREEN;
extern const char* AV_COLOR_YELLOW;
extern const char* AV_COLOR_BLUE;
extern const char* AV_COLOR_MAGENTA;
extern const char* AV_COLOR_CYAN;
extern const char* AV_COLOR_RESET;

typedef void (*AvLogSinkFn)(const AvLogMessage* msg, void* userData);

typedef struct AvLogSink {
    AvLogSinkFn fn;
    void* userData;
} AvLogSink;
#define AV_MAX_SINKS 8

typedef struct AvLogConfig {
    AvLogLevel level;
    AvAssertLevel assertLevel;

    uint32 queueSize;
    bool32 printTime;
    bool32 printFile;
    bool32 printLine;
    bool32 printFunc;
    bool32 printCategory;
    bool32 printLevel;
    bool32 useColors;
} AvLogConfig;

bool8 avLogInit(const AvLogConfig* config);
void avLogShutdown();

bool8 avLogAddSink(AvLogSinkFn fn, void* userData);
void avLogRemoveAllSinks();

void avLogEx(AvResult result, uint64 line, const char* file, const char* func, const char* category, const char* fmt, ...);
void avLogV(AvResult result, uint64 line, const char* file, const char* func, const char* category, const char* fmt, va_list args);
void avAssertEx(bool32 assert, const char* expression, uint64 line,  const char* file, const char* func, const char* category, const char* fmt, ...);

#ifndef AV_LOG_CATEGORY
#define AV_LOG_CATEGORY "default"
#endif

#define AV_LOCATION_PARAMS __LINE__, __FILE__, __func__

#ifndef NDEBUG
#define avLog(result, fmt, ...) \
    avLogEx(result, AV_LOCATION_PARAMS, AV_LOG_CATEGORY, fmt __VA_OPT__(,) __VA_ARGS__)
#define avAssert(result, fmt, ...) \
    avAssertEx((result)!=0, #result, AV_LOCATION_PARAMS, AV_LOG_CATEGORY, fmt __VA_OPT__(,) __VA_ARGS__)
#define avValidate(result, fmt, ...)\
    avAssertEx((result)==AV_SUCCESS, #result, AV_LOCATION_PARAMS, AV_LOG_CATEGORY, fmt __VA_OPT__(,) __VA_ARGS__)
#define avDebug(fmt, ...) \
    avLog(AV_DEBUG, fmt __VA_OPT__(,) __VA_ARGS__)
#define avInfo(fmt, ...) \
    avLog(AV_INFO, fmt __VA_OPT__(,) __VA_ARGS__)
#define avWarn(fmt, ...) \
    avLog(AV_INFO, fmt __VA_OPT__(,) __VA_ARGS__)
#define avError(fmt, ...) \
    avLog(AV_ERROR, fmt __VA_OPT__(,) __VA_ARGS__)
#define avFatal(fmt, ...) \
    avLog(AV_FATAL, fmt __VA_OPT__(,) __VA_ARGS__)
#else
#define avLog(result, fmt, ...)         ((void)0)
#define avAssert(result, fmt, ...)      ((void)0)
#define avValidate(result, fmt, ...)    ((void)0)
#define avDebug(fmt, ...)               ((void)0)
#define avInfo(fmt, ...)                ((void)0)
#define avWarn(fmt, ...)                ((void)0)
#define avError(fmt, ...)               ((void)0)
#define avFatal(fmt, ...)               ((void)0)
#endif

void avLogConsoleSink(const AvLogMessage* msg, void* userData);
void avLogFileSink(const AvLogMessage* msg, void* userData);


typedef enum ValidationMessageType {
    VALIDATION_MESSAGE_TYPE_GENERAL,
    VALIDATION_MESSAGE_TYPE_VALIDATION,
    VALIDATION_MESSAGE_TYPE_PERFORMANCE,
} ValidationMessageType;

typedef enum AvValidationLevel{
    AV_VALIDATION_LEVEL_VERBOSE = 0,
	AV_VALIDATION_LEVEL_INFO = 1,
	AV_VALIDATION_LEVEL_WARNINGS_AND_ERRORS = 2,
	AV_VALIDATION_LEVEL_ERRORS = 3,
} AvValidationLevel;

void avLogVulkan(const char* renderer, AvValidationLevel type, AvLogLevel level, const char* fmt, ...);

#ifdef __cplusplus
}
#endif