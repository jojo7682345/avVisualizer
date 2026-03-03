#include "../renderer.h"
#include "containers/darray.h"
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "logging.h"
typedef struct Win32HandleInfo {
    HINSTANCE instance;
    HWND hwnd;
} Win32HandleInfo;

void vulkanPlatformGetRequiredExtensionNames(const char*** namesDarray) {
	darrayPush(*namesDarray, &"VK_KHR_win32_surface");
}

bool32 vulkanPlatformCreateSurface(void* rendererState, void* platformState, void* surface){
	VkInstance* instance = (VkInstance*)rendererState;
	Win32HandleInfo* handle = (Win32HandleInfo*)platformState;
	VkWin32SurfaceCreateInfoKHR createInfo = {0};
	createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	createInfo.hinstance = handle->instance;
	createInfo.hwnd = handle->hwnd;
	createInfo.flags = 0;
	if(!vkCreateWin32SurfaceKHR(*instance, &createInfo, NULL, surface)){
		avFatal("Unable to create surface");
		return false;
	}
	return true;

}