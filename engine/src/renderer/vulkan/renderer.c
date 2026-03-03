#include "../renderer.h"
#include "defines.h"
#include "logging.h"
#include <vulkan/vulkan.h>
#include "containers/darray.h"
#include <AvUtils/avString.h>
#include <AvUtils/avMemory.h>

typedef struct RendererState {
    VkInstance instance;
    VkDebugUtilsMessengerEXT* debugMessenger;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    VkSurfaceKHR surface;
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
    vkGetPhysicalDeviceQueueFamilyProperties2(device, &queueFamilyCount, queueFamilies);

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
    if(darrayLength(swapChainSupport.formats)==0 || darrayLenth(swapChainSupport.presentModes)==0){
        darrayDestroy(swapChainSupport.formats);
        darrayDestroy(swapChainSupport.presentModes);
        return false;
    }
    return true;
}

static VkSurfaceFormatKHR chooseSwapSurfaceFormat(const VkSurfaceFormatKHR* availableFormats){
    uint32 formatCount = darrayLength(availableFormats);
    for(uint32 i = 0; i < formatCount; i++){
        VkSurfaceFormatKHR format = availableFormats[i];
        if(format.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormats->colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR){
            return format;
        }
    }
    return availableFormats[0];
}

static VkPresentModeKHR choosSwapPresentMode(const VkPresentModeKHR* availablePresentModes){
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
        
    }
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

        if(!vulkanPlatformCreateSurface(state, typedConfig->platformState, state->surface)){
            avFatal("Failed to create surface");
            return false;
        }
    }

    /*
     *  VkPhysicalDevice
     */
    {
        uint32 deviceCount = 0;
        if(!vkEnumeratePhysicalDevices(state->instance, &deviceCount, NULL)) {
            avFatal("Failed to enumerate physical devices");
            darrayDestroy(requiredValidationLayerNames);
            return false;
        }
        VkPhysicalDevice* devices = darrayReserve(VkPhysicalDevice, deviceCount);
        if(!vkEnumeratePhysicalDevices(state->instance, &deviceCount, devices)){
            avFatal("Failed to enumerate physical devices");
            darrayDestroy(devices);
            darrayDestroy(requiredValidationLayerNames);
            return false;
        }

        for(uint32 i = 0; i < devices; i++){
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
        createInfo.pQueueCreateInfos = &queueCreateInfos;
        createInfo.queueCreateInfoCount = queueCount;
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledLayerCount = requiredValidationLayerCount;
        createInfo.ppEnabledLayerNames = requiredValidationLayerNames;
        createInfo.enabledExtensionCount = deviceExtensionCount;
        createInfo.ppEnabledExtensionNames = deviceExtensions;
        if(!vkCreateDevice(state->physicalDevice, &createInfo, NULL, &state->device)){
            avFatal("Failed to create logical device!");
            darrayDestroy(requiredValidationLayerNames);
            return false;
        }
        darrayDestroy(requiredValidationLayerNames);

        vkGetDeviceQueue(state->device, indices.graphicsFamily, 0, &state->graphicsQueue);
        vkGetDeviceQueue(state->device, indices.presentFamily, 0, &state->presentQueue);
    }
    return true;

}
void rendererShutdown(void* statePtr){
    vkDestroyDevice(state->device, NULL);

    vkDestroySurfaceKHR(state->instance, state->surface, NULL);
    if(state->debugMessenger){
        destroyDebugUtilsMessengerEXT(state->instance, *state->debugMessenger, NULL);
    }
    vkDestroyInstance(state->instance, NULL);
}