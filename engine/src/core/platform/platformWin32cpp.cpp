#include <windows.h>
#include "imgui.h"
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_vulkan.h"


extern "C" {
    void initPlatformCpp(HWND hwnd);
    LRESULT platformCppProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
}

int Platform_CreateVkSurface(ImGuiViewport* vp, ImU64 vk_inst, const void* vk_allocators, ImU64* out_vk_surface) {
    VkInstance instance = (VkInstance)vk_inst;
    const VkAllocationCallbacks* allocator =
    (const VkAllocationCallbacks*)vk_allocators;

    HWND hwnd = (HWND)vp->PlatformHandleRaw;

    VkWin32SurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.hinstance = GetModuleHandle(nullptr);
    info.hwnd = hwnd;

    VkSurfaceKHR surface;

    VkResult err = vkCreateWin32SurfaceKHR(
    instance,
    &info,
    allocator,
    &surface
    );

    if (err != VK_SUCCESS)
    return 1;

    *out_vk_surface = (ImU64)surface;
    return 0;
}

void initPlatformCpp(HWND hwnd){
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Platform_CreateVkSurface = Platform_CreateVkSurface;

    ImGui::StyleColorsDark();

    io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 24.0f);
    ImGui_ImplWin32_Init(hwnd);

}

LRESULT platformCppProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam){
    if(ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)){
        return true;
    }
    return false;
}