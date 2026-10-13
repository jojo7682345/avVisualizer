#include "../window.h"
#include "core/utils/math.h"
#include "containers/idMapping.h"
#include "core/platform.h"
#include <AvUtils/avMemory.h>
#include "core/systems/event.h"
#include <AvUtils/avThreading.h>

#define AV_LOG_CATEGORY "window system"
#include "engine.h"
#include "logging.h"

typedef struct WindowState {
    IdMapping(Window) windows;
    void (*windowRequestCloseHandler) (WindowID window);
    void (*allwindowsClosedHandler) ();
} WindowState;
static WindowState* state = NULL;

static void windowRequestCloseEvent(Event* events, uint32 count){

    for(uint32 i = 0; i < count; i++){
        Event event = events[i];
        WindowID window = event.context.data.u32[0];
        if(MAPPING_GET(state->windows, window)==NULL){
            continue;
        }
        state->windowRequestCloseHandler(window);
        //event.flags.consumed = true;
    }

}

static void allWindowsClosedEvent(Event* events, uint32 count){
    (void)events;
    if(count != 1){
        avWarn("Received multiple all windows closed events, this shoundn't happen");
    }
    state->allwindowsClosedHandler();
}

static void windowRequestCloseHandler(WindowID window){
    windowDestroy(window);
}

static void allwindowsClosedHandler(){
    Event event = {
        .id = EVENT_CODE_APPLICATION_QUIT, 
        .eventSource = state,
    };
    eventFire(event);
}

bool8 initializeWindowSystem(uint64* memoryRequirements, void* statePtr, void* configPtr){
    if(configPtr==NULL) return false;

    WindowSystemConfig* config = (WindowSystemConfig*) configPtr;
    uint32 maxWindows = (config->maxWindows==0) ? 1 : config->maxWindows;

    *memoryRequirements = sizeof(WindowState) + _Alignof(WindowState);
    if(statePtr==NULL) return true;

    avInfo("Initializing window system");
    state = ( WindowState*)alignPtr(statePtr, _Alignof(WindowState));
    avMemset(state, 0, sizeof(WindowState));
    MAPPING_CREATE(state->windows, maxWindows);

    if(config->windowRequestCloseHandlerOverride){
        state->windowRequestCloseHandler = config->windowRequestCloseHandlerOverride;
    }else{
        state->windowRequestCloseHandler = windowRequestCloseHandler;
    }
    if(config->allwindowsClosedHandlerOverride){
        state->allwindowsClosedHandler = config->allwindowsClosedHandlerOverride;
    }else{
        state->allwindowsClosedHandler = allwindowsClosedHandler;
    }

    registerEventSink(EVENT_CODE_WINDOW_REQUEST_CLOSE, windowRequestCloseEvent);
    registerEventSink(EVENT_CODE_ALL_WINDOWS_CLOSED, allWindowsClosedEvent);
    return true;
}
void shutdownWindowSystem(void* statePtr){
    (void)statePtr;
    if(state==NULL) return;
    avInfo("Shutting down window system");
    uint32 remainingWindows = MAPPING_SIZE(state->windows);
    for(uint32 i = 0; i < remainingWindows; i++){
        platformDestroyWindow(state->windows[i]);
    }
    MAPPING_DESTROY(state->windows);
    state = NULL;
}

WindowID windowCreate(uint32 x, uint32 y, uint32 width, uint32 height, const char* title){
    AvThreadID threadId = avThreadGetID();
    if(threadId!=AV_MAIN_THREAD_ID){
        avError("Only the main thread is allowed to create windows");
        return MAPPING_INVALID_ID;
    }
    Window* tmp = NULL;
    uint64 windowID = MAPPING_ADD(state->windows, tmp);
    if(windowID==MAPPING_INVALID_ID) {
        avError("Created too many windows");
        return MAPPING_INVALID_ID;
    }
    WindowConfig config = {
        .ctx = (void*)windowID,
        .width = width,
        .height = height,
        .x = x,
        .y = y,
        .title = title,
        .flags = {
            .hidden = false,
            .dummyContent = true, // TODO: change
        },
    };

    Window window = platformCreateWindow(config);
    if(window == NULL){
        avError("Unable to open window");
        MAPPING_REMOVE(window, windowID);
        return MAPPING_INVALID_ID;
    }
    *MAPPING_GET(state->windows, windowID) = window;
    return windowID;

}
void windowRequestClose(WindowID window){
    EventContext context = {
        .data.u32[0] = window,
    };
    eventFire(EVENT(EVENT_CODE_WINDOW_REQUEST_CLOSE, state, context));
}

void windowDestroy(WindowID window){
    AvThreadID threadId = avThreadGetID();
    if(threadId!=AV_MAIN_THREAD_ID){
        avError("Only the main thread is allowed to destroy windows");
        return;
    }
    Window* windowPtr = MAPPING_GET(state->windows, window);
    if(windowPtr == NULL) {
        avWarn("Tried to destroy invalid window %u", window);
        return;
    }
    platformDestroyWindow(*windowPtr);
    MAPPING_REMOVE(state->windows, window);
    eventFire(EVENT(EVENT_CODE_WINDOW_CLOSED, state, {.data.u32[0]=window}));

    uint32 windowCount = MAPPING_SIZE(state->windows);
    if(windowCount == 0){
        Event event = {.id = EVENT_CODE_ALL_WINDOWS_CLOSED, .eventSource=state};
        eventFire(event);
    }
}

