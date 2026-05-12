#ifdef __linux__
#include "core/platform.h"
#include "logging.h"

#include <AvUtils/avMemory.h>



#include <dlfcn.h>

#include "linux/wayland.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static bool32 isWayland(){
    const char *xdg_session_type = getenv("XDG_SESSION_TYPE");
    if (xdg_session_type && strcmp(xdg_session_type, "wayland") == 0) return true;

    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    if (wayland_display) return true;

    return false;
}

static bool32 isX11(){
    return getenv("DISPLAY") != NULL;
}

typedef struct PlatformBackend {
	bool32 initialized;
	void* api;
    bool8 (*startup)(uint64* memoryRequirement, void* state, PlatformConfig* config);
    void (*shutdown)(void* state);

    void (*getFramebufferSize)(uint32* w, uint32* h);
    bool8 (*pumpMessages)(void);

    void (*consoleWrite)(const char* msg, uint8 color);
    void (*consoleWriteError)(const char* msg, uint8 color);

    double (*getAbsoluteTime)(void);
    void (*sleep)(uint64 ms);
} PlatformBackend;

static PlatformBackend backend = {0};
static void* platform_state;









void registry_global_handler(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    printf("interface: '%s', version: %u, name: %u\n", interface, version, name);
}

void registry_global_remove_handler(
    void *data, struct wl_registry *registry, uint32_t name) {
    printf("removed: %u\n", name);
}

static bool32 createWaylandBackend(PlatformBackend* bkend){
	void *lib = dlopen("libwayland-client.so.0", RTLD_LAZY);
	if(lib==NULL){
		avError("Unable to load libwayland-client.so.0");
		return false;
	}
	WaylandAPI* api = avAllocate(sizeof(WaylandAPI), "");
	if(!loadWaylandSymbols(lib, api)){
		dlclose(lib);
		avFree(api);
		return false;
	}
	bkend->api = api;
	
	
	

	api->display = wl_display_connect(NULL);
	api->registry = wl_display_get_registry(api->display);
	struct wl_registry_listener registry_listener = {
        .global = registry_global_handler,
        .global_remove = registry_global_remove_handler
    };
	wl_registry_add_listener(api->registry, &registry_listener, NULL);

	while(1){
		wl_display_dispatch(api->display);
	}

	return true;
}

static void destroyWaylandBackend(){
	if(!backend.initialized) return;
	WaylandAPI* api = backend.api;




	dlclose(api->lib);
	avFree(api);
	backend.initialized = false;
}

static bool32 createX11Backend(PlatformBackend* bkend){

}


bool8 platformSystemStartup(uint64* memoryRequirement, void* state, void* config) {
	if(backend.initialized==false){
		if (isWayland()) {
			if(!createWaylandBackend(&backend)){
				avError("Unable to create wayland backend");
				return false;
			}
		} else if (isX11()) {
			if(!createX11Backend(&backend)){
				avError("Unable to create X11 backend");
				return false;
			}
		} else {
			return false;
		}
	}
    return backend.startup(memoryRequirement, state, config);
}


void platformSystemShutdown(void* platformState){

}

bool8 platformPumpMessages(void){
	return false;
}

double platformGetAbsoluteTime(void){
	
}

#endif
