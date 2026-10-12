#include "logging.h"
#include "renderer.h"
#include "gpu.h"
#include <AvUtils/avMemory.h>
#include <AvUtils/avString.h>
#include <vulkan/vulkan_core.h>

static bool32 checkFeatures(const VkPhysicalDeviceFeatures* available, const VkPhysicalDeviceFeatures* required){
	#define CHECK_FEATURE(name) \
		if(required->name && !available->name) return false;

	CHECK_FEATURE(robustBufferAccess);
    CHECK_FEATURE(fullDrawIndexUint32);
    CHECK_FEATURE(imageCubeArray);
    CHECK_FEATURE(independentBlend);
    CHECK_FEATURE(geometryShader);
    CHECK_FEATURE(tessellationShader);
    CHECK_FEATURE(sampleRateShading);
    CHECK_FEATURE(dualSrcBlend);
    CHECK_FEATURE(logicOp);
    CHECK_FEATURE(multiDrawIndirect);
    CHECK_FEATURE(drawIndirectFirstInstance);
    CHECK_FEATURE(depthClamp);
    CHECK_FEATURE(depthBiasClamp);
    CHECK_FEATURE(fillModeNonSolid);
    CHECK_FEATURE(depthBounds);
    CHECK_FEATURE(wideLines);
    CHECK_FEATURE(largePoints);
    CHECK_FEATURE(alphaToOne);
    CHECK_FEATURE(multiViewport);
    CHECK_FEATURE(samplerAnisotropy);
    CHECK_FEATURE(textureCompressionETC2);
    CHECK_FEATURE(textureCompressionASTC_LDR);
    CHECK_FEATURE(textureCompressionBC);
    CHECK_FEATURE(occlusionQueryPrecise);
    CHECK_FEATURE(pipelineStatisticsQuery);
    CHECK_FEATURE(vertexPipelineStoresAndAtomics);
    CHECK_FEATURE(fragmentStoresAndAtomics);
    CHECK_FEATURE(shaderTessellationAndGeometryPointSize);
    CHECK_FEATURE(shaderImageGatherExtended);
    CHECK_FEATURE(shaderStorageImageExtendedFormats);
    CHECK_FEATURE(shaderStorageImageMultisample);
    CHECK_FEATURE(shaderStorageImageReadWithoutFormat);
    CHECK_FEATURE(shaderStorageImageWriteWithoutFormat);
    CHECK_FEATURE(shaderUniformBufferArrayDynamicIndexing);
    CHECK_FEATURE(shaderSampledImageArrayDynamicIndexing);
    CHECK_FEATURE(shaderStorageBufferArrayDynamicIndexing);
    CHECK_FEATURE(shaderStorageImageArrayDynamicIndexing);
    CHECK_FEATURE(shaderClipDistance);
    CHECK_FEATURE(shaderCullDistance);
    CHECK_FEATURE(shaderFloat64);
    CHECK_FEATURE(shaderInt64);
    CHECK_FEATURE(shaderInt16);
    CHECK_FEATURE(shaderResourceResidency);
    CHECK_FEATURE(shaderResourceMinLod);
    CHECK_FEATURE(sparseBinding);
    CHECK_FEATURE(sparseResidencyBuffer);
    CHECK_FEATURE(sparseResidencyImage2D);
    CHECK_FEATURE(sparseResidencyImage3D);
    CHECK_FEATURE(sparseResidency2Samples);
    CHECK_FEATURE(sparseResidency4Samples);
    CHECK_FEATURE(sparseResidency8Samples);
    CHECK_FEATURE(sparseResidency16Samples);
    CHECK_FEATURE(sparseResidencyAliased);
    CHECK_FEATURE(variableMultisampleRate);
    CHECK_FEATURE(inheritedQueries);

#undef CHECK_FEATURE

	return true;
}



#define INVALID_QUEUE_FAMILY UINT32_MAX
static bool32 findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface, const GpuRequirements* requirements, Gpu* gpu){
	gpu->graphicsFamily = INVALID_QUEUE_FAMILY;
	gpu->presentFamily = INVALID_QUEUE_FAMILY;
	gpu->computeFamily = INVALID_QUEUE_FAMILY;
	gpu->transferFamily = INVALID_QUEUE_FAMILY;

	uint32 queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, NULL);
	if(queueFamilyCount == 0) return false;
	VkQueueFamilyProperties* queueFamilies = avAllocate(sizeof(VkQueueFamilyProperties)*queueFamilyCount, "");
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies);

	for(uint32 i = 0; i < queueFamilyCount; i++){
		VkQueueFamilyProperties* family = &queueFamilies[i];
		if(family->queueCount > 0 && (family->queueFlags & VK_QUEUE_GRAPHICS_BIT)){
			gpu->graphicsFamily = i;
		}
		if(family->queueCount > 0 && (family->queueFlags & VK_QUEUE_COMPUTE_BIT)){
			gpu->computeFamily = i;
		}
		if(family->queueCount > 0 && (family->queueFlags & VK_QUEUE_TRANSFER_BIT)){
			gpu->transferFamily = i;
		}

		VkBool32 presentSupport = VK_FALSE;

		vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
		if(family->queueCount > 0 && presentSupport){
			gpu->presentFamily = i;
		}
	}
	avFree(queueFamilies);

	if(requirements->requireGraphicsQueue && gpu->transferFamily == INVALID_QUEUE_FAMILY){
		return false;
	}
	if(requirements->requireComputeQueue && gpu->transferFamily == INVALID_QUEUE_FAMILY){
		return false;
	}
	if(requirements->requireTransferQueue && gpu->transferFamily == INVALID_QUEUE_FAMILY){
		return false;
	}
	if(requirements->requirePresentQueue && gpu->transferFamily == INVALID_QUEUE_FAMILY){
		return false;
	}
	return true;
}

static bool32 checkExtensions(VkPhysicalDevice device, const GpuRequirements* requirements){
	uint32 extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, NULL);
    VkExtensionProperties* extensions = avAllocate(sizeof(VkExtensionProperties) * extensionCount, "");
    if (extensions == NULL){
        return false;
    }
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, extensions);
    for (uint32 i = 0; i < requirements->requiredExtensionCount; i++) {
        bool32 found = false;
        for (uint32 j = 0; j < extensionCount; j++) {
            if (avStringEquals(AV_CSTR(requirements->requiredExtensions[i]),AV_CSTR(extensions[j].extensionName))){
                found = true;
                break;
            }
        }
        if (!found){
            avFree(extensions);
            return false;
        }
    }
    avFree(extensions);
    return true;
}

static int32 scoreGpu(VkPhysicalDevice device, VkSurfaceKHR surface, const GpuRequirements* requirements, const GpuPreferences* preferences, Gpu* gpuInfo){
	Gpu candidate = {0};
	candidate.physical = device;

	vkGetPhysicalDeviceProperties(device, &candidate.properties);
	vkGetPhysicalDeviceFeatures(device, &candidate.features);
	vkGetPhysicalDeviceMemoryProperties(device, &candidate.memory);

	if(!checkFeatures(&candidate.features, &requirements->requiredFeatures.vulkan10)){
		return -1; // device is not suitable
	}
	if(!findQueueFamilies(device, surface, requirements, &candidate)){
		return -1;
	}
	if(!checkExtensions(device, requirements)){
		return -1;
	}

	int32 score = 1; // the gpu is acceptable
	if(preferences->type != GPU_TYPE_ANY){
		if(preferences->type == GPU_TYPE_DISCRETE && candidate.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU){
			score += 1000;
		}
		if(preferences->type == GPU_TYPE_INTEGRATED && candidate.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU){
			score += 1000;
		}
	}
	VkDeviceSize localMemory = 0;
	for(uint32 i = 0; i < candidate.memory.memoryHeapCount; i++){
		if(candidate.memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT){
			localMemory += candidate.memory.memoryHeaps[i].size;
		}
	}
	score += (int32)(localMemory / (1024*1024));

	*gpuInfo = candidate;
	return score;
}

static uint32 getUniqueQueueFamilies(const Gpu* gpu, uint32* families){
	uint32 count = 0;

    uint32 queueFamilies[] =
    {
        gpu->graphicsFamily,
        gpu->presentFamily,
        gpu->computeFamily,
        gpu->transferFamily
    };

    for (uint32 i = 0; i < 4; i++){
        uint32 family = queueFamilies[i];
        if (family == INVALID_QUEUE_FAMILY){
            continue;
        }
        bool32 exists = false;
        for (uint32 j = 0; j < count; j++) {
            if (families[j] == family) {
                exists = true;
                break;
            }
        }
        if (!exists){
            families[count] = family;
            count++;
        }
    }

    return count;
}

bool32 querryGpus(Renderer* renderer, GpuRequirements requirements, GpuPreferences preferences){
	uint32 deviceCount = 0;
	vkEnumeratePhysicalDevices(renderer->instance.handle, &deviceCount, NULL);
	if(deviceCount == 0){
		avError("No vulkan capable devices present");
		return false;
	}
	VkPhysicalDevice* devices = avAllocate(sizeof(VkPhysicalDevice)*deviceCount, "");
	vkEnumeratePhysicalDevices(renderer->instance.handle, &deviceCount, devices);

	uint32 suitableCount = 0;
	Gpu* suitableDevices = avAllocate(sizeof(Gpu)*deviceCount, "");

	int bestScore = 0;
	uint32 bestGpu = {0};
	for(uint32 i = 0; i < deviceCount; i++){
		VkPhysicalDevice device = devices[i];
		Gpu candidate;
		int score = scoreGpu(device, requirements.surface, &requirements, &preferences, &candidate);
		if(score > 0){
			suitableDevices[suitableCount++] = candidate;
		}
		if(score > bestScore){
			bestScore = score;
			bestGpu = i;
		}
	}
	if(suitableCount == 0){
		avError("No suitable devices present");
		avFree(suitableDevices);
		avFree(devices);
	}
	avFree(devices);
	renderer->devices = suitableDevices;
	renderer->deviceCount = suitableCount;
	renderer->gpu = &suitableDevices[bestGpu];
	return true;
}

bool32 gpuSelect(Renderer* renderer, GpuRequirements requirements, GpuPreferences preferences, Gpu* gpu){
	uint32 deviceCount = 0;
	vkEnumeratePhysicalDevices(renderer->instance.handle, &deviceCount, NULL);
	if(deviceCount==0){
		avError("No vulkan capable devices present");
		return false;
	}
	VkPhysicalDevice* devices = avAllocate(sizeof(VkPhysicalDevice)*deviceCount, "");
	vkEnumeratePhysicalDevices(renderer->instance.handle, &deviceCount, devices);

	int bestScore = 0;
	VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
	Gpu bestGpu = {0};
	for(uint32 i = 0; i < deviceCount; i++){
		VkPhysicalDevice device = devices[i];
		Gpu candidate;
		int score = scoreGpu(device, requirements.surface, &requirements, &preferences, &candidate);
		if(score > bestScore){
			bestScore = score;
			bestDevice = device;
			bestGpu = candidate;
		}
	}
	avFree(devices);
	if(bestDevice==VK_NULL_HANDLE){
		avError("Failed to find suitable gpu");
		return false;
	}
	*gpu = bestGpu;
	renderer->gpuRequirements = requirements;
	return true;
}

bool32 gpuActivate(Renderer* renderer, Gpu* gpu){
	if(renderer==NULL) return false;
	if(gpu->device!=VK_NULL_HANDLE){
		avError("GPU already activated");
		return false;
	}

	GpuRequirements requirements = renderer->gpuRequirements;

	float priority = 1.0f;
	uint32 queueFamilies[4];
	uint32 queueFamilyCount = getUniqueQueueFamilies(gpu, queueFamilies);
	VkDeviceQueueCreateInfo queueInfos[4];
	for(uint32 i = 0; i < queueFamilyCount; i++){
		queueInfos[i] = (VkDeviceQueueCreateInfo){
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = queueFamilies[i],
			.queueCount = 1,
			.pQueuePriorities = &priority,
		};
	}

	VkDeviceCreateInfo deviceInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = queueFamilyCount,
		.pQueueCreateInfos = queueInfos,
		.pEnabledFeatures = &requirements.requiredFeatures.vulkan10,
		.enabledExtensionCount = requirements.requiredExtensionCount,
		.ppEnabledExtensionNames = requirements.requiredExtensions,
	};

	VkResult result = vkCreateDevice(gpu->physical, &deviceInfo, NULL, &gpu->device);
	if(result != VK_SUCCESS){
		avError("Failed to create gpu device");
		return false;
	}
	vkGetDeviceQueue(gpu->device, gpu->graphicsFamily, 0, &gpu->graphicsQueue);
	vkGetDeviceQueue(gpu->device, gpu->presentFamily, 0, &gpu->presentQueue);
	vkGetDeviceQueue(gpu->device, gpu->computeFamily, 0, &gpu->computeQueue);
	vkGetDeviceQueue(gpu->device, gpu->transferFamily, 0, &gpu->transferQueue);
	return true;
}
void gpuDeactivate(Gpu* gpu){
    if (gpu->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(gpu->device);
        vkDestroyDevice(gpu->device, NULL);
    }
    gpu->device = VK_NULL_HANDLE;
	gpu->graphicsQueue = VK_NULL_HANDLE;
	gpu->presentQueue = VK_NULL_HANDLE;
	gpu->computeQueue = VK_NULL_HANDLE;
	gpu->transferQueue = VK_NULL_HANDLE;
}

void cleanupGpus(Renderer* renderer){
	for(uint32 i = 0; i < renderer->deviceCount; i++){
		Gpu* gpu = &renderer->devices[i];
		if(gpu->device!=VK_NULL_HANDLE){
			gpuDeactivate(gpu);
		}
		avMemset(gpu, 0, sizeof(Gpu));
	}
	avFree(renderer->devices);
	renderer->deviceCount = 0;
}

bool32 gpuIsActive(Gpu* gpu){
	return gpu->device!=VK_NULL_HANDLE;
}