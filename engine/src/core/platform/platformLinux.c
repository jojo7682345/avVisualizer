#ifdef __linux__
#include "core/platform.h"
#include "logging.h"

#include <AvUtils/avMemory.h>


#include <wayland-client.h>
#include <dlfcn.h>

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

#define WAYLAND_FUNCS \
    WL(display_connect) \
    WL(display_disconnect) \
    WL(display_get_registry) \
    WL(registry_add_listener) \
	WL(display_dispatch)\


typedef struct WaylandAPI {
	void* lib;
	#define WL(fn) typeof(wl_##fn)* fn;
	WAYLAND_FUNCS
	#undef WL

	struct wl_display* display;
	struct wl_registry* registry;

} WaylandAPI;

static void loadSym(void* lib, void** func, const char* name){
	*func = dlsym(lib, name);
	if(*func == NULL){
		avFatal("Missing symbol %s", name);
	}
}

void registry_global_handler(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    printf("interface: '%s', version: %u, name: %u\n", interface, version, name);
}

void registry_global_remove_handler(
    void *data, struct wl_registry *registry, uint32_t name) {
    printf("removed: %u\n", name);
}

static PlatformBackend createWaylandBackend(){
	void *lib = dlopen("libwayland-client.so.0", RTLD_LAZY);
	PlatformBackend bkend = {0};
	WaylandAPI* api = avAllocate(sizeof(WaylandAPI), "");
	bkend.api = api;
	api->lib = lib;
	#define WL(fn) loadSym(api->lib, (void**)&api->fn, "wl_"#fn);
	WAYLAND_FUNCS
	#undef WL

	api->display = api->display_connect(NULL);
	api->registry = api->display_get_registry(api->display);
	struct wl_registry_listener registry_listener = {
        .global = registry_global_handler,
        .global_remove = registry_global_remove_handler
    };
	api->registry_add_listener(api->registry, &registry_listener, NULL);

	while(1){
		api->display_dispatch(api->display);
	}

	return bkend;
}

static void destroyWaylandBackend(){
	if(!backend.initialized) return;
	WaylandAPI* api = backend.api;




	dlclose(api->lib);
	avFree(api);
	backend.initialized = false;
}

static PlatformBackend createX11Backend(void){

}


bool8 platformSystemStartup(uint64* memoryRequirement, void* state, void* config) {
	if(backend.initialized==false){
		if (isWayland()) {
			backend = createWaylandBackend();
		} else if (isX11()) {
			backend = createX11Backend();
		} else {
			return false;
		}
	}
    return backend.startup(memoryRequirement, state, config);
}





#endif
