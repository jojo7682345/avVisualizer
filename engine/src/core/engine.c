#include "engine.h"
#include "core/event.h"
#include "core/clock.h"
#include "core/input.h"
#include "platform/platform.h"
#include "renderer/renderer.h"

#include <AvUtils/avLogging.h>
#include <AvUtils/avMemory.h>

typedef struct engine_state_t {
    Application* game_inst;
    bool8 is_running;
    bool8 is_suspended;
    int16 width;
    int16 height;
    Clock clock;
    double last_time;

    // Indicates if the window is currently being resized.
    //b8 resizing;
    // The current number of frames since the last resize operation.
    // Only set if resizing = true. Otherwise 0.
    //u8 frames_since_resize;
} engine_state_t;

static engine_state_t* engineState;


bool8 engine_create(Application* game_inst) {
    if (game_inst->engineState) {
        avAssert(0, "engine_create called more than once.");
        return false;
    }

    // Stand up the engine state.
    game_inst->engineState = avAllocate(sizeof(engine_state_t), "");
    engineState = game_inst->engineState;
    engineState->game_inst = game_inst;
    engineState->is_running = false;
    engineState->is_suspended = false;

    

    if(engineState->game_inst->logSettings) setLogSettings(*engineState->game_inst->logSettings);

    return true;
}

static bool8 engineOnEvent(uint16 code, void* sender, void* listener_inst, EventContext context);

bool8 engine_run(Application* game_inst) {
    engineState->is_running = true;
    clockStart(&engineState->clock);
    clockUpdate(&engineState->clock);
    engineState->last_time = engineState->clock.elapsed;
    // f64 running_time = 0;
    // TODO: frame rate lock
    // u8 frame_count = 0;
    double target_frame_seconds = 1.0f / 60;
    double frame_elapsed_time = 0;
    
    uint64 memSize = 0;
    eventSystemInitialize(&memSize, 0, 0);
    void* eventMem = avAllocate(memSize, "");
    eventSystemInitialize(&memSize, eventMem, 0);

    inputSystemInitialize(&memSize, 0, 0);
    void* inputMem = avAllocate(memSize, "");
    inputSystemInitialize(&memSize, inputMem, 0);

    platformSystemConfig platformSystemConfig = {
        .applicationName = game_inst->appConfig.name,
        .x = game_inst->appConfig.startPosX,
        .y = game_inst->appConfig.startPosY,
        .width = game_inst->appConfig.startWidth,
        .height = game_inst->appConfig.startHeight,
    };

    engineState->width = game_inst->appConfig.startWidth;
    engineState->height = game_inst->appConfig.startHeight;

    platformSystemStartup(&memSize, 0, &platformSystemConfig);
    void* platformMem = avAllocate(memSize, "");
    platformSystemStartup(&memSize, platformMem, &platformSystemConfig);

    RendererConfig rendererConfig = {
        .appName = game_inst->appConfig.name,
        .engineName = "AV_VISUALIZER",
        .appVersion = 0,
        .engineVersion = 0,
        .enableValidation = true,
        .platformState = platformMem,
    };

    rendererStartup(&memSize, 0, &rendererConfig);
    void* rendererMem = avAllocate(memSize, "");
    rendererStartup(&memSize, rendererMem, &rendererConfig);

    // Initialize the game.
    if (!engineState->game_inst->initialize(engineState->game_inst)) {
        avAssert(0, "Game failed to initialize.");
        return false;
    }

    while (engineState->is_running) {
        if (!platformPumpMessages()) {
            engineState->is_running = false;
        }

        if (!engineState->is_suspended) {
            // Update clock and get delta time.
            clockUpdate(&engineState->clock);
            double current_time = engineState->clock.elapsed;
            double delta = (current_time - engineState->last_time);
            double frame_start_time = platformGetAbsoluteTime();

    
            // if (!renderer_frame_prepare()) {
            //     // This can also happen not just from a resize above, but also if a renderer flag
            //     // (such as VSync) changed, which may also require resource recreation. To handle this,
            //     // Notify the application of a resize event, which it can then pass on to its rendergraph(s)
            //     // as needed.
            //     engineState->game_inst->on_resize(engineState->game_inst, engineState->width, engineState->height);
            //     continue;
            // }

            if (!engineState->game_inst->update(engineState->game_inst)) {
                avAssert(0, "Game update failed, shutting down.");
                engineState->is_running = false;
                break;
            }

            // if (!renderer_begin(&engineState->p_frame_data)) {
            //     KFATAL("Failed to begin renderer. Shutting down.");
            //     engineState->is_running = false;
            //     break;
            // }

            //rendererBeginFrame(engineState->width, engineState->height);
            
            // Have the application generate the render packet.
            bool8 prepare_result = engineState->game_inst->prepareFrame(engineState->game_inst);

            if (!prepare_result) {
                continue;
            }
            extern void startFrame();
            extern void endFrame();
            startFrame();
            // Call the game's render routine.
            if (!engineState->game_inst->renderFrame(engineState->game_inst)) {
                avAssert(0, "Game render failed, shutting down.");
                engineState->is_running = false;
                break;
            }
            endFrame();

            rendererDrawFrame();
            // End the frame.
            //renderer_end();

            

            // // Present the frame.
            // if (!renderer_present()) {
            //     avAssert(0, "The call to renderer_present failed. This is likely unrecoverable. Shutting down.");
            //     engineState->is_running = false;
            //     break;
            // }

            // Figure out how long the frame took and, if below
            double frame_end_time = platformGetAbsoluteTime();
            frame_elapsed_time = frame_end_time - frame_start_time;
            // running_time += frame_elapsed_time;
            double remaining_seconds = target_frame_seconds - frame_elapsed_time;

            if (remaining_seconds > 0) {
                double remaining_ms = (remaining_seconds * 1000);

                // If there is time left, give it back to the OS.
                double limit_frames = false;
                if (remaining_ms > 0 && limit_frames) {
                    platformSleep(remaining_ms - 1);
                }

                // TODO: frame rate lock
                // frame_count++;
            }

            // NOTE: Input update/state copying should always be handled
            // after any input should be recorded; I.E. before this line.
            // As a safety, input is the last thing to be updated before
            // this frame ends.
            inputUpdate();

            // Update last time
            engineState->last_time = current_time;
        }
    }

    rendererShutdown(0);

    engineState->is_running = false;

    // Shut down the game.
    engineState->game_inst->shutdown(engineState->game_inst);

    // Unregister from events.
    eventUnregister(EVENT_CODE_APPLICATION_QUIT, 0, engineOnEvent);

    inputSystemShutdown(inputMem);

    platformSystemShutdown(platformMem);

    return true;
}

static bool8 engineOnEvent(uint16 code, void* sender, void* listener_inst, EventContext context) {
    switch (code) {
        case EVENT_CODE_APPLICATION_QUIT: {
            //KINFO("EVENT_CODE_APPLICATION_QUIT recieved, shutting down.\n");
            engineState->is_running = false;
            return true;
        }
    }

    return false;
}

static bool8 engineOnResized(uint16 code, void* sender, void* listener_inst, EventContext context) {
    if (code == EVENT_CODE_RESIZED) {
        engineState->width = context.data.u16[0];
        engineState->height = context.data.u16[1];
        engineState->game_inst->on_resize(engineState->game_inst, engineState->width, engineState->height);
        rendererSignalResize();
        return true;
    }

    // Event purposely not handled to allow other listeners to get this.
    return false;
}

void engine_on_event_system_initialized(void) {
    // Register for engine-level events.
    eventRegister(EVENT_CODE_APPLICATION_QUIT, 0, engineOnEvent);
    eventRegister(EVENT_CODE_RESIZED, 0, engineOnResized);
}

