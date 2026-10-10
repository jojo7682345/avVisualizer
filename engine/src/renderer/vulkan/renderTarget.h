#pragma once
#include "renderer.h"
#include "renderer/renderer.h"
#include <vulkan/vulkan_core.h>


RenderTargetHandle renderTargetCreateFromSurface(VkSurfaceKHR surface, WindowRenderTargetConfig config);
