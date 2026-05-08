#include "idMapping.h"
#include <AvUtils/avMemory.h>

typedef struct IDmappingHeader{
    uint32 addressCapacity;
    uint32 capacity;
    uint32 count;
    uint32 stride;
    uint32* index;
    uint32* reference;
} IDmappingHeader;

#define MAPPING_HEADER(mapping) (((IDmappingHeader*)(mapping))-1)

void* idMappingGet(void** mapping, uint32 id){
    if(mapping==NULL || *mapping==NULL) return NULL;
    IDmappingHeader* header = MAPPING_HEADER(*mapping);
    if(id >= header->addressCapacity) return NULL;
    uint32 index = header->index[id];
    if(index >= header->count) return NULL;
    return (byte*)(*mapping) + index * header->stride;
}

uint32 idMappingAdd(void** mapping, void* data){
    if(mapping==NULL || *mapping==NULL || data == NULL) return (uint32)-1;
    
    IDmappingHeader* header = MAPPING_HEADER(*mapping);
    if(header->count >= header->addressCapacity) return (uint32) -1; //full

    if(header->count >= header->capacity) {
        uint32 oldCapacity = header->capacity;
        header->capacity *= 2;
        IDmappingHeader* newHeader = avAllocate(sizeof(IDmappingHeader) + header->stride * header->capacity + sizeof(uint32)*header->capacity + sizeof(uint32)*header->addressCapacity, "");
        avMemcpy(newHeader, header, sizeof(IDmappingHeader) + header->stride*oldCapacity);
        newHeader->reference = (uint32*)(((byte*)newHeader) + sizeof(IDmappingHeader) + header->stride*header->capacity);
        newHeader->index = newHeader->reference + header->capacity;
        avMemcpy(newHeader->index, header->index, sizeof(uint32)*header->addressCapacity);
        avMemcpy(newHeader->reference, header->reference, sizeof(uint32)*oldCapacity);
        avFree(header);
        *mapping = newHeader+1;
        header = newHeader;
        for(uint32 i = oldCapacity; i < header->capacity; i++){
            avMemset((byte*)(header+1) +  i*header->stride, 0, header->stride);
            header->reference[i] = i;
        }
    }
    uint32 index = header->count++;
    avMemcpy((byte*)(header+1) + index*header->stride, data, header->stride);
    return header->reference[index];
}

bool32 idMappingRemove(void** mapping, uint32 id){
    if(mapping==NULL || *mapping==NULL) return false;
    IDmappingHeader* header = MAPPING_HEADER(*mapping);
    if(id >= header->addressCapacity) return false;
    uint32 index = header->index[id];
    if(index >= header->count) return false;
    uint32 lastIndex = header->count - 1;
    uint32 lastId = header->reference[lastIndex];
    header->count--;
    if(lastIndex==index){
        return true;
    }
    avMemswap((byte*)(header+1) + header->stride*index, (byte*)(header+1) + header->stride*lastIndex, header->stride);
    header->reference[lastIndex] = id;
    header->reference[index] = lastId;
    header->index[id] = lastIndex;
    header->index[lastId] = index;
    return true;
}

bool32 idMappingInsert(void** mapping, uint32 id, void* data){
    if(mapping==NULL || *mapping==NULL) return false;
    IDmappingHeader* header = MAPPING_HEADER(*mapping);
    if(id > header->addressCapacity) return false;
    uint32 index = header->index[id];
    if(index < header->count) return false;

    //add and swap references
    uint32 oldId = idMappingAdd(mapping, data);
    header = MAPPING_HEADER(*mapping);
    uint32 oldIndex = header->index[oldId];

    header->index[id] = oldIndex;
    header->reference[oldIndex] = id;

    header->index[oldId] = index;
    header->reference[index] = oldId;
    return true;
}

bool32 idMappingCreate(void** mapping, uint32 stride, uint32 addressCapacity){
    if(mapping==NULL || stride==0) return false;
    if(addressCapacity <= 1) addressCapacity = 1;
    IDmappingHeader* header = avAllocate(sizeof(IDmappingHeader) + stride + sizeof(uint32) + sizeof(uint32)*addressCapacity, "");
    header->capacity = 1;
    header->stride = stride;
    header->addressCapacity = addressCapacity;
    header->count = 0;
    header->reference = (uint32*)((byte*)(header+1) + stride);
    header->index = header->reference + header->capacity;
    for(uint32 i = 0; i < addressCapacity; i++){
        header->index[i] = i;
    }
    header->reference[0] = 0;
    *mapping = header+1;
    return true;
}

bool32 idMappingDestroy(void** mapping){
    if(mapping==NULL) return false;
    if(*mapping==NULL) return true;
    IDmappingHeader* header = MAPPING_HEADER(*mapping);
    avFree(header);
    return true;
}

uint32 idMappingCount(void** mapping){
    if(mapping==NULL) return 0;
    if(*mapping==NULL) return 0;
    IDmappingHeader* header = MAPPING_HEADER(*mapping);
    return header->count;
}

uint32 idMappingGetId(void** mapping, uint32 index){
    if(mapping==NULL) return (uint32)-1;
    if(*mapping==NULL) return (uint32)-1;
    IDmappingHeader* header = MAPPING_HEADER(*mapping);
    if(index >= header->count) return (uint32)-1;
    return header->reference[index];
}