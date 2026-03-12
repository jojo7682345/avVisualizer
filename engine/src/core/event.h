#pragma once

#include "defines.h"

typedef struct EventContext {
    // 16 bytes
    union {
        void* ptr[2];
        /** @brief An array of 2 64-bit signed integers. */
        int64 i64[2];
        /** @brief An array of 2 64-bit unsigned integers. */
        uint64 u64[2];

        /** @brief An array of 2 64-bit floating-point numbers. */
        double f64[2];

        /** @brief An array of 4 32-bit signed integers. */
        int32 i32[4];
        /** @brief An array of 4 32-bit unsigned integers. */
        uint32 u32[4];
        /** @brief An array of 4 32-bit floating-point numbers. */
        float f32[4];

        /** @brief An array of 8 16-bit signed integers. */
        int16 i16[8];

        /** @brief An array of 8 16-bit unsigned integers. */
        uint16 u16[8];

        /** @brief An array of 16 8-bit signed integers. */
        int8 i8[16];
        /** @brief An array of 16 8-bit unsigned integers. */
        uint8 u8[16];

        /** @brief An array of 16 characters. */
        char c[16];
    } data;
} EventContext;

typedef int32 EventStageID;
typedef uint32 EventID;

typedef struct EventFlags {
    uint32 consumed : 1;
    uint32 modified : 1;
    uint32 hops : 4;
    uint32 reserved : 26;
} EventFlags;

#define EVENT_ID_INVALID ((EventID)-1)

typedef struct Event{
    EventID id;
    EventFlags flags;
    void* eventSource;
    EventContext context;
} Event;

typedef void (*EventStageFn) (Event* events, uint32 count);

#define EVENT_REGISTER_FAILED ((EventStageID)-1)
#define EVENT_REGISTER_FAILED_NO_SOURCE ((EventStageID)-2)
#define EVENT_REGISTER_FAILED_NO_SPACE ((EventStageID)-3)
#define EVENT_REGISTER_FAILED_NO_TARGET ((EventStageID)-4)
#define EVENT_REGISTER_FAILED_NO_BEFORE ((EventStageID)-5)
#define EVENT_REGISTER_FAILED_NO_AFTER ((EventStageID)-6)
#define EVENT_REGISTER_FAILED_WRONG_ORDER ((EventStageID)-7)

#define EVENT_STAGE_START ((EventStageID)-8)
#define EVENT_STAGE_END ((EventStageID)-9)

AV_API bool32 registerEventID(EventID id);

// event sink is placed at the end of the pipeline
AV_API EventStageID registerEventSink(EventID id, EventStageFn stage);

// event sink is placed after the target, is not garanteed to be directly after
AV_API EventStageID registerEventSinkAfter(EventID id, EventStageFn stage, EventStageID target);

// event sink is placed before the target, is not guaranteed to be directly before
AV_API EventStageID registerEventSinkBefore(EventID id, EventStageFn stage, EventStageID target);

// event sink is placed somewhere inbetween the two targets
AV_API EventStageID registerEventSinkBetween(EventID id, EventStageFn stage, EventStageID before, EventStageID after);

AV_API bool32 unregisterEventSink(EventID id, EventStageID target);

AV_API bool32 eventFire(Event event);
AV_API bool32 eventFireOverwrite(Event event);

bool32 eventsDispatch();

typedef struct EventSystemConfig {
    uint32 maxIDs;
} EventSystemConfig;
bool8 eventSystemInitialize(uint64* memory_requirement, void* state, void* config);
void eventSystemShutdown(void* state);

#define EVENT(eventCode, source, eventContext) (Event) {.id=eventCode, .eventSource=source, .context=eventContext}
