#pragma once
#include <vulkan/vulkan.h>
#include <logging.h>
#include <vulkan/vulkan_core.h>

typedef struct Instance {
	VkInstance handle;
	VkDebugUtilsMessengerEXT debugMessenger;
} Instance;

typedef struct Gpu {
	VkPhysicalDevice physical;
	VkDevice handle;

	VkPhysicalDeviceProperties properties;
	VkPhysicalDeviceFeatures features;
	VkPhysicalDeviceMemoryProperties memory;

	uint32 graphicsFamily;
	uint32 computeFamily;
	uint32 transferFamily;
	uint32 presentFamily;

	VkQueue graphicsQueue;
	VkQueue computeQueue;
	VkQueue transferQueue;
	VkQueue presentQueue;
} Gpu;


typedef struct Swapchain {
	VkSurfaceKHR surface;
} Swapchain;

typedef struct Renderer {
	Instance instance;
	Gpu gpu;
	Swapchain swapchain;
} Renderer;

bool32 rendererCreate(Renderer* renderer);
void rendererDestroy(Renderer* renderer);