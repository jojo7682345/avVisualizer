#pragma once
#include "defines.h"

typedef uint32 WindowID;

typedef struct WindowSystemConfig {
    uint32 maxWindows;
    void (*windowRequestCloseHandlerOverride) (WindowID window);
    void (*allwindowsClosedHandlerOverride) ();
} WindowSystemConfig;

bool8 initializeWindowSystem(uint64* memoryRequirements, void* statePtr, void* configPtr);
void shutdownWindowSystem(void* statePtr);


//void windowSystemUpdate();

WindowID windowCreate(uint32 x, uint32 y, uint32 width, uint32 height, const char* tile);
void windowRequestClose(WindowID window);
void windowDestroy(WindowID window);

