#pragma once
#include <defines.h>

typedef struct WaylandBackendConfig{
	const char* title;
	const char* appClass;
} WaylandBackendConfig;

bool8 waylandBackendInitialize(uint64* memoryRequirements, void* statePtr, void* configPtr);