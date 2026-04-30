#pragma once
#include "defines.h"

bool32 idMappingCreate(void** mapping, uint32 stride, uint32 addressSize);
bool32 idMappingDestroy(void** mapping);
uint32 idMappingAdd(void** mapping, void* data);
bool32 idMappingInsert(void** mapping, uint32 id, void* data);
bool32 idMappingRemove(void** mapping, uint32 id);
uint32 idMappingCount(void** mapping);
void* idMappingGet(void** mapping, uint32 id);
uint32 idMappingGetId(void** mapping, uint32 index);

#define MAPPING_CREATE(ptr, addressSize) idMappingCreate((void**)&(ptr), sizeof((ptr)[0]), addressSize)
#define MAPPING_DESTROY(ptr) idMappingDestroy((void**)&(ptr))
#define MAPPING_ADD(ptr, data) idMappingAdd((void**)&(ptr), &(data))
#define MAPPING_INSERT(ptr, id, data) idMappingInsert((void**)&(ptr), (id), &(data))
#define MAPPING_REMOVE(ptr, id) idMappingRemove((void**)&(ptr), (id))
#define MAPPING_GET(ptr, id) ((typeof((ptr)[0])*)idMappingGet((void**)&(ptr), (id)))
#define MAPPING_SIZE(ptr) idMappingCount((void**)&(ptr))

#define MAPPING_ID(ptr, index) idMappingGetId((void**)&(ptr), (index))