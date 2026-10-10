#include "wayland-backend-internal.h"
#include "generated/xdg-shell-client-protocol.h"
#include "core/platform.h"

#include <AvUtils/avMemory.h>
#include <stdlib.h>
#include <string.h>
#define _GNU_SOURCE
#define __USE_GNU
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <wayland-client-protocol.h>

#include "core/systems/event.h"
#include "engine.h"

static struct wl_buffer* createSolidBuffer(struct wl_shm* shm, uint32 width, uint32 height, uint32 color){
    if(shm==NULL || width==0 || height==0 ) return NULL;
    if(width > INT32_MAX  / 4 || height > INT32_MAX / (width * 4)) return NULL;

    int32 stride = (int32)(width*4);
    int32 size = stride * (int32)height;
    int fd = memfd_create("wayland-test-buffer", MFD_CLOEXEC);
    if(fd < 0) return NULL;
    if(ftruncate(fd, size) < 0) {
        close(fd);
        return NULL;
    }

    void* mapping = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(mapping == MAP_FAILED){
        close(fd);
        return NULL;
    }

    uint32* pixels = mapping;
    for(size_t i = 0; i < (size_t)width * height; i++){
        pixels[i] = color;
    }
    struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, size);
    close(fd);
    if(pool == NULL){
        munmap(mapping, (size_t)size);
        return NULL;
    }

    struct wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, (int32)width, (int32)height, (int32)stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    munmap(mapping, (size_t)size);
    return buffer;
}

static bool8 waylandWindowShowBlankBuffer(Window window){
    uint32 width = window->logicalWidth > 0 ? (uint32)window->logicalWidth : window->requestedPixelWidth;
    uint32 height = window->logicalHeight > 0 ? (uint32)window->logicalHeight : window->requestedPixelHeight;
    if(width == 0 || height == 0) return false;

    window->testBuffer = createSolidBuffer(window->display->shm, width, height, 0x00202020);
    if(window->testBuffer == NULL) return false;

    wl_surface_attach(window->surface, window->testBuffer, 0, 0);
    wl_surface_damage(window->surface, 0, 0, (int32)width, (int32)height);
    wl_surface_commit(window->surface);
    return true;
}

static void handleTopLevelConfigure(void* data, struct xdg_toplevel* toplevel, int32 width, int32 height, struct wl_array* states){
    (void)toplevel;
    (void)states;
    Window window = data;
    if(width > 0) window->logicalWidth = width;
    if(height > 0) window->logicalHeight = height;
}

static void handleTopLevelClose(void* data, struct xdg_toplevel* toplevel){
    (void)toplevel;
    Window window = data;
    window->shouldClose = true;

    EventContext eventData = {};
    eventFire(EVENT(EVENT_CODE_APPLICATION_QUIT, 0, eventData));
}

static const struct xdg_toplevel_listener toplevelListener = {
    .configure = handleTopLevelConfigure,
    .close = handleTopLevelClose,
};

static void handleSurfaceConfigure(void* data, struct xdg_surface* surface, uint32 serial){
    Window window = data;
    xdg_surface_ack_configure(surface, serial);
    window->configured = true;
}

static const struct xdg_surface_listener surfaceListener = {
    .configure = handleSurfaceConfigure,
};


Window waylandWindowCreate(WaylandDisplay* display, uint32 width, uint32 height, const char* title){
    if(display==NULL || !display->initialized) return NULL;
    Window window = avAllocate(sizeof(*window), "allocating window");
    if(window == NULL) return NULL;
    avMemset(window, 0, sizeof(struct Window));
    window->display = display;
    window->requestedPixelWidth = width;
    window->requestedPixelHeight = height;
    window->surface = wl_compositor_create_surface(display->compositor);
    if(window->surface == NULL) goto error;
    window->xdgSurface = xdg_wm_base_get_xdg_surface(display->wmBase, window->surface);
    if(window->xdgSurface==NULL) goto error;
    if(xdg_surface_add_listener(window->xdgSurface, &surfaceListener, window) < 0) goto error;
    window->toplevel = xdg_surface_get_toplevel(window->xdgSurface);
    if(window->toplevel == NULL) goto error;
    if(xdg_toplevel_add_listener(window->toplevel, &toplevelListener, window) < 0) goto error;
    xdg_toplevel_set_title(window->toplevel, title != NULL ? title : "-");
    wl_surface_commit(window->surface);
    while(!window->configured){
        if(wl_display_roundtrip(display->display) < 0) goto error;
    }
    if(!waylandWindowShowBlankBuffer(window)) goto error;
    return window;
error:
    waylandWindowDestroy(window);
    return NULL;
}

void waylandWindowDestroy(Window window){
    if(window == NULL) return;
    if(window->testBuffer != NULL) wl_buffer_destroy(window->testBuffer);
    if(window->toplevel != NULL) xdg_toplevel_destroy(window->toplevel);
    if(window->xdgSurface != NULL) xdg_surface_destroy(window->xdgSurface);
    if(window->surface != NULL) wl_surface_destroy(window->surface);
    avFree(window);
}