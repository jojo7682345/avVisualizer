#pragma once
#include <vulkan/vulkan.h>
#include <logging.h>
#include "renderer.h"

typedef struct GpuFeatures {
	VkPhysicalDeviceFeatures vulkan10;
	// can be expanded later if required
} GpuFeatures;

typedef struct GpuRequirements {
	GpuFeatures requiredFeatures;

	const char* const* requiredExtensions;
	uint32 requiredExtensionCount;
	


	bool32 requireGraphicsQueue;
	bool32 requirePresentQueue;
	bool32 requireComputeQueue;
	bool32 requireTransferQueue;
}GpuRequirements;

typedef enum GpuTypePreference{
	GPU_TYPE_ANY,
	GPU_TYPE_DISCRETE,
	GPU_TYPE_INTEGRATED,
} GpuTypePreference;

typedef struct GpuPreferences {
	GpuTypePreference type;
	bool32 preferDedicatedCompute;
	bool32 preferDedicatedTransfer;
} GpuPreferences;


bool32 gpuSelect(Renderer* renderer, GpuRequirements requirements, GpuPreferences preferences, Gpu* gpu);
bool32 gpuConnect(Renderer* renderer, GpuRequirements requirements, Gpu* gpu);
void gpuDisconnect(Renderer* renderer);