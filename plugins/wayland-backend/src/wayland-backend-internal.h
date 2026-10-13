#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include "core/platform.h"

typedef struct WaylandDisplay {
    struct wl_display* display;
    struct wl_registry* registry;
    struct wl_compositor* compositor;
    struct xdg_wm_base* wmBase;

    struct wl_shm* shm;

    bool32 connected;
    bool32 initialized;
} WaylandDisplay;


typedef struct Window {
    WaylandDisplay* display;

    struct wl_surface* surface;
    struct xdg_surface* xdgSurface;
    struct xdg_toplevel* toplevel;

    struct wl_buffer* testBuffer;

    uint32 requestedPixelWidth;
    uint32 requestedPixelHeight;

    uint32 logicalWidth;
    uint32 logicalHeight;

    bool32 configured;
    bool32 shouldClose;
    bool32 resizePending;

    WindowFlags flags;
    void* ctx;
} WaylandWindow;

bool32 waylandDisplayInitialize(WaylandDisplay* display);
void waylandDisplayShutdown(WaylandDisplay* display);
bool8 waylandDisplayPumpMessages(WaylandDisplay* display);

Window waylandWindowCreate(WaylandDisplay* display, WindowConfig config);
void waylandWindowDestroy(Window window);