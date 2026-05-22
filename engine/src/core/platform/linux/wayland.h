#pragma once
#include "../platformLinux.h"


bool32 isWayland();
bool32 createWaylandBackend(PlatformBackend* bkend);
void destroyWaylandBackend(PlatformBackend* bkend);
