#include "ecsInternal.h"

#include <AvUtils/avMath.h>


FrameData registerFrameData(Scene scene, uint64 size, uint32 alignment, bool32 resizable){
    if(size == 0) return (FrameData)-1;
    FrameDataDescriptor descr = {
        .alignment = alignment,
        .size = size,
        .capacity = resizable ? 0 : size,
        .offset = (uint64)-1,
    };

    if(scene->frameDataDescriptorCount >= MAX_COMPONENT_COUNT) return (FrameData)-1;
    if(scene->frameDataDescriptorCount >= scene->frameDataDescriptorCapacity){
        scene->frameDataDescriptorCapacity = AV_MAX(1, scene->frameDataDescriptorCapacity * 2);
        scene->frameDataDescriptors = avReallocate(scene->frameDataDescriptors, sizeof(FrameDataDescriptor), "");
    }
    FrameData frameData = scene->frameDataDescriptorCount++;
    scene->frameDataDescriptors[frameData] = descr;
    return frameData;
}

void* accessFrameData(Scene scene, FrameData resource, uint64* size){
    if(resource >= scene->frameDataDescriptorCount) return NULL;
    FrameDataDescriptor* descr = &scene->frameDataDescriptors[resource];
    if(size) *size = descr->size;
    if(descr->offset){
        avError("Detected read before write of frame data %u", (uint32)resource);
        return NULL;
    }
    return scene->frameDataMem + descr->offset;
}

bool32 frameDataResize(Scene scene, FrameData resource, uint64 newSize){
    if(resource >= scene->frameDataDescriptorCount) return false;
    FrameDataDescriptor* descr = &scene->frameDataDescriptors[resource];
    if(descr->capacity==0) return false;
    if(descr->offset!=(uint64)-1){
        return false;
    }
    if(newSize < descr->capacity) {
        descr->size = newSize;
        return true;
    }
}