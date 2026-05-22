#pragma once
#include "../platformLinux.h"

bool32 isX11();
bool32 createX11Backend(PlatformBackend* bkend);
void destroyX11Backend(PlatformBackend* bkend);