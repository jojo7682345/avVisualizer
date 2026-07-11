#include "renderer.h"

#include "containers/darray.h"
#include "core/platform.h"
#include "instance.h"
#include "gpu.h"
#include <vulkan/vulkan_core.h>



bool32 rendererCreate(Renderer* renderer){
	InstanceCreateInfo instanceInfo = {0};
	instanceInfo.applicationName = "TBD";
	instanceInfo.applicationVersion = 0;
	instanceInfo.engineName = "TDB";
	instanceInfo.engineVersion = 0;
	instanceInfo.enableValidation = false;
	const char** extensions = darrayCreate(const char*);
	darrayPush(extensions, (const char*)VK_KHR_SURFACE_EXTENSION_NAME);
	vulkanPlatformGetRequiredExtensionNames(&extensions);
	instanceInfo.extensionCount = darrayLength(extensions);
	instanceInfo.extensions = extensions;
	if(!instanceCreate(&instanceInfo, renderer)){
		avError("Failed to create instance");
		return false;
	}
	darrayDestroy(extensions);

	vulkanPlatformCreateSurface(renderer->instance.handle, &renderer->swapchain.surface);

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
	};
	Gpu selectedGpu = {0};
	if(!gpuSelect(renderer, gpuReq, gpuPref, &selectedGpu)){
		avError("Failed to select gpu");
		return false;
	}
	if(!gpuConnect(renderer, gpuReq, &selectedGpu)){
		avError("Failed to connect to gpu");
		return false;
	}


	return true;
}

void rendererDestroy(Renderer *renderer){
	gpuDisconnect(renderer);
	vkDestroySurfaceKHR(renderer->instance.handle, renderer->swapchain.surface, NULL);
	instanceDestroy(renderer);

}