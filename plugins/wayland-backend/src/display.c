#include "wayland-backend-internal.h"
#include "generated/xdg-shell-client-protocol.h"

#include <string.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <errno.h>
#include "logging.h"
static void waylandDisplayHandlePing(void* data, struct xdg_wm_base* wmBase, uint32 serial){
    (void)data;
    xdg_wm_base_pong(wmBase, serial);
}

static const struct xdg_wm_base_listener wmBaseListener = {
    .ping = waylandDisplayHandlePing,
};

static void waylandDisplayHandleGlobal(void* data, struct wl_registry* registry, uint32 name, const char* interface, uint32 version){
    WaylandDisplay* display = data;
    if(strcmp(interface, wl_compositor_interface.name) ==0){
        uint32 bindVersion = version < 4 ? version : 4;
        display->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, bindVersion);
    }else if(strcmp(interface, xdg_wm_base_interface.name)==0){
        uint32 bindVersion = version < 1 ? version : 1;
        display->wmBase = wl_registry_bind(registry, name, &xdg_wm_base_interface, bindVersion);
        if(display->wmBase != NULL){
            xdg_wm_base_add_listener(display->wmBase, &wmBaseListener, display);
        }
    }else if(strcmp(interface, wl_shm_interface.name)==0){
        uint32 bindVersion = version < 1 ? version : 1;
        display->shm = wl_registry_bind(registry, name, &wl_shm_interface, bindVersion);
    }
}

static void waylandDisplayHandleGlobalRemove(void* data, struct wl_registry* registry, uint32 name){
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registryListener = {
    .global = waylandDisplayHandleGlobal,
    .global_remove = waylandDisplayHandleGlobalRemove,
};

bool32 waylandDisplayInitialize(WaylandDisplay* display){
    memset(display, 0, sizeof(*display));
    display->display = wl_display_connect(NULL);
    if(display->display == NULL) goto error;
    display->connected = true;
    display->registry = wl_display_get_registry(display->display);
    if(display->registry == NULL) goto error;
    if(wl_registry_add_listener(display->registry, &registryListener, display) < 0) goto error;
    if(wl_display_roundtrip(display->display)<0) goto error;
    if(display->compositor == NULL || display->wmBase == NULL || display->shm==NULL) goto error;
    display->initialized = true;
    return true;
error:
    waylandDisplayShutdown(display);
    return false;
}

void waylandDisplayShutdown(WaylandDisplay* display){
    if(display==NULL)return;
    if(display->shm != NULL) wl_shm_destroy(display->shm);
    if(display->wmBase != NULL) xdg_wm_base_destroy(display->wmBase);
    if(display->compositor != NULL) wl_compositor_destroy(display->compositor);
    if(display->registry != NULL) wl_registry_destroy(display->registry);
    if(display->display != NULL) wl_display_disconnect(display->display);
    memset(display, 0, sizeof(*display));
}

bool8 waylandDisplayPumpMessages(WaylandDisplay* display){
    while(wl_display_prepare_read(display->display) != 0){
        wl_display_dispatch_pending(display->display);
    }
    if(wl_display_flush(display->display) == -1 && errno != EAGAIN){
        wl_display_cancel_read(display->display);
        avError("Failed to flush Wayland requests");
        return false;
    }
    if(wl_display_read_events(display->display)==-1){
        avError("Failed to read Wayland events");
        return false;
    }

    wl_display_dispatch_pending(display->display);
    return true;
}