#pragma once
#include <vulkan/vulkan.h>
#include <logging.h>
#include <vulkan/vulkan_core.h>
#include "renderer.h"



typedef enum GpuTypePreference{
	GPU_TYPE_ANY,
	GPU_TYPE_DISCRETE,
	GPU_TYPE_INTEGRATED,
} GpuTypePreference;

typedef struct GpuPreferences {
	GpuTypePreference type;
	bool32 preferDedicatedCompute;
	bool32 preferDedicatedTransfer;
	VkSurfaceKHR mainSurface;
} GpuPreferences;

bool32 querryGpus(Renderer* renderer, GpuRequirements requirements, GpuPreferences preferences);

void cleanupGpus(Renderer* renderer);

bool32 gpuSelect(Renderer* renderer, GpuRequirements requirements, GpuPreferences preferences, Gpu* gpu);
bool32 gpuActivate(Renderer* renderer, Gpu* gpu);