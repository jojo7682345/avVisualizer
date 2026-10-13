#pragma once
#include "renderer/rendererCore.h"
#include <vulkan/vulkan_core.h>


RenderTargetHandle renderTargetCreateFromSurface(VkSurfaceKHR surface, WindowRenderTargetConfig config);
