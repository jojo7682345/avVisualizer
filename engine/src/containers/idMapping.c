#include "idMapping.h"
#include <AvUtils/avMemory.h>

typedef struct IDmappingHeader{
    uint32 capacity;
    uint32 count;
    uint32* index;
    uint32* reference;
    uint32 stride;
    byte data[];
} IDmappingHeader;

#define MAPPING_HEADER(mapping) (((IDmappingHeader*)(mapping))-1)

void* idMappingGet(void** mapping, uint32 id){
    if(mapping==NULL || *mapping==NULL) return NULL;
    IDmappingHeader* header = MAPPING_HEADER(*mapping);
    if(id >= header->capacity) return NULL;
    uint32 index = header->index[id];
    if(index >= header->count) return NULL;
    return (byte*)(*mapping) + index * header->stride;
}

uint32 idMappingAdd(void** mapping, void* data){
    if(mapping==NULL || *mapping==NULL || data == NULL) return (uint32)-1;
    IDmappingHeader* header = MAPPING_HEADER(*mapping);
    if(header->count >= header->capacity) {
        uint32 oldCapacity = header->capacity;
        header->capacity *= 2;
        IDmappingHeader* newHeader = avAllocate(sizeof(IDmappingHeader) + header->stride * header->capacity + sizeof(uint32)*header->capacity*2, "");
        newHeader->index = (uint32*)(((byte*)header) + sizeof(IDmappingHeader) + header->stride*header->capacity);
        newHeader->reference = newHeader->index + header->capacity;
        avMemcpy(newHeader, header, sizeof(IDmappingHeader) + header->stride*oldCapacity);
        avMemcpy(newHeader->index, header->index, sizeof(uint32)*oldCapacity);
        avMemcpy(newHeader->reference, header->reference, sizeof(uint32)*oldCapacity);
        avFree(header);
        *mapping = newHeader->data;
        header = newHeader;
        for(uint32 i = oldCapacity; i < header->capacity; i++){
            avMemset(header->data +  i*header->stride, 0, header->stride);
            header->index[i] = i;
            header->reference[i] = i;
        }
    }
    uint32 index = header->count++;
    avMemcpy(header->data + index*header->stride, data, header->stride);
    return header->reference[index];
}

bool32 idMappingRemove(void** mapping, uint32 id){
    if(mapping==NULL || *mapping==NULL) return false;
    IDmappingHeader* header = MAPPING_HEADER(*mapping);
    if(id >= header->capacity) return false;
    uint32 index = header->index[id];
    if(index >= header->count) return false;
    uint32 lastIndex = header->count - 1;
    uint32 lastId = header->reference[lastIndex];
    header->count--;
    if(lastIndex==index){
        return true;
    }
    avMemswap(header->data + header->stride*index, header->data + header->stride*lastIndex, header->stride);
    header->reference[lastIndex] = id;
    header->reference[index] = lastId;
    header->index[id] = lastIndex;
    header->index[lastId] = index;
    return true;
}

bool32 idMappingCreate(void** mapping, uint32 stride){
    if(mapping==NULL || stride==0) return false;

    IDmappingHeader* header = avAllocate(sizeof(IDmappingHeader) + stride + sizeof(uint32)*2, "");
    header->capacity = 1;
    header->stride = stride;
    header->count = 0;
    header->index = (uint32*)(header->data + stride);
    header->reference = header->index + 1;
    *mapping = header;
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