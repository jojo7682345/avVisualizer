#pragma once
#include "defines.h"

bool32 idMappingCreate(void** mapping, uint32 stride);
bool32 idMappingDestroy(void** mapping);
uint32 idMappingAdd(void** mapping, void* data);
bool32 idMappingRemove(void** mapping, uint32 id);
uint32 idMappingCount(void** mapping);
void* idMappingGet(void** mapping, uint32 id);

#define MAPPING_CREATE(ptr) idMappingCreate(&(ptr), sizeof((ptr)[0]))
#define MAPPING_DESTROY(ptr) idMappingDestroy(&(ptr))
#define MAPPING_ADD(ptr, data) idMappingAdd(&(ptr), &(data))
#define MAPPING_REMOVE(ptr, id) idMappingRemove(&(ptr), (id))
#define MAPPING_GET(ptr, id) ((typeof((ptr)[0])*)idMappingGet(&(ptr), (id)))
#define MAPPING_SIZE(ptr) idMappingCount(&(ptr))