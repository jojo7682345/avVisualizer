#pragma once
#include "core/platform.h"

typedef struct PlatformBackend {
	bool32 initialized;
	void* api;
    bool8 (*startup)(uint64* memoryRequirement, void* state, PlatformConfig* config);
    void (*shutdown)(void* state);

    void (*getFramebufferSize)(uint32* w, uint32* h);
    bool8 (*pumpMessages)(void);

    void (*consoleWrite)(const char* msg, uint8 color);
    void (*consoleWriteError)(const char* msg, uint8 color);

    double (*getAbsoluteTime)(void);
    void (*sleep)(uint64 ms);
} PlatformBackend;