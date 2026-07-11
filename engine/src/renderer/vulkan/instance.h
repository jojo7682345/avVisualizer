#pragma once
#include <vulkan/vulkan.h>
#include <logging.h>

#include "renderer.h"

typedef struct InstanceCreateInfo {
	const char* applicationName;
	uint32 applicationVersion;
	const char* engineName;
	uint32 engineVersion;

	bool32 enableValidation;

	uint32 extensionCount;
	const char* const* extensions;
} InstanceCreateInfo;

bool32 instanceCreate(const InstanceCreateInfo* createInfo, Renderer* renderer);
void instanceDestroy(Renderer* instance);

