#include "renderer.h"
#include "../renderer.h"

#include "containers/darray.h"
#include "core/platform.h"
#include "core/utils/math.h"
#include "instance.h"
#include "gpu.h"
#include <AvUtils/avMemory.h>
#include <vulkan/vulkan_core.h>

Renderer* renderer;

static uint32 toVkVersion(Version version){
	return VK_MAKE_VERSION(version.major, version.minor, version.patch);
}

bool8 rendererStartup(uint64* memoryRequirement, void* statePtr, void* configPtr){
 	*memoryRequirement = sizeof(Renderer) + _Alignof(Renderer);
    if(statePtr==NULL) return true;
    renderer = alignPtr(statePtr, _Alignof(Renderer));
	avMemset(renderer, 0, sizeof(Renderer));
    
    RendererConfig* config = (RendererConfig*)configPtr;

	InstanceCreateInfo instanceInfo = {0};
	instanceInfo.applicationName = config->appName;
	instanceInfo.applicationVersion = toVkVersion(config->appVersion);
	instanceInfo.engineName = config->engineName;
	instanceInfo.engineVersion = toVkVersion(config->engineVersion);
	instanceInfo.enableValidation = false;
	const char** extensions = darrayCreate(const char*);
	darrayPush(extensions, (const char*)VK_KHR_SURFACE_EXTENSION_NAME);
	platformGetVulkanRequiredExtensionNames(&extensions);
	instanceInfo.extensionCount = darrayLength(extensions);
	instanceInfo.extensions = extensions;
	if(!instanceCreate(&instanceInfo, renderer)){
		avError("Failed to create instance");
		return false;
	}
	darrayDestroy(extensions);


	VkSurfaceKHR mainSurface;
	if(!platformCreateVulkanSurface(renderer->instance.handle, &mainSurface, config->mainWindow)){
		avError("Failed to create surface");
		return false;
	}

	GpuRequirements gpuReq = {
		.requirePresentQueue = true,
		.requireGraphicsQueue = true,
		.requireTransferQueue = true,
		.requiredExtensionCount = 1,
		.requiredExtensions = (const char*[]){
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		},
	};
	GpuPreferences gpuPref = {
		.type = GPU_TYPE_ANY,
		.mainSurface = mainSurface,
	};
	if(!querryGpus(renderer, gpuReq, gpuPref)){
		avError("Failed to find suitable GPU's");
		return false;
	}
	//activate main gpu
	if(!gpuActivate(renderer, renderer->gpu)){
		avError("Failed to connect to gpu");
		return false;
	}


	return true;
}



void rendererShutdown(void* state){
	(void) state;
	cleanupGpus(renderer);
	instanceDestroy(renderer);
}