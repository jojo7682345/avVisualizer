#pragma once

#include "defines.h"
#include "engine.h"
#include "AvUtils/avLogging.h"


/** @brief Externally-defined function to create a application, provided by the consumer
 * of this library.
 * @param out_app A pointer which holds the created application object as provided by the consumer.
 * @returns True on successful creation; otherwise false.
 */
extern bool8 configureEngine(EngineConfig* out_app);

/**
 * @brief The main entry point of the application.
 * @returns 0 on successful execution; nonzero on error.
 */
int main(void) {
    // Request the application instance from the application.
    EngineConfig config = {0};
    if (!configureEngine(&config)) {
        avError("Could not create application!");
        return -1;
    }

    // Ensure the function pointers exist.
    if (!config.initialize) {
        avError("The application's function pointers must be assigned!");
        return -2;
    }

    // Initialization.
    if (!engineInitialize(&config)) {
        avError("Engine failed to create!.");
        return 1;
    }

    // Begin the engine loop.
    if (!engineRun(&config)) {
        avError("Application did not shutdown gracefully.");
        return 2;
    }

    return 0;
}