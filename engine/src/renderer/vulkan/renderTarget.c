#include "renderTarget.h"
#include "containers/darray.h"
#include "containers/idMapping.h"
#include "core/platform.h"
#include "logging.h"
#include "renderer/renderer.h"
#include "renderer/vulkan/gpu.h"
#include "renderer/vulkan/renderer.h"
#include <vulkan/vulkan_core.h>
#include <AvUtils/avMath.h>
#include <AvUtils/avMemory.h>

RenderTargetHandle renderTargetCreateFromWindow(Window window, WindowRenderTargetConfig config){
	VkSurfaceKHR surface;
	if(!platformCreateVulkanSurface(renderer->instance.handle, &surface, window)){
		avError("Failed to create vulkan surface");
		return MAPPING_INVALID_ID;
	}
	
	RenderTargetHandle handle = renderTargetCreateFromSurface(surface, config);
	if(handle==MAPPING_INVALID_ID){
		vkDestroySurfaceKHR(renderer->instance.handle, surface,	NULL);
		return MAPPING_INVALID_ID;
	}
	return handle;
}

static bool32 isDeviceSuitable(Gpu* gpu, VkSurfaceKHR surface){
	bool32 supported = false;
	if(vkGetPhysicalDeviceSurfaceSupportKHR(gpu->physical, gpu->presentFamily, surface, &supported)!=VK_SUCCESS){
		avError("Failed to querry surface support");
		return false;
	}
	return supported;
}

static bool32 selectGpuForSurface(VkSurfaceKHR surface, Gpu** res){
	Gpu* gpu = renderer->gpu;
	bool32 foundGpu = false;
	if(!isDeviceSuitable(gpu, surface)){
		for(uint32 i = 0; i < renderer->deviceCount; i++){
			if(isDeviceSuitable(&renderer->devices[i], surface)){
				gpu = &renderer->devices[i];
				foundGpu = true;
				break;
			}
		}
	}else{
		foundGpu = true;
	}
	if(!foundGpu){
		avError("Failed to find a suitable gpu to present to surface");
		return false;
	}
	*res = gpu;
	return true;
}

typedef struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    VkSurfaceFormatKHR* formats;
    VkPresentModeKHR* presentModes;
} SwapChainSupportDetails;

SwapChainSupportDetails querrySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface){
    SwapChainSupportDetails details = {0};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);
    uint32 formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, 0);
    if(formatCount){
        details.formats = darrayReserve(VkSurfaceFormatKHR, formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats);
    }

    uint32 presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, NULL);
    if(presentModeCount){
        details.presentModes = darrayReserve(VkPresentModeKHR, presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes);
    }
    return details;
}

void cleanupSwapchainSupport(SwapChainSupportDetails details){
	darrayDestroy(details.formats);
	darrayDestroy(details.presentModes);
}

VkSurfaceFormatKHR chooseSwapSurfaceFormat(SwapChainSupportDetails details, WindowSurfaceFormat requestedFormat){
	uint32 formatCount = darrayLength(details.formats);
	for(uint32 i = 0; i < formatCount; i++){
		VkSurfaceFormatKHR availableFormat = details.formats[i];
		if(availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR){
			return availableFormat;
		}
	}
	return details.formats[0];
}

VkPresentModeKHR chooseSwapPresentMode(SwapChainSupportDetails details, WindowPresentMode requestedMode){
	uint32 modeCount = darrayLength(details.presentModes);
	for(uint32 i = 0; i < modeCount; i++){
		VkPresentModeKHR availablePresentMode = details.presentModes[i];
		
		switch(requestedMode){
			case WINDOW_PRESENT_MODE_IMMEDIATE:
				if(availablePresentMode==VK_PRESENT_MODE_IMMEDIATE_KHR){
					return availablePresentMode;
				}
				break;
			case WINDOW_PRESENT_MODE_DOUBLE_BUFFERED:
				if(availablePresentMode==VK_PRESENT_MODE_FIFO_KHR){
					return availablePresentMode;
				}
				break;
			case WINDOW_PRESENT_MODE_TRIPPLE_BUFFERED:
				if(availablePresentMode==VK_PRESENT_MODE_MAILBOX_KHR){
					return availablePresentMode;
				}
				break;
		}
	}
	return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseSwapExtent(SwapChainSupportDetails details, uint32 requestedWidth, uint32 requestedHeight) {
    if(details.capabilities.currentExtent.width != UINT32_MAX) {
        return details.capabilities.currentExtent;
    }else{
        VkExtent2D actualExtent = {
            requestedWidth,
            requestedHeight,
        };
        actualExtent.width = AV_CLAMP(actualExtent.width, details.capabilities.minImageExtent.width, details.capabilities.maxImageExtent.width);
        actualExtent.height = AV_CLAMP(actualExtent.height, details.capabilities.minImageExtent.height, details.capabilities.maxImageExtent.height);
        return actualExtent;
    }
}

uint32 chooseSwapImageCount(SwapChainSupportDetails details){
	uint32 imageCount = 0;
	imageCount = details.capabilities.minImageCount + 1;
	if (details.capabilities.maxImageCount > 0 && imageCount > details.capabilities.maxImageCount) {
    	imageCount = details.capabilities.maxImageCount;
	}
	return imageCount;
}

static void fillSwapchainQueueSharing(VkSwapchainCreateInfoKHR* createInfo, Gpu* gpu, bool32 crossGpuPresenting, WindowRenderTargetConfig config){
	uint32 queueFamilyIndices[] = {gpu->graphicsFamily, gpu->presentFamily, gpu->transferFamily};
	if(crossGpuPresenting){
		if(gpu->transferFamily != gpu->presentFamily){
			createInfo->imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo->pQueueFamilyIndices = queueFamilyIndices+1;
			createInfo->queueFamilyIndexCount = 2;
		}else{
			createInfo->imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			createInfo->queueFamilyIndexCount = 0;
			createInfo->pQueueFamilyIndices = 0;
		}
	}else{
		if(gpu->graphicsFamily != gpu->presentFamily){
			createInfo->imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo->pQueueFamilyIndices = queueFamilyIndices;
			createInfo->queueFamilyIndexCount = 2;
			if(config.transferDestination && (gpu->transferFamily != gpu->graphicsFamily && gpu->transferFamily != gpu->presentFamily)){
				createInfo->queueFamilyIndexCount = 3;
			}
		}else{
			if(config.transferDestination && (gpu->transferFamily != gpu->graphicsFamily && gpu->transferFamily != gpu->presentFamily)){
				createInfo->imageSharingMode = VK_SHARING_MODE_CONCURRENT;
				createInfo->queueFamilyIndexCount = 2;
				createInfo->pQueueFamilyIndices = queueFamilyIndices + 1;
			}else{
				createInfo->imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
				createInfo->queueFamilyIndexCount = 0;
				createInfo->pQueueFamilyIndices = 0;
			}
		}
	}
}

static RenderTargetHandle renderTargetCreateFromSwapchain(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, RenderTargetType type, PresentTarget presentTarget){
	RenderTarget target = {
		.aspect = VK_IMAGE_ASPECT_COLOR_BIT,
		.imageMemory = VK_NULL_HANDLE,
		.extent = extent,
		.format = format,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | usage,
		.type =  type,
		.presentationTarget = MAPPING_INVALID_ID,
	};

	VkResult result = vkGetSwapchainImagesKHR(presentTarget.gpu->device, presentTarget.swapchain, &target.imageCount, NULL);
	if(result != VK_SUCCESS){
		avError("Failed to get swapchain images");
		return MAPPING_INVALID_ID;
	}
	
	void* data = avAllocate((sizeof(VkImage) + sizeof(VkImageView)) * target.imageCount, "");
	avMemset(data, 0, (sizeof(VkImage) + sizeof(VkImageView)) * target.imageCount);
	target.images = data;
	target.imageViews = (VkImageView*)(((VkImage*)data) + target.imageCount);
	result = vkGetSwapchainImagesKHR(presentTarget.gpu->device, presentTarget.swapchain, &target.imageCount, target.images);
	if(result != VK_SUCCESS){
		avError("Failed to get swapchain images");
		avFree(target.images);
		return MAPPING_INVALID_ID;
	}
	for(uint32 i = 0; i < target.imageCount; i++){
		VkImageViewCreateInfo createInfo = {0};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		createInfo.image = target.images[i];
		createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		createInfo.format = target.format;
		createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		createInfo.subresourceRange.baseMipLevel = 0;
		createInfo.subresourceRange.levelCount = 1;
		createInfo.subresourceRange.baseArrayLayer = 0;
		createInfo.subresourceRange.layerCount = 1;
		result = vkCreateImageView(presentTarget.gpu->device, &createInfo, NULL, &target.imageViews[i]);
		if(result != VK_SUCCESS){
			avError("Failed to create image view %u", i);
			for(uint32 j = 0; j < target.imageCount; j++){
				if(target.imageViews[j]!=VK_NULL_HANDLE){
					vkDestroyImageView(presentTarget.gpu->device, target.imageViews[j], NULL);
				}
			}
			avFree(target.images);
			return MAPPING_INVALID_ID;
		}
	}

	RenderTargetHandle handle = MAPPING_ADD(renderer->renderTargets, target);
	if(handle == MAPPING_INVALID_ID){
		avError("Could not create additional renderTarget");
		for(uint32 i = 0; i < target.imageCount; i++){
			vkDestroyImageView(presentTarget.gpu->device, target.imageViews[i], NULL);
		}
		avFree(target.images);
		return MAPPING_INVALID_ID;
	}
	return handle;
}

RenderTargetHandle renderTargetCreateImage(ImageRenderTargetConfig config){
	
}

RenderTargetHandle renderTargetCreateFromSurface(VkSurfaceKHR surface, WindowRenderTargetConfig config){
	Gpu* gpu;
	if(!selectGpuForSurface(surface, &gpu)){
		return MAPPING_INVALID_ID;
	}

	SwapChainSupportDetails support = querrySwapchainSupport(gpu->physical, surface);
	if(darrayLength(support.formats)==0){
		avError("No formats supported for surface");
		cleanupSwapchainSupport(support);
		return MAPPING_INVALID_ID;
	}
	if(darrayLength(support.presentModes)==0){
		avError("No present modes supported for surface");
		cleanupSwapchainSupport(support);
		return MAPPING_INVALID_ID;
	}
	
	VkSurfaceFormatKHR format = chooseSwapSurfaceFormat(support, config.format);
	VkPresentModeKHR presentMode = chooseSwapPresentMode(support, config.presentMode);
	VkExtent2D extent = chooseSwapExtent(support, config.requestedWidth, config.requestedHeight);
	uint32 imageCount = chooseSwapImageCount(support);

	bool32 crossGpuPresenting = gpu != renderer->gpu;

	VkImageUsageFlags swapUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if(crossGpuPresenting){
		swapUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	}

	PresentTarget presentTarget = {
		.gpu = gpu,
		.surface = surface, // presentTarget now owns the surface
	};

	VkSwapchainCreateInfoKHR createInfo = { 0 };
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = surface;
	createInfo.minImageCount = imageCount;
	createInfo.imageFormat = format.format;
	createInfo.imageColorSpace = format.colorSpace;
	createInfo.imageExtent = extent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = swapUsage;
	createInfo.preTransform = support.capabilities.currentTransform;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;
	createInfo.oldSwapchain = VK_NULL_HANDLE;
	fillSwapchainQueueSharing(&createInfo, gpu, crossGpuPresenting, config);
	
	if(!gpuIsActive(gpu)){
		if(!gpuActivate(renderer, gpu)){
			avError("Failed to activate compatible gpu");
			return MAPPING_INVALID_ID;
		}
	}

	VkResult result = vkCreateSwapchainKHR(gpu->device, &createInfo, NULL, &presentTarget.swapchain);
	if(result != VK_SUCCESS){
		avError("Failed to create swapchain");
		return MAPPING_INVALID_ID;
	}

	VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if(crossGpuPresenting){
		imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	}
	RenderTargetType type = crossGpuPresenting ? RENDER_TARGET_TYPE_CROSS_GPU_SWAPCHAIN : RENDER_TARGET_TYPE_SWAPCHAIN;
	RenderTargetHandle handle = renderTargetCreateFromSwapchain(extent, format.format, imageUsage, type, presentTarget);

	if(!crossGpuPresenting){
		return handle;
	}

	// create image backed renderTarget and link to the created swapchain handle

}

void renderTargetDestroy(RenderTargetHandle handle){
	(void) handle;
}