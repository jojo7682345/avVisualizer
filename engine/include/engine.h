#pragma once

#include "defines.h"
#include "logging.h"

typedef struct ApplicationConfig {
    /** @brief Window starting position x axis, if applicable. */
    int16 startPosX;

    /** @brief Window starting position y axis, if applicable. */
    int16 startPosY;

    /** @brief Window starting width, if applicable. */
    int16 startWidth;

    /** @brief Window starting height, if applicable. */
    int16 startHeight;

    /** @brief The application name used in windowing, if applicable. */
    char* name;
} ApplicationConfig;

typedef struct Application {
    /** @brief The application configuration. */
    ApplicationConfig appConfig;
    AvLogSettings* logSettings;


    /**
     * @brief Function pointer to the application's boot sequence. This should
     * fill out the application config with the application's specific requirements.
     * @param app_inst A pointer to the application instance.
     * @returns True on success; otherwise false.
     */
    bool8 (*boot)(struct Application* app_inst);

    /**
     * @brief Function pointer to application's initialize function.
     * @param app_inst A pointer to the application instance.
     * @returns True on success; otherwise false.
     * */
    bool8 (*initialize)(struct Application* app_inst);

    /**
     * @brief Function pointer to application's update function.
     * @param app_inst A pointer to the application instance.
     * @param p_frame_data A pointer to the current frame's data.
     * @returns True on success; otherwise false.
     * */
    bool8 (*update)(struct Application* app_inst);

    /**
     * @brief Function pointer to application's prepareFrame function.
     * @param app_inst A pointer to the application instance.
     * @param p_frame_data A pointer to the current frame's data.
     * @returns True on success; otherwise false.
     */
    bool8 (*prepareFrame)(struct Application* app_inst);

    /**
     * @brief Function pointer to application's render_frame function.
     * @param app_inst A pointer to the application instance.
     * @param p_frame_data A pointer to the current frame's data.
     * @returns True on success; otherwise false.
     * */
    bool8 (*renderFrame)(struct Application* app_inst);

    /**
     * @brief Function pointer to handle resizes, if applicable.
     * @param app_inst A pointer to the application instance.
     * @param width The width of the window in pixels.
     * @param height The height of the window in pixels.
     * */
    void (*on_resize)(struct Application* app_inst, uint32 width, uint32 height);

    /**
     * @brief Shuts down the application, prompting release of resources.
     * @param app_inst A pointer to the application instance.
     */
    void (*shutdown)(struct Application* app_inst);

    /** @brief application-specific state. Created and managed by the application. */
    void* state;

    /** @brief A block of memory to hold the engine state. Created and managed by the engine. */
    void* engineState;
} Application;

/**
 * @brief Creates the engine, standing up the platform layer and all
 * underlying subsystems.
 * @param game_inst A pointer to the application instance associated with the engine
 * @returns True on success; otherwise false.
 */
AV_API bool8 engine_create(struct Application* game_inst);

/**
 * @brief Starts the main engine loop.
 * @param game_inst A pointer to the application instance associated with the engine
 * @returns True on success; otherwise false.
 */
AV_API bool8 engine_run(struct Application* game_inst);

/**
 * @brief A callback made when the event system is initialized,
 * which internally allows the engine to begin listening for events
 * required for initialization.
 */
void engine_on_event_system_initialized(void);

/** @brief System internal event codes. Application should use codes beyond 255. */
typedef enum SystemEventCode {
    /** @brief Shuts the application down on the next frame. */
    EVENT_CODE_APPLICATION_QUIT = 0x01,

    /** @brief Keyboard key pressed.
     * Context usage:
     * uint16 key_code = data.data.uint16[0];
     * uint16 repeat_count = data.data.uint16[1];
     */
    EVENT_CODE_KEY_PRESSED = 0x02,

    /** @brief Keyboard key released.
     * Context usage:
     * uint16 key_code = data.data.uint16[0];
     * uint16 repeat_count = data.data.uint16[1];
     */
    EVENT_CODE_KEY_RELEASED = 0x03,

    /** @brief Mouse button pressed.
     * Context usage:
     * uint16 button = data.data.uint16[0];
     * uint16 x = data.data.int16[1];
     * uint16 y = data.data.int16[2];
     */
    EVENT_CODE_BUTTON_PRESSED = 0x04,

    /** @brief Mouse button released.
     * Context usage:
     * uint16 button = data.data.uint16[0];
     * uint16 x = data.data.int16[1];
     * uint16 y = data.data.int16[2];
     */
    EVENT_CODE_BUTTON_RELEASED = 0x05,

    /** @brief Mouse button pressed then released.
     * Context usage:
     * uint16 button = data.data.uint16[0];
     * uint16 x = data.data.int16[1];
     * uint16 y = data.data.int16[2];
     */
    EVENT_CODE_BUTTON_CLICKED = 0x06,

    /** @brief Mouse moved.
     * Context usage:
     * uint16 x = data.data.int16[0];
     * uint16 y = data.data.int16[1];
     */
    EVENT_CODE_MOUSE_MOVED = 0x07,

    /** @brief Mouse moved.
     * Context usage:
     * ui z_delta = data.data.int8[0];
     */
    EVENT_CODE_MOUSE_WHEEL = 0x08,

    /** @brief Resized/resolution changed from the OS.
     * Context usage:
     * uint16 width = data.data.uint16[0];
     * uint16 height = data.data.uint16[1];
     */
    EVENT_CODE_RESIZED = 0x09,

    // Change the render mode for debugging purposes.
    /* Context usage:
     * int32 mode = context.data.int32[0];
     */
    EVENT_CODE_SET_RENDER_MODE = 0x0A,

    /** @brief Special-purpose debugging event. Context will vary over time. */
    EVENT_CODE_DEBUG0 = 0x10,
    /** @brief Special-purpose debugging event. Context will vary over time. */
    EVENT_CODE_DEBUG1 = 0x11,
    /** @brief Special-purpose debugging event. Context will vary over time. */
    EVENT_CODE_DEBUG2 = 0x12,
    /** @brief Special-purpose debugging event. Context will vary over time. */
    EVENT_CODE_DEBUG3 = 0x13,
    /** @brief Special-purpose debugging event. Context will vary over time. */
    EVENT_CODE_DEBUG4 = 0x14,

    /** @brief The hovered-over object id, if there is one.
     * Context usage:
     * int32 id = context.data.uint32[0]; - will be INVALID ID if nothing is hovered over.
     */
    EVENT_CODE_OBJECT_HOVER_ID_CHANGED = 0x15,

    /**
     * @brief An event fired by the renderer backend to indicate when any render targets
     * associated with the default window resources need to be refreshed (i.e. a window resize)
     */
    EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED = 0x16,

    /**
     * @brief An event fired by the kvar system when a kvar has been updated.
     */
    EVENT_CODE_KVAR_CHANGED = 0x17,

    /**
     * @brief An event fired when a watched file has been written to.
     * Context usage:
     * uint32 watch_id = context.data.uint32[0];
     */
    EVENT_CODE_WATCHED_FILE_WRITTEN = 0x18,

    /**
     * @brief An event fired when a watched file has been removed.
     * Context usage:
     * uint32 watch_id = context.data.uint32[0];
     */
    EVENT_CODE_WATCHED_FILE_DELETED = 0x19,

    /**
     * @brief An event fired while a button is being held down and the
     * mouse is moved.
     *
     * Context usage:
     * int16 x = context.data.int16[0]
     * int16 y = context.data.int16[1]
     * uint16 button = context.data.uint16[2]
     */
    EVENT_CODE_MOUSE_DRAGGED = 0x20,

    /**
     * @brief An event fired when a button is pressed and a mouse movement
     * is done while it is pressed.
     *
     * Context usage:
     * int16 x = context.data.int16[0]
     * int16 y = context.data.int16[1]
     * uint16 button = context.data.uint16[2]
     */
    EVENT_CODE_MOUSE_DRAG_BEGIN = 0x21,

    /**
     * @brief An event fired when a button is released was previously dragging.
     *
     * Context usage:
     * int16 x = context.data.int16[0]
     * int16 y = context.data.int16[1]
     * uint16 button = context.data.uint16[2]
     */
    EVENT_CODE_MOUSE_DRAG_END = 0x22,

    /** @brief The maximum event code that can be used internally. */
    MAX_EVENT_CODE = 0xFF
} SystemEventCode;