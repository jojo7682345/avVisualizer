#pragma once
#include "defines.h"


bool8 platformRendererStartup(uint64* memoryRequirement, void* state, void* config);

void platformChoosePixelFormat();

void platformCreateGlContext();

void platformSwapBuffers();