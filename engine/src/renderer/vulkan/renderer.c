#include "renderer.h"
#include "../rendererCore.h"

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

	Window bootstrapWindow = platformCreateWindow((WindowConfig){
		.ctx = NULL,
		.width = 100,
		.height = 100,
		.title = config->engineName,
		.flags = {
			.hidden = true,
			.disableEvents = true,
		},
	});
	VkSurfaceKHR mainSurface = VK_NULL_HANDLE;
	if(!platformCreateVulkanSurface(renderer->instance.handle, &mainSurface, bootstrapWindow)){
		avError("Failed to create surface");
		goto cleanup;
	}

	GpuRequirements gpuReq = {
		.requirePresentQueue = true,
		.requireGraphicsQueue = true,
		.requireTransferQueue = true,
		.requiredExtensionCount = 1,
		.requiredExtensions = (const char*[]){
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		},
		.surface = mainSurface,
	};
	GpuPreferences gpuPref = {
		.type = GPU_TYPE_ANY,
		.mainSurface = mainSurface,
	};
	if(!querryGpus(renderer, gpuReq, gpuPref)){
		avError("Failed to find suitable GPU's");
		goto cleanup;
	}
	//activate main gpu
	if(!gpuActivate(renderer, renderer->gpu)){
		avError("Failed to connect to gpu");
		goto cleanup;
	}

	vkDestroySurfaceKHR(renderer->instance.handle, mainSurface, NULL);
	platformDestroyWindow(bootstrapWindow);
	mainSurface = VK_NULL_HANDLE;
	bootstrapWindow = NULL;

	return true;
cleanup:
	if(mainSurface != VK_NULL_HANDLE) vkDestroySurfaceKHR(renderer->instance.handle, mainSurface, NULL);
	if(bootstrapWindow!=NULL)platformDestroyWindow(bootstrapWindow);

	//TODO: add cleanup of other things
	return false;
}



void rendererShutdown(void* state){
	(void) state;
	cleanupGpus(renderer);
	instanceDestroy(renderer);
}