#include "engine.h"

#include <AvUtils/avLogging.h>
#include <AvUtils/avMemory.h>

#include "platform.h"
#include "core/systems/event.h"
#include "core/systems/input.h"
#include "core/systems/io.h"
#include "core/systems/jobs.h"
#include "core/utils/clock.h"
#include "renderer/renderer.h"

typedef struct EngineState {
    EngineConfig config;
    bool8 is_running;
    bool8 is_suspended;
    int16 width;
    int16 height;
    Clock clock;
    double last_time;
    uint64 frameIndex;

    void* systemsMemory;

    // Indicates if the window is currently being resized.
    //b8 resizing;
    // The current number of frames since the last resize operation.
    // Only set if resizing = true. Otherwise 0.
    //u8 frames_since_resize;
} EngineState;

static EngineState* engineState;

typedef struct EngineSystem {
    void* state;
    bool8 (*initialize)(uint64* memory_requirement, void* statePtr, void* configPtr);
    void (*uninitialize)(void* state);
    void* config;
} EngineSystem;


bool8 engineInitialize(EngineConfig* config) {
    if(engineState != NULL){
        avError("engine already initalized");
        return false;
    }
    engineState = avAllocate(sizeof(EngineState), "");
    engineState->config = *config;
    engineState->is_running = false;
    engineState->is_suspended = false;

    AvLogConfig logConfig = {0};
    logConfig.queueSize = 4096;
    logConfig.assertLevel = AV_ASSERT_LEVEL_NORMAL;
    logConfig.level = AV_LOG_LEVEL_INFO;
    logConfig.useColors = true;
    avLogInit(&logConfig);
    avLogAddSink(avLogConsoleSink, NULL);
    //if(engineState->game_inst->logSettings) setLogSettings(*engineState->game_inst->logSettings);
    return true;
}

static PlatformConfig platformConfig;
static EventSystemConfig eventConfig;
static RendererConfig rendererConfig;
static JobSystemConfig jobsystemConfig;
static IoSystemConfig ioSystemConfig;
EngineSystem engineSystems[] = {
    {.initialize=eventSystemInitialize,     .uninitialize=eventSystemShutdown,      .config=&eventConfig        },
    {.initialize=inputSystemInitialize,     .uninitialize=inputSystemShutdown,      .config=NULL                }, 
    {.initialize=platformSystemStartup,     .uninitialize=platformSystemShutdown,   .config=&platformConfig     },
    //{.initialize=rendererStartup,           .uninitialize=rendererShutdown,         .config=&rendererConfig     },
    {.initialize=jobSystemInitialize,       .uninitialize=jobSystemDeinitialize,    .config=&jobsystemConfig    },
    {.initialize=initializeIoSystem,        .uninitialize=deinitializeIoSystem,     .config=&ioSystemConfig     },
};

bool8 systemsInitialize(EngineConfig* config){

    eventConfig.maxIDs = 0xffff;

    platformConfig.applicationName = config->appConfig.name;
    platformConfig.x = config->appConfig.startPosX;
    platformConfig.y = config->appConfig.startPosY;
    platformConfig.width = config->appConfig.startWidth;
    platformConfig.height = config->appConfig.startHeight;
    
    engineState->width = config->appConfig.startWidth;
    engineState->height = config->appConfig.startHeight;

    rendererConfig.appName = config->appConfig.name;
    rendererConfig.engineName = "AV_VISUALIZER";
    rendererConfig.appVersion = (Version){0};
    rendererConfig.engineVersion = (Version){0};
    rendererConfig.enableValidation = true;
    rendererConfig.platformState = NULL; //platformMem,

    jobsystemConfig.maxWorkerThreads = 10;
    
    ioSystemConfig.concurrentRequests = 16;
    ioSystemConfig.inlineBufferSize = 256 * 1024; // 256 KB
    ioSystemConfig.threadCount = 1;
    
    
    uint64 totalMemSize = 0;
    for(uint32 i = 0; i < sizeof(engineSystems)/sizeof(EngineSystem); i++){
        uint64 memSize = 0;
        engineSystems[i].initialize(&memSize, NULL, engineSystems[i].config);
        totalMemSize += memSize;
    }
    engineState->systemsMemory = avAllocate(totalMemSize, "Allocating systems memory");
    byte* memory = engineState->systemsMemory;
    for(uint32 i = 0; i < sizeof(engineSystems)/sizeof(EngineSystem); i++){
        uint64 memSize = 0;
        engineSystems[i].initialize(&memSize, memory, engineSystems[i].config);
        engineSystems[i].state = memory;
        memory += memSize;
    }

    engine_on_event_system_initialized();
}

void systemsUninitialize(){
    for(uint32 i = 0; i < sizeof(engineSystems)/sizeof(EngineSystem); i++){
        engineSystems[i].uninitialize(engineSystems[i].state);
    }
    avFree(engineState->systemsMemory);
}

bool8 engineRun(EngineConfig* game_inst){
    engineState->is_running = true;
    clockStart(&engineState->clock);
    clockUpdate(&engineState->clock);
    engineState->last_time = engineState->clock.elapsed;

    double targetFrameSeconds = 1.0f / 120.0f;
    double frameElapsedTime = 0;

    systemsInitialize(game_inst);

    if(!engineState->config.initialize(&engineState->config)){
        avAssert(0, "Game failed to initialize");
        return false;
    }

    while(engineState->is_running){
        if(!platformPumpMessages()){
            engineState->is_running = false;
        }
        eventsDispatch();
        if(engineState->is_suspended) continue;

        clockUpdate(&engineState->clock);
        double current_time = engineState->clock.elapsed;
        double delta = (current_time - engineState->last_time);
        double frame_start_time = platformGetAbsoluteTime();

        ioSystemUpdate();

        
    

        // prepare for next frame
        inputUpdate(); // store current keys pressed to previous keys pressed
        engineState->last_time = current_time;
        engineState->frameIndex++;
    }

    engineState->is_running = false;
    engineState->config.shutdown(&engineState->config);


    systemsUninitialize();
    avFree(engineState);

    avLogShutdown();
    return true;
}


static void engineOnEvent(Event* events, uint32 count) {
    for(uint32 i = 0; i < count; i++){
        Event* event = events+i;
        switch (event->id) {
            case EVENT_CODE_APPLICATION_QUIT: {
                avInfo("EVENT_CODE_APPLICATION_QUIT recieved, shutting down");
                engineState->is_running = false;
                event->flags.consumed = 1;
                continue;
            }
        }
    }
}

static void engineOnResized(Event* events, uint32 count){//(uint16 code, void* sender, void* listener_inst, EventContext context) {
    bool32 resized = false;
    uint16 width = 0;
    uint16 height = 0;
    for(uint32 i = 0; i < count; i++){
        Event* event = events+i;

        if (event->id == EVENT_CODE_RESIZED) {
            resized = true;
            event->flags.consumed = 1;
            width = event->context.data.u16[0];
            height = event->context.data.u16[1];
            continue;

            
        }
    }
    if(resized){
        engineState->width = width;
        engineState->height = height;
        engineState->config.onResize(&engineState->config, engineState->width, engineState->height);
        //rendererSignalResize();
    }
}

void engine_on_event_system_initialized(void) {
    // Register for engine-level events.
    registerEventSink(EVENT_CODE_APPLICATION_QUIT, engineOnEvent);
    registerEventSink(EVENT_CODE_RESIZED, engineOnResized);
}

// bool8 engine_run(EngineConfig* game_inst) {
    //     engineState->is_running = true;
    //     clockStart(&engineState->clock);
    //     clockUpdate(&engineState->clock);
    //     engineState->last_time = engineState->clock.elapsed;
    //     // f64 running_time = 0;
    //     // TODO: frame rate lock
    //     // u8 frame_count = 0;
    //     double target_frame_seconds = 1.0f / 120;
    //     double frame_elapsed_time = 0;
        
    //     systemsInitialize(game_inst);
        
    //     // Initialize the game.
    //     if (!engineState->game_inst->initialize(engineState->game_inst)) {
    //         avAssert(0, "Game failed to initialize.");
    //         return false;
    //     }
    
    //     while (engineState->is_running) {
    //         if (!platformPumpMessages()) {
    //             engineState->is_running = false;
    //         }
    //         eventsDispatch();
    //         if (!engineState->is_suspended) {
    //             // Update clock and get delta time.
    //             clockUpdate(&engineState->clock);
    //             double current_time = engineState->clock.elapsed;
    //             double delta = (current_time - engineState->last_time);
    //             double frame_start_time = platformGetAbsoluteTime();
    
        
            
        
                
    
    //             ioSystemUpdate();
    
    //             // Figure out how long the frame took and, if below
    //             double frame_end_time = platformGetAbsoluteTime();
    //             frame_elapsed_time = frame_end_time - frame_start_time;
    //             // running_time += frame_elapsed_time;
    //             double remaining_seconds = target_frame_seconds - frame_elapsed_time;
    
    //             if (remaining_seconds > 0) {
    //                 double remaining_ms = (remaining_seconds * 1000);
    
    //                 // If there is time left, give it back to the OS.
    //                 double limit_frames = true;
    //                 if (remaining_ms > 0 && limit_frames) {
    //                     platformSleep(remaining_ms - 1);
    //                 }
    
    //                 // TODO: frame rate lock
    //                 // frame_count++;
    //             }
    
    //             // NOTE: Input update/state copying should always be handled
    //             // after any input should be recorded; I.E. before this line.
    //             // As a safety, input is the last thing to be updated before
    //             // this frame ends.
    //             inputUpdate();
    
    //             // Update last time
    //             engineState->last_time = current_time;
    //             engineState->frameIndex++;
    //         }
    //     }
    
    //     engineState->is_running = false;
    
    //     // Shut down the game.
    //     engineState->game_inst->shutdown(engineState->game_inst);
    
    //     // Unregister from events.
    //     //eventUnregister(EVENT_CODE_APPLICATION_QUIT, 0, engineOnEvent);
    //     //unregisterEventSink(EVENT_CODE_APPLICATION_QUIT, 0);
    //     //inputSystemShutdown(inputMem);
    
    //     //platformSystemShutdown(platformMem);
    //     systemsUninitialize();
    //     avFree(engineState);
    
    //     avLogShutdown();
    //     return true;
    // }

