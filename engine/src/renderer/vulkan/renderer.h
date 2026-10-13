#pragma once
#include <vulkan/vulkan.h>
#include <logging.h>
#include <vulkan/vulkan_core.h>
#include <containers/idMapping.h>
#include "../renderer.h"

typedef struct Instance {
	VkInstance handle;
	VkDebugUtilsMessengerEXT debugMessenger;
} Instance;

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

	VkSurfaceKHR surface;
}GpuRequirements;

typedef struct Gpu {
	VkPhysicalDevice physical;
	VkDevice device;

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

	VkCommandPool mainCommandPool;
} Gpu;

typedef struct PresentTarget {
	VkSurfaceKHR surface;
	VkSwapchainKHR swapchain;

	Gpu* gpu;
} PresentTarget;

typedef enum RenderTargetType {
	RENDER_TARGET_TYPE_IMAGE, // aquire image using frameIndex
	RENDER_TARGET_TYPE_SWAPCHAIN, // aquireImage using presentTarget swapchain
	RENDER_TARGET_TYPE_CROSS_GPU_SWAPCHAIN, // aquire image using presentTarget swapchain
} RenderTargetType;

typedef struct RenderTarget {
	VkExtent2D extent;
	VkFormat format;
	uint32 imageCount;
	VkImage* images;
	VkImageView* imageViews;
	VkDeviceMemory imageMemory;
	VkImageUsageFlags usage;
	VkImageAspectFlags aspect;
	VkSampleCountFlagBits samples;

	RenderTargetType type;
	PresentTarget* presentTarget; // is owned by the renderTarget on the main gpu
	RenderTargetHandle presentationTarget; // is renderTarget image needs to be transfered to for cross gpu presenting, if -1 renderTarget is owned by other gpu
} RenderTarget;


typedef struct Renderer {
	Instance instance;

	uint32 deviceCount;	
	Gpu* devices; // additional devices for presenting
	Gpu* gpu; // main gpu (for rendering)
	GpuRequirements gpuRequirements; // stored for lazily activating gpu's

	IdMapping(RenderTarget) renderTargets;
	PresentTarget* presentTargets;
	
} Renderer;

extern Renderer* renderer;