#pragma once

#include "defines.h"
#include "logging.h"

typedef struct application_config {
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
} application_config;

typedef struct Application {
    /** @brief The application configuration. */
    application_config appConfig;
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