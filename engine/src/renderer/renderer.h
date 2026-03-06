#pragma once
#include "defines.h"

typedef struct Version {
    uint8 major;
    uint8 minor;
    uint8 patch;
} Version;

typedef struct  RendererConfig {
    const char* appName;
    const char* engineName;
    Version engineVersion;
    Version appVersion;
    bool8 enableValidation;
    void* platformState;
} RendererConfig;

bool8 rendererStartup(uint64* memoryRequirement, void* state, void* config);
void rendererShutdown(void* state);

void rendererDrawFrame();
void rendererSignalResize();

void vulkanPlatformGetRequiredExtensionNames(const char*** namesDarray);
bool32 vulkanPlatformCreateSurface(void* rendererState, void* platformState, void* surface);
