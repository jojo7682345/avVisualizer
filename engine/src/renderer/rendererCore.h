#pragma once
#include "core/platform.h"
#include "renderer.h"
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

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
} RendererConfig;

bool8 rendererStartup(uint64* memoryRequirement, void* state, void* config);
void rendererShutdown(void* state);

typedef enum WindowSurfaceFormat {
    SURFACE_FORMAT_AUTO,
    SURFACE_FORMAT_SDR,
    SURFACE_FORMAT_HDR10,
} WindowSurfaceFormat;

typedef enum WindowPresentMode {
    WINDOW_PRESENT_MODE_IMMEDIATE,
    WINDOW_PRESENT_MODE_DOUBLE_BUFFERED, // VSYNC
    WINDOW_PRESENT_MODE_TRIPPLE_BUFFERED, // cooler VSYNC
} WindowPresentMode;
typedef struct WindowRenderTargetConfig {
    WindowSurfaceFormat format;
    WindowPresentMode presentMode;
    uint32 requestedWidth;
    uint32 requestedHeight;
    bool8 transferDestination;
} WindowRenderTargetConfig;

typedef enum ImageRenderTargetPersistType {
    IMAGE_RENDER_TARGET_PERSIST_UNTIL_PRESENTED, // multiple images in flight, discarded after present
    IMAGE_RENDER_TARGET_PERSIST_ACROSS_FRAMES, // multiple images in flight
    IMAGE_RENDER_TARGET_DISCARD_AFTER_USE, // one image in flight
} ImageRenderTargetPersistType;
typedef struct ImageRenderTargetFlags {
    bool32 color : 1;
    bool32 depth : 1;
    bool32 stencil : 1;
    // transient means the image is used and consumed in one frame and can immediately be reused next frame
    // a transient image will only have one image, a non transient image will have multiple, that are the same as the images in flight
    
    bool32 clearedColor : 1; // clear the color each frame
    bool32 clearedDepth : 1; // clear the depth each frame
    bool32 clearedStencil : 1; // clear the stencil each frame
    bool32 startCleared : 1; // filled with clear values initially
} ImageRenderTargetFlags;
typedef struct ImageRenderTargetConfig {
    uint32 width;
    uint32 height;
    VkFormat format;
    VkFormat depthStencilFormat; // use findSupportedDepthFormat for selecting format
    VkImageUsageFlags usage;
    VkSampleCountFlagBits samples;

    ImageRenderTargetPersistType persistType;
    ImageRenderTargetFlags flags;

    VkClearColorValue clearColor; // optional
    VkClearDepthStencilValue clearDepthStencil; // optional
} ImageRenderTargetConfig;

RenderTargetHandle renderTargetCreateImage(ImageRenderTargetConfig config);
RenderTargetHandle renderTargetCreateFromWindow(Window window, WindowRenderTargetConfig config);
void renderTargetDestroy(RenderTargetHandle handle);