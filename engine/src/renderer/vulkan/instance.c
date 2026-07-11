#include "instance.h"
#include <vulkan/vulkan_core.h>
#include <AvUtils/avMemory.h>
#include <AvUtils/avString.h>
#include "containers/darray.h"
#include "logging.h"

static const char* validationLayers[] = {
	"VK_LAYER_KHRONOS_validation"
};
static const uint32 validationLayerCount = sizeof(validationLayers)/sizeof(validationLayers[0]);

static bool32 checkValidationLayerSupport(){
	uint32 layerCount = 0;
	vkEnumerateInstanceLayerProperties(&layerCount, NULL);

	VkLayerProperties* layers = avAllocate(layerCount* sizeof(VkLayerProperties), "");

	vkEnumerateInstanceLayerProperties(&layerCount, layers);

	for(uint32 i = 0; i < validationLayerCount; i++){
		bool32 found = false;
		for(uint32 j = 0; j < layerCount; j++){
			if(avStringEquals(AV_CSTR(validationLayers[i]), AV_CSTR(layers[j].layerName))){
				found = true;
				break;
			}
		}
		if(!found){
			avFree(layers);
			return false;
		}
	}
	avFree(layers);
	return true;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData) {
	(void)pUserData;

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

	AvLogLevel type = { 0 };
	if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
		type = AV_LOG_LEVEL_INFO;
	}
	if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
		type = AV_LOG_LEVEL_ERROR;
	}
	if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
		type = AV_LOG_LEVEL_INFO;
	}

	//logDeviceValidation("vulkan", level, type, pCallbackData->pMessage);
    avLogVulkan("vulkan", level, type, "%s", pCallbackData->pMessage);
	return VK_FALSE;
}

static void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT* createInfo){
	createInfo->sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	createInfo->messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	createInfo->messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	createInfo->pfnUserCallback = debugCallback;
	createInfo->pUserData = NULL;
	createInfo->pNext = NULL;
}

static VkResult createDebugMessenger(Instance* instance){
	VkDebugUtilsMessengerCreateInfoEXT createInfo = {0};
	populateDebugMessengerCreateInfo(&createInfo);

	PFN_vkCreateDebugUtilsMessengerEXT create = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance->handle, "vkCreateDebugUtilsMessengerEXT");
	if(create == NULL){
		avError("Failed to get proc address for vkCreateDebugUtilsMessengerEXT");
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}
	return create(instance->handle, &createInfo, NULL, &instance->debugMessenger);
}

static void destroyDebugMessenger(Instance* instance){
	if(instance->debugMessenger == VK_NULL_HANDLE) return;

	PFN_vkDestroyDebugUtilsMessengerEXT destroy = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance->handle, "vkDestroyDebugUtilsMessengerEXT");
	if(destroy != NULL){
		destroy(instance->handle, instance->debugMessenger, NULL);
	}
	instance->debugMessenger = VK_NULL_HANDLE;
}

bool32 instanceCreate(const InstanceCreateInfo* createInfo, Renderer* renderer){
	VkResult result = VK_SUCCESS;
	if(createInfo==NULL||renderer==NULL){
		avError("Invalid arguments");
		return false;
	}
	Instance* instance = &renderer->instance;

	avMemset(instance, 0, sizeof(*instance));
	VkApplicationInfo appInfo = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = createInfo->applicationName,
		.applicationVersion = createInfo->applicationVersion,
		.pEngineName = createInfo->engineName,
		.engineVersion = createInfo->engineVersion,
		.apiVersion = VK_API_VERSION_1_0,
	};


	
	uint32 layerCount = 0;
	const char* const* layers = NULL;
	if(createInfo->enableValidation){
		if(!checkValidationLayerSupport()){
			avError("Validation requested but not present");
			return false;
		}
		layerCount = 1;
		layers =  validationLayers;
	}

	const char** extensions = darrayCreate(const char*);
	uint32 extensionCount = 0;
	for(uint32 i = 0; i < createInfo->extensionCount; i++){
		darrayPush(extensions, createInfo->extensions[i]);
	}
	if(createInfo->enableValidation){
		darrayPush(extensions, (const char*)VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}
	extensionCount = darrayLength(extensions);

	VkInstanceCreateInfo instanceInfo = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &appInfo,
		.enabledExtensionCount = extensionCount,
		.ppEnabledExtensionNames = extensions,
		.enabledLayerCount = layerCount,
		.ppEnabledLayerNames = layers,
	};
	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {0};
	if(createInfo->enableValidation){
		populateDebugMessengerCreateInfo(&debugCreateInfo);
		instanceInfo.pNext = &debugCreateInfo;
	}
	result = vkCreateInstance(&instanceInfo, NULL, &instance->handle);
	darrayDestroy(extensions);
	if(result != VK_SUCCESS){
		avError("Failed to create vulkan instance");
		return false;
	}
	avLog(AV_SUCCESS, "created vulkan instance");

	if(createInfo->enableValidation){
		result = createDebugMessenger(instance);
		if(result != VK_SUCCESS){
			instanceDestroy(renderer);
			avError("Failed to create debug messenger");
			return false;
		}
	}
	return true;
}


void instanceDestroy(Renderer* renderer){
	if(renderer == NULL) return;
	Instance* instance = &renderer->instance;
	destroyDebugMessenger(instance);

	if(instance->handle != VK_NULL_HANDLE){
		vkDestroyInstance(instance->handle, NULL);
		instance->handle = VK_NULL_HANDLE;
	}
}