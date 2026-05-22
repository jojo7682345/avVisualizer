#include "wayland.h"

#include <unistd.h>
//#include <stdlib.h>
#include <string.h>

#include <dlfcn.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>


#include "logging.h"

#include <AvUtils/avMemory.h>

#define WAYLAND_FUNCS \
    WL(display_connect) \
    WL(display_disconnect) \
	WL(proxy_add_listener)\
	WL(display_dispatch)\
	WL(proxy_get_version)\
	WL(proxy_marshal_flags)\
	WL(registry_interface)\
	WL(compositor_interface)\
	WL(shm_interface)\
	WL(display_roundtrip)\
	WL(compositor_create_surface)\


typedef struct WaylandAPI {
	void* lib;
	
	struct wl_display* display;
	struct wl_registry* registry;
	struct wl_compositor* compositor;
	struct wl_shm* shm;
	struct xdg_wm_base* xdg_wm_base;

	struct wl_surface* surface;
	struct xdg_surface* xdg_surface;
	struct xdg_toplevel* xdg_toplevel;

	#define WL(fn) typeof(wl_##fn)* fn;
	WAYLAND_FUNCS
	#undef WL

} WaylandAPI;


static inline struct wl_registry * display_get_registry(WaylandAPI* api, struct wl_display *wl_display) {
	struct wl_proxy *registry;
	registry = api->proxy_marshal_flags((struct wl_proxy *) wl_display, WL_DISPLAY_GET_REGISTRY, api->registry_interface, api->proxy_get_version((struct wl_proxy *) wl_display), 0, NULL);
	return (struct wl_registry *) registry;
}
static inline int registry_add_listener(WaylandAPI* api, struct wl_registry *wl_registry, const struct wl_registry_listener *listener, void *data) {
	return api->proxy_add_listener((struct wl_proxy *) wl_registry, (void (**)(void)) listener, data);
}
static inline void *registry_bind(WaylandAPI* api, struct wl_registry *wl_registry, uint32_t name, const struct wl_interface *interface, uint32_t version){
	struct wl_proxy *id;
	id = api->proxy_marshal_flags((struct wl_proxy *) wl_registry, WL_REGISTRY_BIND, interface, version, 0, name, interface->name, version, NULL);
	return (void *) id;
}


static inline bool32 loadSym(void* lib, void** func, const char* name){
	*func = dlsym(lib, name);
	if(*func == NULL){
		avError("Missing symbol %s", name);
		return false;
	}
	return true;
}

static inline bool32 loadWaylandSymbols(void* lib, WaylandAPI* api){
	api->lib = lib;
	#define WL(fn) if(!loadSym(lib, (void**)&api->fn, "wl_"#fn)) return false;
	WAYLAND_FUNCS
	#undef WL
	return true;
}

void destroyWaylandBackend(PlatformBackend* backend){
	if(!backend->initialized) return;
	WaylandAPI* api = backend->api;


	dlclose(api->lib);
	avFree(api);
	backend->initialized = false;
}









void registry_global_handler(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    (void)version;
	WaylandAPI* api = (WaylandAPI*)data;
	if(strcmp(interface, "wl_compositor")==0){
		api->compositor = registry_bind(api, registry, name, api->compositor_interface, 3);
	}else if(strcmp(interface, "wl_shm")==0){
		api->shm = registry_bind(api, registry, name, api->shm_interface, 1);
	}else if(strcmp(interface, "xdg_wm_base")){

	}
}

void registry_global_remove_handler(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data;
	(void)registry;
	(void)name;
}

const struct wl_registry_listener registry_listener = {
    .global = registry_global_handler,
    .global_remove = registry_global_remove_handler
};




static bool32 initializeWayland(WaylandAPI* api){
	api->display = api->display_connect(NULL);
	api->registry = display_get_registry(api, api->display);
	registry_add_listener(api, api->registry, &registry_listener, api);

	api->display_roundtrip(api->display);

	api->surface = api->compositor_create_surface(api->compositor);

	if(api->compositor && api->shm){
		avDebug("Received all wayland globals");
	}else{
		avError("Some required wayland globals not available");
		return false;
	}


	while(1){
		api->display_dispatch(api->display);
	}
}

bool32 createWaylandBackend(PlatformBackend* bkend){
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
	
	if(!initializeWayland(api)){
		dlclose(lib);
		avFree(api);
		return false;
	}
	

	

	return true;
}