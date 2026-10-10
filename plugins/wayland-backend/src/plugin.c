#include "core/platform.h"
#include "logging.h"
#include "core/utils/math.h"
#include <AvUtils/avMemory.h>
#include <stdalign.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include "wayland-backend-internal.h"

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
	// TODO: implement
}
bool32 waylandBackend_createVulkanSurface(void* instance, void* surface, Window window){
	// TODO: implement
    return false;
}

Window waylandBackend_createWindow(uint32 x, uint32 y, uint32 width, uint32 height, const char* name){
	(void)x;
    (void)y;
    if(state==NULL)return NULL;
    return waylandWindowCreate(&state->display, width, height, name);
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