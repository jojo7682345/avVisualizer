#pragma once
#include "defines.h"
#include "core/platform.h"

typedef struct Version {
    uint8 major;
    uint8 minor;
    uint8 patch;
} Version;

typedef struct  RendererConfig {
    const char* appName;
    const char* engineName;
    Version engineVersion;
    Version appVersion;
    bool8 enableValidation;
    void* platformState;
    Window mainWindow;
} RendererConfig;

bool8 rendererStartup(uint64* memoryRequirement, void* state, void* config);
void rendererShutdown(void* state);


typedef uint32 RenderTargetHandle;


typedef enum WindowSurfaceFormat {
    SURFACE_FORMAT_AUTO,
    SURFACE_FORMAT_SDR,
    SURFACE_FORMAT_HDR10,
} WindowSurfaceFormat;

typedef enum WindowPresentMode {
    WINDOW_PRESENT_MODE_IMMEDIATE,
    WINDOW_PRESENT_MODE_DOUBLE_BUFFERED, //VSYNC
    WINDOW_PRESENT_MODE_TRIPPLE_BUFFERED, // cooler VSYNC
} WindowPresentMode;
typedef struct WindowRenderTargetConfig {
    WindowSurfaceFormat format;
    WindowPresentMode presentMode;
    uint32 requestedWidth;
    uint32 requestedHeight;
    bool8 transferDestination;
} WindowRenderTargetConfig;

RenderTargetHandle renderTargetCreateFromWindow(Window window, WindowRenderTargetConfig config);
void renderTargetDestroy(RenderTargetHandle handle);
