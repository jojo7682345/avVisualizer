#include "defines.h"
#include <dlfcn.h>
#include <wayland-client.h>
#include "logging.h"

#define WAYLAND_FUNCS \
    WL(display_connect) \
    WL(display_disconnect) \
    WL(registry_add_listener) \
	WL(display_dispatch)\
	WL(proxy_get_version)\
	WL(proxy_marshal_flags)\
	WL(registry_interface)\

typedef struct WaylandAPI {
	void* lib;
	
	struct wl_display* display;
	struct wl_registry* registry;
	#define WL(fn) typeof(wl_##fn)* fn;
	WAYLAND_FUNCS
	#undef WL

} WaylandAPI;




static inline bool32 loadSym(void* lib, void** func, const char* name){
	*func = dlsym(lib, name);
	if(*func == NULL){
		avError("Missing symbol %s", name);
		return false;
	}
}

static inline bool32 loadWaylandSymbols(void* lib, WaylandAPI* api){
	api->lib = lib;
	#define WL(fn) if(!loadSym(lib, (void**)&api->fn, "wl_"#fn)) return false;
	WAYLAND_FUNCS
	#undef WL
}