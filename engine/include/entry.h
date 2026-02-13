#pragma once

#include "defines.h"
#include "engine.h"
#include "AvUtils/avLogging.h"


/** @brief Externally-defined function to create a application, provided by the consumer
 * of this library.
 * @param out_app A pointer which holds the created application object as provided by the consumer.
 * @returns True on successful creation; otherwise false.
 */
extern bool8 create_application(Application* out_app);

extern bool8 initialize_application(Application* app);

/**
 * @brief The main entry point of the application.
 * @returns 0 on successful execution; nonzero on error.
 */
int main(void) {
    // Request the application instance from the application.
    Application app_inst = {0};
    if (!create_application(&app_inst)) {
        avAssert(0,"Could not create application!");
        return -1;
    }

    // Ensure the function pointers exist.
    if (!app_inst.renderFrame || !app_inst.prepareFrame || !app_inst.update || !app_inst.initialize || !app_inst.on_resize) {
        avAssert(0,"The application's function pointers must be assigned!");
        return -2;
    }

    // Initialization.
    if (!engine_create(&app_inst)) {
        avAssert(0,"Engine failed to create!.");
        return 1;
    }

    if (!initialize_application(&app_inst)) {
        avAssert(0, "Could not initialize application!");
        return -1;
    }

    // Begin the engine loop.
    if (!engine_run(&app_inst)) {
        avAssert(0, "Application did not shutdown gracefully.");
        return 2;
    }

    return 0;
}