#ifdef _WIN32
#include "imgui/imgui.h"
#include "imgui/imgui_impl_vulkan.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imstb_truetype.h"
extern "C" {
#include <vulkan/vulkan.h>
#include "defines.h"
void initImGuiRenderer();
extern VkCommandBuffer beginSingleTimeCommands();
extern void endSingleTimeCommands(VkCommandBuffer commandBuffer);
void ImGuiRendererDraw(VkCommandBuffer cmd);
void ImGuiRendererCleanup(VkDevice device);
void ImGuiRendererInit(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue, uint32 imageCount, uint32 minImageCount, uint32 graphicsQueueFamily, VkRenderPass renderPass);
void startFrame();
void endFrame();
}

static VkDescriptorPool imguiDescriptorPool;
static VkPipelineCache pipelineCache;

void ImGuiRendererInit(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue, uint32 imageCount, uint32 minImageCount, uint32 graphicsQueueFamily, VkRenderPass renderPass){
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;
    vkCreateDescriptorPool(device, &pool_info, NULL, &imguiDescriptorPool);
    
    VkPipelineCacheCreateInfo cacheInfo = {};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cacheInfo.initialDataSize = 0;
    cacheInfo.pInitialData = NULL;
    vkCreatePipelineCache(device, &cacheInfo, NULL, &pipelineCache);

    ImGui_ImplVulkan_InitInfo init_info = { 0 };
    init_info.Instance = instance;
    init_info.PhysicalDevice = physicalDevice;
    init_info.Device = device;
    init_info.QueueFamily = graphicsQueueFamily;
    init_info.Queue = graphicsQueue;
    init_info.PipelineCache = pipelineCache;
    init_info.DescriptorPool = imguiDescriptorPool;
    init_info.MinImageCount = minImageCount;
    init_info.ImageCount = imageCount;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.PipelineInfoMain.RenderPass = renderPass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoForViewports.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.PipelineInfoForViewports.RenderPass = renderPass;
    init_info.PipelineInfoForViewports.Subpass = 0;
    init_info.CheckVkResultFn = [](VkResult err){ assert(err == VK_SUCCESS); };

    ImGui_ImplVulkan_Init(&init_info);



    // VkCommandBuffer cmd = beginSingleTimeCommands();
    // ImGui_ImplVulkan_CreateFontsTexture(cmd);
    // endSingleTimeCommands(cmd);
    // ImGui_ImplVulkan_DestroyFontUploadObjects();
}

void ImGuiRendererDraw(VkCommandBuffer cmd){
    ImDrawData* draw_data = ImGui::GetDrawData();
    ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
}


void ImGuiRendererCleanup(VkDevice device){
    vkDeviceWaitIdle(device);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(device, imguiDescriptorPool, NULL);
    vkDestroyPipelineCache(device, pipelineCache, NULL);
}

void startFrame(){
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void endFrame(){
    ImGui::Render();
}

#endif