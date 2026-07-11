#include "../wayland-backend.h"
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_wayland.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include "core/platform.h"
#include "logging.h"
#include "xdg-shell-client-protocol.h"

#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <containers/darray.h>

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>

#include <AvUtils/avMemory.h>
#include "core/utils/math.h"

typedef struct WaylandState {
	struct wl_display* display; 
	struct wl_registry* registry;
	struct wl_compositor* compositor;
	struct xdg_wm_base* shell;
	struct wl_surface* surface;
	struct xdg_surface* shellSurface;
	struct xdg_toplevel* toplevel;

	struct wl_seat* seat;
	struct wl_keyboard* keyboard;

	int quit;
	int readyToResize;
	int resize;
	int newWidth;
	int newHeight;
} WaylandState;

static WaylandState* state;

static void handleShellPing(void* data, struct xdg_wm_base* shell, uint32_t serial) {
    (void)data;
	xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener shellListener = {
    .ping = handleShellPing
};

static void keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard, uint32 format, int fd, uint32 size) {
    // Usually handled with libxkbcommon
	(void)data;
	(void)wl_keyboard;
	(void)format;
	(void)fd;
	(void)size;
}

static void keyboard_enter(void *data, struct wl_keyboard *wl_keyboard, uint32 serial, struct wl_surface *surface, struct wl_array *keys){
    (void)data;
	(void)wl_keyboard;
	(void)serial;
	(void)surface;
	(void)keys;
	avDebug("Keyboard focus entered\n");
}

static void keyboard_leave(void *data, struct wl_keyboard *wl_keyboard, uint32 serial, struct wl_surface *surface){
	(void)data;
	(void)wl_keyboard;
	(void)serial;
	(void)surface;
    avDebug("Keyboard focus left\n");
}

static void keyboard_key(void *data, struct wl_keyboard *wl_keyboard, uint32 serial, uint32 time, uint32 key, uint32 state) {
	(void)data;
	(void)wl_keyboard;
	(void)serial;
	(void)time;
    avDebug("Key event: key=%u state=%u\n", key, state);

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        avDebug("Pressed\n");
    }
}

static void keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard, uint32 serial, uint32 mods_depressed, uint32 mods_latched, uint32 mods_locked, uint32 group){
	(void)data;
	(void)wl_keyboard;
	(void)serial;
	(void)mods_depressed;
	(void)mods_latched;
	(void)mods_locked;
	(void)group;
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard, int32 rate, int32 delay) {
	(void)data;
	(void)wl_keyboard;
	(void)rate;
	(void)delay;

}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

static void seat_capabilities(void *data, struct wl_seat *wl_seat, enum wl_seat_capability caps){
	(void)data;
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !state->keyboard) {
        state->keyboard = wl_seat_get_keyboard(wl_seat);
        wl_keyboard_add_listener(state->keyboard, &keyboard_listener, NULL);
    }
}
static void seat_name(void *data, struct wl_seat *wl_seat, const char *name){
	(void)data;
	(void)wl_seat;
    avDebug("Seat: %s\n", name);
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

void registry_global_handler (void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
	(void)data;
	(void)version;
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->compositor = wl_registry_bind(registry, name,
            &wl_compositor_interface, 3);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0){
		state->shell = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(state->shell, &shellListener, NULL);
	}else if (strcmp(interface, wl_seat_interface.name) == 0) {
        state->seat = wl_registry_bind( registry, name, &wl_seat_interface, 1);
        wl_seat_add_listener(state->seat, &seat_listener, NULL);
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



static void handleShellSurfaceConfigure(void* data, struct xdg_surface* shellSurface, uint32_t serial) {
    (void)data;
	xdg_surface_ack_configure(shellSurface, serial);

    if (state->resize) {
        state->readyToResize = 1;
    }
}

static const struct xdg_surface_listener shellSurfaceListener = {
    .configure = handleShellSurfaceConfigure
};

static void handleToplevelConfigure(void* data, struct xdg_toplevel* toplevel, int32_t width, int32_t height, struct wl_array* states){
    (void)data;
	(void)toplevel;
	(void)states;
	if (width != 0 && height != 0) {
        state->resize = 1;
        state->newWidth = width;
        state->newHeight = height;
    }
}

static void handleToplevelClose(void* data, struct xdg_toplevel* toplevel) {
	(void)data;
	(void)toplevel;
    state->quit = 1;
}

static const struct xdg_toplevel_listener toplevelListener = {
    .configure = handleToplevelConfigure,
    .close = handleToplevelClose
};


bool8 waylandBackendInitialize(uint64* memoryRequirements, void* statePtr, void* configPtr){
	*memoryRequirements = sizeof(WaylandState) + _Alignof(WaylandState);
	if(statePtr==NULL){
		return true;
	}
	PlatformConfig* config = (PlatformConfig*)configPtr;
	if(config ==NULL) return false;


	state = alignPtr((WaylandState*)statePtr, _Alignof(WaylandState));
	avMemset(state, 0, sizeof(WaylandState));

	state->display = wl_display_connect(NULL);
    state->registry = wl_display_get_registry(state->display);
	wl_registry_add_listener(state->registry, &registry_listener, NULL);
	
	wl_display_roundtrip(state->display);
	state->surface = wl_compositor_create_surface(state->compositor);
	state->shellSurface = xdg_wm_base_get_xdg_surface(state->shell, state->surface);
	xdg_surface_add_listener(state->shellSurface, &shellSurfaceListener, NULL);
	state->toplevel = xdg_surface_get_toplevel(state->shellSurface);
	xdg_toplevel_add_listener(state->toplevel, &toplevelListener, NULL);

	xdg_toplevel_set_title(state->toplevel, config->applicationName);
	xdg_toplevel_set_app_id(state->toplevel, config->applicationName);

	wl_surface_commit(state->surface);
	wl_display_roundtrip(state->display);
	wl_surface_commit(state->surface);

	return true;
}

void waylandBackendShutdown(void* statePtr){
	(void)statePtr;
	if(state->keyboard) wl_keyboard_destroy(state->keyboard);
	if(state->seat) wl_seat_destroy(state->seat);

	xdg_toplevel_destroy(state->toplevel);
	xdg_surface_destroy(state->shellSurface);
	wl_surface_destroy(state->surface);
	xdg_wm_base_destroy(state->shell);
	wl_compositor_destroy(state->compositor);
	wl_registry_destroy(state->registry);
	wl_display_disconnect(state->display);
}

void waylandBackendGetVulkanRequiredExtensions(const char*** namesDarray){
	
	darrayPush(*namesDarray,(const char*) "VK_KHR_wayland_surface");
}
bool32 waylandBackendCreateVulkanSurface(void* instance, void* surface){
	VkWaylandSurfaceCreateInfoKHR info = {
		.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
		.surface = state->surface,
		.display = state->display,
	};
	if(vkCreateWaylandSurfaceKHR(instance, &info, NULL, surface) != VK_SUCCESS){
		avError("Failed to create wayland surface");
		return false;
	}
	return true;
}