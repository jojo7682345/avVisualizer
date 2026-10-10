#include "renderTarget.h"
#include "containers/darray.h"
#include "containers/idMapping.h"
#include "core/platform.h"
#include "logging.h"
#include "renderer/renderer.h"
#include <vulkan/vulkan_core.h>


RenderTargetHandle renderTargetCreateFromWindow(Window window, WindowRenderTargetConfig config){
	VkSurfaceKHR surface;
	if(!platformCreateVulkanSurface(renderer->instance.handle, &surface, window)){
		avError("Failed to create vulkan surface");
		return MAPPING_INVALID_ID;
	}
	return renderTargetCreateFromSurface(surface, config);
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

	// aquire strategy of renderTargets are derived from the link with the presentTarget (if present) otherwise use frameIndex

	//TODO: extent and create swapchain

}

void renderTargetDestroy(RenderTargetHandle handle){
	(void) handle;
}