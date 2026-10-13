#include "core/platform.h"
#include "logging.h"
#include "core/utils/math.h"
#include <AvUtils/avMemory.h>
#include <stdalign.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include "wayland-backend-internal.h"
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>
#include "containers/darray.h"

typedef struct WaylandBackendState {
    WaylandDisplay display;
} WaylandBackendState;
static WaylandBackendState* state;

bool8 waylandBackend_startup(uint64* memoryRequirements, void* statePtr, void* configPtr){
    // TODO: implement
    *memoryRequirements = sizeof(WaylandBackendState) + _Alignof(WaylandBackendState);
    if(statePtr==NULL) return true;
    PlatformConfig* config = (PlatformConfig*)configPtr;
	if(config == NULL) return false;

    state = alignPtr(statePtr, _Alignof(WaylandBackendState));
    avMemset(state, 0, sizeof(WaylandBackendState));
    
    avInfo("Starting wayland backend");

    if(!waylandDisplayInitialize(&state->display)) return false;

	return true;
}

void waylandBackend_shutdown(void* statePtr){
    (void)statePtr;
    avInfo("Shutting down wayland backend");
	waylandDisplayShutdown(&state->display);
    state = NULL;
}

void waylandBackend_getVulkanRequiredExtensions(const char*** namesDarray){
	darrayPush(*namesDarray,(const char*) "VK_KHR_wayland_surface");
}
bool32 waylandBackend_createVulkanSurface(void* instance, void* surface, Window window){
	VkWaylandSurfaceCreateInfoKHR info = {
		.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
		.surface = window->surface,
		.display = state->display.display,
	};
	if(vkCreateWaylandSurfaceKHR(instance, &info, NULL, surface) != VK_SUCCESS){
		avError("Failed to create wayland surface");
		return false;
	}
	return true;
}

Window waylandBackend_createWindow(WindowConfig config){
    if(state==NULL)return NULL;
    return waylandWindowCreate(&state->display, config);
	return NULL;
}

void waylandBackend_destroyWindow(Window window){
	waylandWindowDestroy(window);
}

void waylandBackend_getFramebufferSize(uint32* w, uint32* h){
	// TODO: implement
}

bool8 waylandBackend_pumpMessages(){
    if(state==NULL) return false;
    return waylandDisplayPumpMessages(&state->display);
}