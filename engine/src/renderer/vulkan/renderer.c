#include "../renderer.h"
#include "defines.h"
#include "logging.h"
#include "platform/platform.h"
#include "utils/vector.h"
#include <AvUtils/avMath.h>
#include <vulkan/vulkan.h>
#include "containers/darray.h"
#include <AvUtils/avString.h>
#include <AvUtils/avMemory.h>
#include "utils/matrix.h"

typedef struct RendererState {
    VkInstance instance;
    VkDebugUtilsMessengerEXT* debugMessenger;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkImage* swapchainImages;
    VkImageView* swapchainImageViews;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;
    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;
    VkFramebuffer* framebuffers;
    VkCommandPool commandPool;
    VkCommandBuffer* commandBuffer;
    VkSemaphore* imageAvailableSemaphore;
    VkSemaphore* renderFinishedSemaphore;
    uint32 imageCount;
    VkFence* inFlightFence;
    VkDescriptorPool descriptorPool;
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet* descriptorSets;

    bool32 framebufferResized;
    bool32 minimized;
    
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;

    VkBuffer* uniformBuffers;
    VkDeviceMemory* uniformBuffersMemory;
    void** uniformBuffersMapped;
} RendererState;

struct RendererState* state;



static bool32 checkExtensionsPresent(uint32 requiredExtensionCount, const char** requiredExtensions, uint32 availableExtensionCount, VkExtensionProperties* availableExtensions){
    for(uint32 i = 0; i < requiredExtensionCount; i++){
        bool32 found = false;
        for(uint32 j = 0; j < availableExtensionCount; j++){
            if(avStringEquals(AV_CSTR(requiredExtensions[i]), AV_CSTR(availableExtensions[j].extensionName))){
                found = true;
                break;
            }
        }
        if(!found){
            avLog(AV_NO_SUPPORT, "vulkan extension %s not found", requiredExtensions[i]);
            return false;
        }
    }
    return true;
}

static bool32 checkLayersPresent(uint32 requiredLayerCount, const char** requiredLayers, uint32 availableLayerCount, VkLayerProperties* availableLayers){
    for(uint32 i = 0; i < requiredLayerCount; i++){
        bool32 found = false;
        for(uint32 j = 0; j < availableLayerCount; j++){
            if(avStringEquals(AV_CSTR(requiredLayers[i]), AV_CSTR(availableLayers[j].layerName))){
                found = true;
                break;
            }
        }
        if(!found){
            avLog(AV_VALIDATION_NOT_PRESENT, "vulkan validation layer %s not found", requiredLayers[i]);
            return false;
        }
    }
    return true;
}

static VkResult createDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
	PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
	if (func != NULL) {
		return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
	} else {
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}
}

static void destroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
	PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
	if (func != NULL) {
		func(instance, debugMessenger, pAllocator);
	}
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData) {

	AvValidationLevel level = { 0 };
	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
		level = AV_VALIDATION_LEVEL_VERBOSE;
	}
	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
		level = AV_VALIDATION_LEVEL_INFO;
	}
	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
		level = AV_VALIDATION_LEVEL_WARNINGS_AND_ERRORS;
	}
	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		level = AV_VALIDATION_LEVEL_ERRORS;
	}

	ValidationMessageType type = { 0 };
	if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
		type = VALIDATION_MESSAGE_TYPE_GENERAL;
	}
	if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
		type = VALIDATION_MESSAGE_TYPE_VALIDATION;
	}
	if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
		type = VALIDATION_MESSAGE_TYPE_PERFORMANCE;
	}

	logDeviceValidation("vulkan", level, type, pCallbackData->pMessage);

	return VK_FALSE;
}

static void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT* createInfo) {
	createInfo->flags = 0;
	createInfo->sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	createInfo->messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	createInfo->messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	createInfo->pfnUserCallback = debugCallback;
	createInfo->pUserData = 0;
	createInfo->pNext = 0;
}



typedef struct QueueFamilyIndices {
    uint32 graphicsFamily;
    uint32 presentFamily;
    uint8 completeMask;
}QueueFamilyIndices;
#define QUEUE_FAMILY_GRAPHICS_COMPLETE (1<<0)
#define QUEUE_FAMILY_PRESENT_COMPLETE (1<<1)
#define QUEUE_FAMILY_COMPLETE (QUEUE_FAMILY_GRAPHICS_COMPLETE | QUEUE_FAMILY_PRESENT_COMPLETE)

QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device){
    QueueFamilyIndices indices = {0};
    uint32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, NULL);
    VkQueueFamilyProperties* queueFamilies = darrayReserve(VkQueueFamilyProperties, queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies);

    for(uint32 i = 0; i < queueFamilyCount; i++){
        VkQueueFamilyProperties queueFamily = queueFamilies[i];
        if(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT){
            indices.graphicsFamily = i;
            indices.completeMask |= QUEUE_FAMILY_GRAPHICS_COMPLETE;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, state->surface, &presentSupport);
        if(presentSupport){
            indices.presentFamily = i;
            indices.completeMask |= QUEUE_FAMILY_PRESENT_COMPLETE;
        }

        if(indices.completeMask & QUEUE_FAMILY_COMPLETE == QUEUE_FAMILY_COMPLETE){
           break;
        }
    }
    darrayDestroy(queueFamilies);

    return indices;
}

const char* deviceExtensions[] ={
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};
const uint32 deviceExtensionCount = sizeof(deviceExtensions)/sizeof(const char*);


static bool32 checkDeviceExtensionSupport(VkPhysicalDevice device){
    uint32 extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, NULL);
    VkExtensionProperties* availableExtensions = darrayReserve(VkExtensionProperties, extensionCount);
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, availableExtensions);

    for(uint32 i = 0; i < deviceExtensionCount; i++){
        bool32 found = false;
        for(uint32 j = 0; j < extensionCount; j++){
            if(avStringEquals(AV_CSTR(deviceExtensions[i]), AV_CSTR(availableExtensions[j].extensionName))){
                found = true;
            }
        }
        if(!found){
            darrayDestroy(availableExtensions);
            return false;
        }
    }

    darrayDestroy(availableExtensions);
    return true;
}

typedef struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    VkSurfaceFormatKHR* formats;
    VkPresentModeKHR* presentModes;
} SwapChainSupportDetails;

static SwapChainSupportDetails querrySwapchainSupport(VkPhysicalDevice device){
    SwapChainSupportDetails details = {0};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, state->surface, &details.capabilities);
    uint32 formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, state->surface, &formatCount, 0);
    if(formatCount){
        details.formats = darrayReserve(VkSurfaceFormatKHR, formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, state->surface, &formatCount, details.formats);
    }

    uint32 presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, state->surface, &presentModeCount, NULL);
    if(presentModeCount){
        details.presentModes = darrayReserve(VkPresentModeKHR, presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, state->surface, &presentModeCount, details.presentModes);
    }
    return details;
}

static bool32 isDeviceSuitable(VkPhysicalDevice device){
    QueueFamilyIndices indices = findQueueFamilies(device);
    if(indices.completeMask & QUEUE_FAMILY_COMPLETE != QUEUE_FAMILY_COMPLETE){
        return false;
    }
    if(!checkDeviceExtensionSupport(device)){
        return false;
    }
    SwapChainSupportDetails swapChainSupport= querrySwapchainSupport(device);
    if(swapChainSupport.formats==0 || swapChainSupport.presentModes==0){
        darrayDestroy(swapChainSupport.formats);
        darrayDestroy(swapChainSupport.presentModes);
        return false;
    }
    darrayDestroy(swapChainSupport.formats);
    darrayDestroy(swapChainSupport.presentModes);
    return true;
}

static VkSurfaceFormatKHR chooseSwapSurfaceFormat(VkSurfaceFormatKHR* availableFormats){
    uint32 formatCount = darrayLength(availableFormats);
    for(uint32 i = 0; i < formatCount; i++){
        VkSurfaceFormatKHR format = availableFormats[i];
        if(format.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormats->colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR){
            return format;
        }
    }
    return availableFormats[0];
}

static VkPresentModeKHR chooseSwapPresentMode(VkPresentModeKHR* availablePresentModes){
    uint32 count = darrayLength(availablePresentModes);
    for(uint32 i = 0; i < count; i++){
        VkPresentModeKHR presentMode = availablePresentModes[i];
        if(presentMode == VK_PRESENT_MODE_MAILBOX_KHR){
            return presentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR capabilities){
    if(capabilities.currentExtent.width != (UINT32_MAX)){
        return capabilities.currentExtent;
    }else{
        uint32 width, height = 0;
        platformGetFramebufferSize(&width, &height);
        VkExtent2D swapchainExtent = {width, height};
        swapchainExtent.width = AV_CLAMP(swapchainExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        swapchainExtent.height = AV_CLAMP(swapchainExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        return swapchainExtent;
    }
}

#include "basic_shader.h"

VkShaderModule createShaderModule(const char* code, uint64 codeSize){
    VkShaderModuleCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = codeSize;
    createInfo.pCode = (const uint32*)code;

    VkShaderModule shaderModule;
    if(vkCreateShaderModule(state->device, &createInfo, NULL, &shaderModule)!=VK_SUCCESS){
        avFatal("unable to create shader module");
    }
    return shaderModule;
}

typedef struct Vertex {
    vec2 pos;
    vec3 color;
} Vertex;

const Vertex vertices[] =  {
    {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
    {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
    {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}
};

uint16 indices[] = {
    0, 1, 2, 2, 3, 0
};

typedef struct UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} UniformBufferObject;

void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32 imageIndex){
    VkCommandBufferBeginInfo beginInfo = {0};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = NULL;
    if(vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS){
        avFatal("failed to beign recodring command buffer");
        return;
    }

    VkRenderPassBeginInfo renderPassInfo = {0};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = state->renderPass;
    renderPassInfo.framebuffer = state->framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = (VkOffset2D){0,0};
    renderPassInfo.renderArea.extent = state->swapchainExtent;
    
    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, state->graphicsPipeline);

    VkViewport viewport = {0};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)(state->swapchainExtent.width);
    viewport.height = (float)(state->swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor = { 0 };
    scissor.offset = (VkOffset2D){0, 0};
    scissor.extent = state->swapchainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    VkBuffer vertexBuffers[] = {state->vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, state->indexBuffer, 0, VK_INDEX_TYPE_UINT16);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, state->pipelineLayout, 0, 1, &state->descriptorSets[imageIndex], 0, NULL);

    vkCmdDrawIndexed(commandBuffer, sizeof(indices)/sizeof(uint16), 1, 0, 0, 0);

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        avFatal("failed to record command buffer");
    }
}

static void cleanupSwapchain();

static bool32 createSwapchain(){
    /*
     *  Swapchain
     */
    {
        SwapChainSupportDetails swapchainSupport = querrySwapchainSupport(state->physicalDevice);
        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapchainSupport.formats);
        VkPresentModeKHR presentMode = chooseSwapPresentMode(swapchainSupport.presentModes);
        VkExtent2D extent = chooseSwapExtent(swapchainSupport.capabilities);

        uint32 imageCount = swapchainSupport.capabilities.minImageCount + 1;
        if(swapchainSupport.capabilities.maxImageCount > 0 && imageCount > swapchainSupport.capabilities.maxImageCount){
            imageCount = swapchainSupport.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo = {0};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = state->surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        QueueFamilyIndices indices = findQueueFamilies(state->physicalDevice);
        uint32 queueFamilyIndices[] = {indices.graphicsFamily, indices.presentFamily};
        if(indices.graphicsFamily != indices.presentFamily){
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }else{
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;
            createInfo.pQueueFamilyIndices = NULL;
        }
        createInfo.preTransform = swapchainSupport.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;
        
        state->swapchainImageFormat = surfaceFormat.format;
        state->swapchainExtent = extent;
        if(extent.width == 0 || extent.height == 0){
            state->minimized = true;
            darrayDestroy(swapchainSupport.formats);
            darrayDestroy(swapchainSupport.presentModes);
            state->swapchainImages = NULL;
            state->swapchainImageViews = NULL;
            state->swapchain = NULL;
            return true;
        }

        if(vkCreateSwapchainKHR(state->device, &createInfo, NULL, &state->swapchain)!=VK_SUCCESS){
            avFatal("Failed to create swapchain!");
            darrayDestroy(swapchainSupport.formats);
            darrayDestroy(swapchainSupport.presentModes);
            return false;
        }
        darrayDestroy(swapchainSupport.formats);
        darrayDestroy(swapchainSupport.presentModes);

        

        vkGetSwapchainImagesKHR(state->device, state->swapchain, &imageCount, NULL);
        state->swapchainImages = darrayReserve(VkImage, imageCount);
        darrayLengthSet(state->swapchainImages, imageCount);
        vkGetSwapchainImagesKHR(state->device, state->swapchain, &imageCount, state->swapchainImages);

        state->swapchainImageViews = darrayReserve(VkImageView, imageCount);
        darrayLengthSet(state->swapchainImageViews, imageCount);
        for(uint32 i = 0; i < imageCount; i++){
            VkImageViewCreateInfo viewCreateInfo = {0};
            viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewCreateInfo.image = state->swapchainImages[i];
            viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewCreateInfo.format = state->swapchainImageFormat;
            viewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewCreateInfo.subresourceRange.baseMipLevel = 0;
            viewCreateInfo.subresourceRange.levelCount = 1;
            viewCreateInfo.subresourceRange.baseArrayLayer = 0;
            viewCreateInfo.subresourceRange.layerCount = 1;
            if(vkCreateImageView(state->device, &viewCreateInfo, NULL, &state->swapchainImageViews[i])!=VK_SUCCESS){
                avFatal("Failed to create image views");
                return false;
            }
        }
        state->imageCount = imageCount;
    }
    return true;
}
static bool32 createFramebuffers(){
    /*
     *      FrameBuffers
     */
    {
        if(state->swapchainExtent.width==0 || state->swapchainExtent.height==0){
            state->framebuffers = NULL;
            return true;
        }
        state->framebuffers = darrayReserve(VkFramebuffer, state->imageCount);
        darrayLengthSet(state->framebuffers, state->imageCount);
        for(uint32 i = 0; i < state->imageCount; i++){
            VkImageView attachments[] = {
                state->swapchainImageViews[i],
            };

            VkFramebufferCreateInfo framebufferInfo = {0};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = state->renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = state->swapchainExtent.width;
            framebufferInfo.height = state->swapchainExtent.height;
            framebufferInfo.layers = 1;

            if(vkCreateFramebuffer(state->device, &framebufferInfo, NULL, &state->framebuffers[i])!= VK_SUCCESS){
                avFatal("Failed to create framebuffer");
                return false;
            }
        }
    }
    return true;
}
static bool32 recreateSwapchain(){
    vkDeviceWaitIdle(state->device);

    cleanupSwapchain();
    
    createSwapchain();
    createFramebuffers();
    
    return true;
}

static VkVertexInputBindingDescription getVertexBindingDescription(){
    VkVertexInputBindingDescription bindingDescription = {0};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
}

static void getAttributeDescriptions(VkVertexInputAttributeDescription attributeDescriptions[2]){
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, pos);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, color);
}



uint32 findMemoryType(uint32 typeFilter, VkMemoryPropertyFlags properties){
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(state->physicalDevice, &memProperties);

    for(uint32 i = 0; i < memProperties.memoryTypeCount; i++){
        if((typeFilter & (1<<i)) && (memProperties.memoryTypes[i].propertyFlags & properties)==properties){
            return i;
        }
    }

    avFatal("Failed to find suitable memory type");
    return -1;
}

void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, VkDeviceMemory* bufferMemory){
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if(vkCreateBuffer(state->device, &bufferInfo, NULL, buffer)){
        avFatal("Failed to create buffer");
        return;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(state->device, *buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if(vkAllocateMemory(state->device, &allocInfo, NULL, bufferMemory)!=VK_SUCCESS){
        avFatal("Failed to allocate buffer memory");
        vkDestroyBuffer(state->device, *buffer, NULL);
        return;
    }
    vkBindBufferMemory(state->device, *buffer, *bufferMemory, 0);
}

void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size){
    VkCommandBufferAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = state->commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    if(vkAllocateCommandBuffers(state->device, &allocInfo, &commandBuffer)!=VK_SUCCESS){
        avFatal("Failed to allocate copy operation command buffer");
        return;
    }

    VkCommandBufferBeginInfo beginInfo = {0};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if(vkBeginCommandBuffer(commandBuffer, &beginInfo)!=VK_SUCCESS){
        vkFreeCommandBuffers(state->device, state->commandPool, 1, &commandBuffer);
        avFatal("Failed to begin command buffer");
        return;
    }

    VkBufferCopy copyRegion = {0};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    if(vkEndCommandBuffer(commandBuffer)!=VK_SUCCESS){
        vkFreeCommandBuffers(state->device, state->commandPool, 1, &commandBuffer);
        avFatal("Failed to end command buffer");
        return;
    }

    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    if(vkQueueSubmit(state->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE)!=VK_SUCCESS){
        vkFreeCommandBuffers(state->device, state->commandPool, 1, &commandBuffer);
        avFatal("Failed to submit command buffer");
        return;
    }
    vkQueueWaitIdle(state->graphicsQueue);
    vkFreeCommandBuffers(state->device, state->commandPool, 1, &commandBuffer);
}

bool8 rendererStartup(uint64* memoryRequirement, void* statePtr, void* config){
    RendererConfig *typedConfig = (RendererConfig *)config;
    *memoryRequirement = sizeof(RendererState);
    if(statePtr == 0){
        return true;
    }
    avMemset(statePtr, 0, sizeof(RendererState));
    state = statePtr;

    const char** requiredValidationLayerNames = 0;
    uint32 requiredValidationLayerCount = 0;
    {
        VkApplicationInfo appInfo = {0};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.apiVersion = VK_API_VERSION_1_0;
        appInfo.pApplicationName = typedConfig->appName;
        appInfo.pEngineName = typedConfig->engineName;
        appInfo.engineVersion = VK_MAKE_VERSION(
            typedConfig->engineVersion.major, 
            typedConfig->engineVersion.minor, 
            typedConfig->engineVersion.patch
        );
        appInfo.applicationVersion = VK_MAKE_VERSION(
            typedConfig->appVersion.major,
            typedConfig->appVersion.minor,
            typedConfig->appVersion.patch
        );

        VkInstanceCreateInfo createInfo = {0};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        const char* vkKHRSurfaceExtensionName = VK_KHR_SURFACE_EXTENSION_NAME;
        const char* vkEXTDebugUtilsExtensionName = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        const char** requiredExtensions = darrayCreate(const char*);
        darrayPush(requiredExtensions, vkKHRSurfaceExtensionName);
        darrayPush(requiredExtensions, vkEXTDebugUtilsExtensionName);
        vulkanPlatformGetRequiredExtensionNames(&requiredExtensions);
        uint32 requiredExtensionCount = darrayLength(requiredExtensions);
        uint32 availableExtensionCount = 0;
        vkEnumerateInstanceExtensionProperties(0, &availableExtensionCount, 0);
        VkExtensionProperties* availableExtensions = darrayReserve(VkExtensionProperties, availableExtensionCount);
        vkEnumerateInstanceExtensionProperties(0, &availableExtensionCount, availableExtensions);
        
        if(!checkExtensionsPresent(requiredExtensionCount, requiredExtensions, availableExtensionCount, availableExtensions)){
            avFatal("Required extension is missing");
            darrayDestroy(requiredExtensions);
            darrayDestroy(availableExtensions);
            return false;
        }
        darrayDestroy(availableExtensions);

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = { 0 };
        if(typedConfig->enableValidation){
            requiredValidationLayerNames = darrayCreate(const char*);
            const char* vkLayerKHRvalidation = "VK_LAYER_KHRONOS_validation";
            darrayPush(requiredValidationLayerNames, vkLayerKHRvalidation);
            requiredValidationLayerCount = darrayLength(requiredValidationLayerNames);

            uint32 availableLayerCount = 0;
            avAssert(vkEnumerateInstanceLayerProperties(&availableLayerCount, 0), VK_SUCCESS, "enumerating instance layer properties");
            VkLayerProperties* availableLayers = darrayReserve(VkLayerProperties, availableLayerCount);
            avAssert(vkEnumerateInstanceLayerProperties(&availableLayerCount, availableLayers), VK_SUCCESS, "enumerating instance layer properties");

            if(!checkLayersPresent(requiredValidationLayerCount, requiredValidationLayerNames, availableLayerCount, availableLayers)){
                avFatal("Required layer is missing");
                darrayDestroy(requiredExtensions);
                darrayDestroy(requiredValidationLayerNames);
                darrayDestroy(availableLayers);
                return false;
            }
            darrayDestroy(availableLayers);

            populateDebugMessengerCreateInfo(&debugCreateInfo);
            createInfo.pNext = &debugCreateInfo;
        }

        createInfo.enabledExtensionCount = requiredExtensionCount;
        createInfo.ppEnabledExtensionNames = requiredExtensions;
        createInfo.enabledLayerCount = requiredValidationLayerCount;
        createInfo.ppEnabledLayerNames = requiredValidationLayerNames;

        VkResult result = vkCreateInstance(&createInfo, nullptr, &state->instance);
        if(result != VK_SUCCESS){
            avAssert(false, true, "Failed to create instance");
            darrayDestroy(requiredExtensions);
            darrayDestroy(requiredValidationLayerNames);
            return false;
        }
        darrayDestroy(requiredExtensions);
        

        if(typedConfig->enableValidation){
            VkDebugUtilsMessengerCreateInfoEXT debugInfo = {0};
            populateDebugMessengerCreateInfo(&debugInfo);
            state->debugMessenger = avAllocate(sizeof(VkDebugUtilsMessengerEXT), "allocating memory for debug messenger");
            result = createDebugUtilsMessengerEXT(state->instance, &debugInfo, NULL, state->debugMessenger);
            if(result != VK_SUCCESS){
                avFatal("Failed to create debug messenger");
                darrayDestroy(requiredValidationLayerNames);
                return false;
            }
        }

        if(!vulkanPlatformCreateSurface(state, typedConfig->platformState, &state->surface)){
            avFatal("Failed to create surface");
            return false;
        }
    }

    /*
     *  VkPhysicalDevice
     */
    {
        uint32 deviceCount = 0;
        if(vkEnumeratePhysicalDevices(state->instance, &deviceCount, NULL)!=VK_SUCCESS) {
            avFatal("Failed to enumerate physical devices");
            darrayDestroy(requiredValidationLayerNames);
            return false;
        }
        VkPhysicalDevice* devices = darrayReserve(VkPhysicalDevice, deviceCount);
        if(vkEnumeratePhysicalDevices(state->instance, &deviceCount, devices)!=VK_SUCCESS){
            avFatal("Failed to enumerate physical devices");
            darrayDestroy(devices);
            darrayDestroy(requiredValidationLayerNames);
            return false;
        }

        for(uint32 i = 0; i < deviceCount; i++){
            if(isDeviceSuitable(devices[i])){
                state->physicalDevice = devices[i];
                break;
            }
        }
        if(state->physicalDevice == VK_NULL_HANDLE){
            avFatal("Failed to find suitable GPU");
            darrayDestroy(devices);
            darrayDestroy(requiredValidationLayerNames);
            return false;
        }
        darrayDestroy(devices);
    }
    /*
    *   VkDevice
    */
    {
        QueueFamilyIndices indices = findQueueFamilies(state->physicalDevice);

        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfos[2] = { 0 };
        queueCreateInfos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfos[0].queueFamilyIndex = indices.graphicsFamily;
        queueCreateInfos[0].queueCount = 1;
        queueCreateInfos[0].pQueuePriorities = &queuePriority;

        queueCreateInfos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfos[1].queueFamilyIndex = indices.presentFamily;
        queueCreateInfos[1].queueCount = 1;
        queueCreateInfos[1].pQueuePriorities = &queuePriority;

        uint queueCount = indices.graphicsFamily != indices.presentFamily ? 2 : 1;

        VkPhysicalDeviceFeatures deviceFeatures = {0};

        VkDeviceCreateInfo createInfo = { 0 };
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pQueueCreateInfos = queueCreateInfos;
        createInfo.queueCreateInfoCount = queueCount;
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledLayerCount = requiredValidationLayerCount;
        createInfo.ppEnabledLayerNames = requiredValidationLayerNames;
        createInfo.enabledExtensionCount = deviceExtensionCount;
        createInfo.ppEnabledExtensionNames = deviceExtensions;
        if(vkCreateDevice(state->physicalDevice, &createInfo, NULL, &state->device)!=VK_SUCCESS){
            avFatal("Failed to create logical device!");
            darrayDestroy(requiredValidationLayerNames);
            return false;
        }
        darrayDestroy(requiredValidationLayerNames);

        vkGetDeviceQueue(state->device, indices.graphicsFamily, 0, &state->graphicsQueue);
        vkGetDeviceQueue(state->device, indices.presentFamily, 0, &state->presentQueue);
    }
    
    if(!createSwapchain()){
        avFatal("Failed to create swapchain");
        return false;
    }
    /*
     *      RenderPass
     */
    {
        VkAttachmentDescription colorAttachment = {0};
        colorAttachment.format = state->swapchainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef = {0};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {0};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        
        VkSubpassDependency dependency = {0};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo = {0};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if(vkCreateRenderPass(state->device, &renderPassInfo, NULL, &state->renderPass)!=VK_SUCCESS){
            avFatal("Failed to create renderpass");
            return false;
        }
    }

    if(!createFramebuffers()){
        avFatal("failed to recreate swapchain");
        return false;
    }
    /*
     *      DescriptorSetLayout
     */
    {
        VkDescriptorSetLayoutBinding uboLayoutBinding = {0};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        uboLayoutBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo layoutInfo = {0};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &uboLayoutBinding;

        if(vkCreateDescriptorSetLayout(state->device, &layoutInfo, NULL, &state->descriptorSetLayout)!=VK_SUCCESS){
            avFatal("Failedto create descriptor set layout");
            return false;
        }

    }
    /*
     *     GraphicsPipeline
     */
    {
        VkShaderModule vertShaderModule = createShaderModule(basic_shader_vert_data, basic_shader_vert_size);
        VkShaderModule fragShaderModule = createShaderModule(basic_shader_frag_data, basic_shader_frag_size);

        VkPipelineShaderStageCreateInfo vertShaderStageInfo = {0};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";
        
        VkPipelineShaderStageCreateInfo fragShaderStageInfo = {0};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};


        VkVertexInputBindingDescription bindingDescription = getVertexBindingDescription();
        VkVertexInputAttributeDescription attributeDescriptions[2] = {0};
        getAttributeDescriptions(attributeDescriptions);

        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {0};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = sizeof(attributeDescriptions)/sizeof(VkVertexInputAttributeDescription);
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {0};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport = {0};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float) state->swapchainExtent.width;
        viewport.height = (float) state->swapchainExtent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor = {0};
        scissor.offset = (VkOffset2D){0,0};
        scissor.extent = state->swapchainExtent;

        VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };

        VkPipelineDynamicStateCreateInfo dynamicState = {0};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = sizeof(dynamicStates)/sizeof(VkDynamicState);
        dynamicState.pDynamicStates = dynamicStates;

        VkPipelineViewportStateCreateInfo viewportState = {0};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer = {0};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.depthBiasConstantFactor = 0.0f;
        rasterizer.depthBiasClamp = 0.0f;
        rasterizer.depthBiasSlopeFactor = 0.0f;

        VkPipelineMultisampleStateCreateInfo multisampling = { 0 };
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.minSampleShading = 1.0f; 
        multisampling.pSampleMask = nullptr;
        multisampling.alphaToCoverageEnable = VK_FALSE;
        multisampling.alphaToOneEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorBlendAttachment = {0};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorBlending = {0};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        colorBlending.blendConstants[0] = 0.0f;
        colorBlending.blendConstants[1] = 0.0f;
        colorBlending.blendConstants[2] = 0.0f;
        colorBlending.blendConstants[3] = 0.0f;

        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &state->descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 0;
        pipelineLayoutInfo.pPushConstantRanges = NULL;

        if(vkCreatePipelineLayout(state->device, &pipelineLayoutInfo, NULL, &state->pipelineLayout)!=VK_SUCCESS){
            avFatal("Failed to create pipeline layout");
            vkDestroyShaderModule(state->device, vertShaderModule, NULL);
            vkDestroyShaderModule(state->device, fragShaderModule, NULL);
            return false;
        };

        VkGraphicsPipelineCreateInfo pipelineInfo = {0};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = NULL;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = state->pipelineLayout;
        pipelineInfo.renderPass = state->renderPass;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineInfo.basePipelineIndex = -1;

        if(vkCreateGraphicsPipelines(state->device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &state->graphicsPipeline)!=VK_SUCCESS){
            avFatal("Failed to create graphics pipeline");
            vkDestroyShaderModule(state->device, vertShaderModule, NULL);
            
            return false;
        }


        vkDestroyShaderModule(state->device, vertShaderModule, NULL);
        vkDestroyShaderModule(state->device, fragShaderModule, NULL);
    }
    /*
     *      CommandPool
     */
    {
        QueueFamilyIndices queueFamilyIndices = findQueueFamilies(state->physicalDevice);
        
        VkCommandPoolCreateInfo poolInfo = {0};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if(vkCreateCommandPool(state->device, &poolInfo, NULL, &state->commandPool)!=VK_SUCCESS){
            avFatal("Failed to create command pool");
            return false;
        }

        state->commandBuffer = darrayReserve(VkCommandBuffer, state->imageCount);
        darrayLengthSet(state->commandBuffer, state->imageCount);

        VkCommandBufferAllocateInfo allocInfo = {0};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = state->commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = state->imageCount;
        if(vkAllocateCommandBuffers(state->device, &allocInfo, state->commandBuffer)!=VK_SUCCESS){
            avFatal("Failed to allocate command buffer");
            return false;
        }
    }
    /*
     *      VertexBuffer
     */
    {
        VkDeviceSize bufferSize = sizeof(vertices);
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer, &stagingBufferMemory);

        void* data;
        vkMapMemory(state->device, stagingBufferMemory, 0, bufferSize, 0, &data);
        avMemcpy(data, vertices, bufferSize);
        vkUnmapMemory(state->device, stagingBufferMemory);

        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &state->vertexBuffer, &state->vertexBufferMemory);
    
        copyBuffer(stagingBuffer, state->vertexBuffer, bufferSize);

        vkDestroyBuffer(state->device, stagingBuffer, NULL);
        vkFreeMemory(state->device, stagingBufferMemory, NULL);
    }
    /*
     *      IndexBuffer
     */
    {
        VkDeviceSize bufferSize = sizeof(indices);
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer, &stagingBufferMemory);

        void* data;
        vkMapMemory(state->device, stagingBufferMemory, 0, bufferSize, 0, &data);
        avMemcpy(data, indices, bufferSize);
        vkUnmapMemory(state->device, stagingBufferMemory);

        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &state->indexBuffer, &state->indexBufferMemory);
    
        copyBuffer(stagingBuffer, state->indexBuffer, bufferSize);

        vkDestroyBuffer(state->device, stagingBuffer, NULL);
        vkFreeMemory(state->device, stagingBufferMemory, NULL);
    }
    /*
     *      Uniform Buffers
     */
    {
        VkDeviceSize bufferSize = sizeof(UniformBufferObject);
        state->uniformBuffers = darrayReserve(VkBuffer, state->imageCount);
        state->uniformBuffersMemory = darrayReserve(VkDeviceMemory, state->imageCount);
        state->uniformBuffersMapped = darrayReserve(void*, state->imageCount);
        darrayLengthSet(state->uniformBuffers, state->imageCount);
        darrayLengthSet(state->uniformBuffersMemory, state->imageCount);
        darrayLengthSet(state->uniformBuffersMapped, state->imageCount);

        for(uint32 i = 0; i < state->imageCount; i++){
            createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &state->uniformBuffers[i], &state->uniformBuffersMemory[i]);
            vkMapMemory(state->device, state->uniformBuffersMemory[i], 0, bufferSize, 0, &state->uniformBuffersMapped[i]);
        }
    }
    /*
     *      Uniform descriptor pool
     */
    {
        VkDescriptorPoolSize poolSize = {0};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = state->imageCount;

        VkDescriptorPoolCreateInfo poolInfo = {0};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = state->imageCount;

        if(vkCreateDescriptorPool(state->device, &poolInfo, NULL, &state->descriptorPool)!=VK_SUCCESS){
            avFatal("Failed to create descriptor pool");
            return false;
        }

        // descriptor sets
        VkDescriptorSetLayout* layouts = darrayCreateSized(VkDescriptorSetLayout, state->imageCount);
        for(uint32 i = 0; i < state->imageCount; i++){
            layouts[i] = state->descriptorSetLayout;
        }
        VkDescriptorSetAllocateInfo allocInfo = {0};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = state->descriptorPool;
        allocInfo.descriptorSetCount = state->imageCount;
        allocInfo.pSetLayouts = layouts;

        state->descriptorSets = darrayCreateSized(VkDescriptorSet, state->imageCount);
        if(vkAllocateDescriptorSets(state->device, &allocInfo, state->descriptorSets)!=VK_SUCCESS){
            avFatal("Failed to allocate descriptor sets");
            return false;
        }

        for(uint32 i = 0; i < state->imageCount; i++){
            VkDescriptorBufferInfo bufferInfo = {0};
            bufferInfo.buffer = state->uniformBuffers[i];
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(UniformBufferObject);

            VkWriteDescriptorSet descriptorWrite = {0};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = state->descriptorSets[i];
            descriptorWrite.dstBinding = 0;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pBufferInfo = &bufferInfo;
            descriptorWrite.pImageInfo = NULL;
            descriptorWrite.pTexelBufferView = NULL;
            vkUpdateDescriptorSets(state->device, 1, &descriptorWrite, 0, NULL);
        }
    }
    /*
     *      Sync Objects
     */
    {
        state->imageAvailableSemaphore = darrayReserve(VkSemaphore, state->imageCount);
        darrayLengthSet(state->imageAvailableSemaphore, state->imageCount);
        state->renderFinishedSemaphore = darrayReserve(VkSemaphore, state->imageCount);
        darrayLengthSet(state->renderFinishedSemaphore, state->imageCount);
        state->inFlightFence = darrayReserve(VkFence, state->imageCount);
        darrayLengthSet(state->inFlightFence, state->imageCount);

        VkSemaphoreCreateInfo semaphoreInfo = {0};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo = {0};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for(uint32 i = 0; i < state->imageCount; i++){
            if(vkCreateSemaphore(state->device, &semaphoreInfo, NULL, &state->imageAvailableSemaphore[i]) != VK_SUCCESS ||
                vkCreateSemaphore(state->device, &semaphoreInfo, NULL, &state->renderFinishedSemaphore[i]) != VK_SUCCESS ||
                vkCreateFence(state->device, &fenceInfo, NULL, &state->inFlightFence[i])){
                    avFatal("Failed to create semaphores");
                    return false;
            }
        }
    }
    return true;

}

void rendererSignalResize(){
    if(state)state->framebufferResized = true;
}

void updateUniformBuffer(uint32 imageIndex){
    double time = platformGetAbsoluteTime();

    UniformBufferObject ubo = {0};
    ubo.model = mat4Rotate(time * (90.0f/180.0f*3.141581), vec3(0.0, 0.0, 1.0));
    ubo.view = lookAt(vec3(2.0, 2.0, 2.0), vec3(0.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0));
    ubo.proj = perspective(45.0/180.0f*3.141581, state->swapchainExtent.width/state->swapchainExtent.height, 0.1f, 10.0f);
    avMemcpy(state->uniformBuffersMapped[imageIndex], &ubo, sizeof(ubo));

}

void rendererDrawFrame(){
    if(state->minimized){
        if(state->framebufferResized){
            state->minimized = false;
            recreateSwapchain();
            state->framebufferResized = false;
        }else{
            return;
        }
    }
    static uint32 frameIndex = 0;
    
    vkWaitForFences(state->device, 1, &state->inFlightFence[frameIndex], VK_TRUE, UINT64_MAX);
    
    uint32 imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(state->device, state->swapchain, UINT64_MAX, state->imageAvailableSemaphore[frameIndex], VK_NULL_HANDLE, &imageIndex);
    if(result == VK_ERROR_OUT_OF_DATE_KHR){
        recreateSwapchain();
        return;
    }else if(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR){
        avFatal("Failed to acquire swap chain image");
        return;
    }
    vkResetFences(state->device, 1, &state->inFlightFence[frameIndex]);
    
    vkResetCommandBuffer(state->commandBuffer[imageIndex], 0);
    recordCommandBuffer(state->commandBuffer[imageIndex], imageIndex);

    updateUniformBuffer(imageIndex);

    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {state->imageAvailableSemaphore[frameIndex]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &state->commandBuffer[imageIndex];
    VkSemaphore signalSemaphores[] = {state->renderFinishedSemaphore[frameIndex]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if(vkQueueSubmit(state->graphicsQueue, 1, &submitInfo, state->inFlightFence[frameIndex])!=VK_SUCCESS){
        avFatal("Failed to submit draw command buffer");
    }

    VkPresentInfoKHR presentInfo = {0};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = {state->swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.pResults = NULL;
    result = vkQueuePresentKHR(state->presentQueue, &presentInfo);
    if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || state->framebufferResized){
        recreateSwapchain();
        state->framebufferResized = false;
    }else if(result != VK_SUCCESS){
        avFatal("failed to present image");
    }

    frameIndex = (frameIndex + 1) % state->imageCount;
}

static void cleanupSwapchain(){
    for(uint32 i = 0; i < darrayLength(state->framebuffers); i++){
        vkDestroyFramebuffer(state->device, state->framebuffers[i], NULL);
    }
    darrayDestroy(state->framebuffers);

    for(uint32 i = 0; i < darrayLength(state->swapchainImageViews); i++){
        vkDestroyImageView(state->device, state->swapchainImageViews[i], NULL);
    }

    darrayDestroy(state->swapchainImageViews);
    darrayDestroy(state->swapchainImages);

    if(state->swapchain){
        vkDestroySwapchainKHR(state->device, state->swapchain, NULL);
    }
}

void rendererShutdown(void* statePtr){

    vkDeviceWaitIdle(state->device);
    cleanupSwapchain();

    for(uint32 i = 0; i < state->imageCount; i++){
        vkDestroyBuffer(state->device, state->uniformBuffers[i], NULL);
        vkFreeMemory(state->device, state->uniformBuffersMemory[i], NULL);
    }
    vkDestroyBuffer(state->device, state->indexBuffer, NULL);
    vkFreeMemory(state->device, state->indexBufferMemory, NULL);
    vkDestroyBuffer(state->device, state->vertexBuffer, NULL);
    vkFreeMemory(state->device, state->vertexBufferMemory, NULL);

    for(uint32 i = 0; i < state->imageCount; i++){
        vkDestroySemaphore(state->device, state->imageAvailableSemaphore[i], NULL);
        vkDestroySemaphore(state->device, state->renderFinishedSemaphore[i], NULL);
        vkDestroyFence(state->device, state->inFlightFence[i], NULL);
    }

    darrayDestroy(state->renderFinishedSemaphore);
    darrayDestroy(state->imageAvailableSemaphore);
    darrayDestroy(state->inFlightFence);

    vkDestroyCommandPool(state->device, state->commandPool, NULL);

    vkDestroyDescriptorPool(state->device, state->descriptorPool, NULL);
    vkDestroyDescriptorSetLayout(state->device, state->descriptorSetLayout, NULL);

    vkDestroyPipeline(state->device, state->graphicsPipeline, NULL);
    vkDestroyPipelineLayout(state->device, state->pipelineLayout, NULL);
    vkDestroyRenderPass(state->device, state->renderPass, NULL);

    

    vkDestroyDevice(state->device, NULL);

    vkDestroySurfaceKHR(state->instance, state->surface, NULL);
    if(state->debugMessenger){
        destroyDebugUtilsMessengerEXT(state->instance, *state->debugMessenger, NULL);
    }
    vkDestroyInstance(state->instance, NULL);
}