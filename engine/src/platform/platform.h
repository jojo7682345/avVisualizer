#pragma once

#include "defines.h"

typedef struct platformSystemConfig {
    const char* applicationName;
    int32 x;
    int32 y;
    int32 width;
    int32 height;
} platformSystemConfig;

/**
 * @brief Performs startup routines within the platform layer. Should be called twice,
 * once to obtain the memory requirement (with state=0), then a second time passing
 * an allocated block of memory to state.
 *
 * @param memory_requirement A pointer to hold the memory requirement in bytes.
 * @param state A pointer to a block of memory to hold state. If obtaining memory requirement only, pass 0.
 * @param config A pointer to a configuration platform_system_config structure required by this system.
 * @return True on success; otherwise false.
 */
bool8 platformSystemStartup(uint64* memoryRequirement, void* state, void* config);

/**
 * @brief Shuts down the platform layer.
 *
 * @param plat_state A pointer to the platform layer state.
 */
void platformSystemShutdown(void* platformState);


void platformGetFramebufferSize(uint32* width, uint32* height);

/**
 * @brief Performs any platform-specific message pumping that is required
 * for windowing, etc.
 *
 * @return True on success; otherwise false.
 */
bool8 platformPumpMessages(void);

/**
 * @brief Performs platform-specific printing to the console of the given
 * message and colour code (if supported).
 *
 * @param message The message to be printed.
 * @param colour The colour to print the text in (if supported).
 */
void platformConsoleWrite(const char* message, uint8 colour);

/**
 * @brief Performs platform-specific printing to the error console of the given
 * message and colour code (if supported).
 *
 * @param message The message to be printed.
 * @param colour The colour to print the text in (if supported).
 */
void platformConsoleWriteError(const char* message, uint8 colour);

/**
 * @brief Gets the absolute time since the application started.
 *
 * @return The absolute time since the application started.
 */
double platformGetAbsoluteTime(void);

/**
 * @brief Sleep on the thread for the provided milliseconds. This blocks the main thread.
 * Should only be used for giving time back to the OS for unused update power.
 * Therefore it is not exported. Times are approximate.
 *
 * @param ms The number of milliseconds to sleep for.
 */
AV_API void platformSleep(uint64 ms);