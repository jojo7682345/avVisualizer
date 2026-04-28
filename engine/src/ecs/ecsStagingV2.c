#include "ecsStagingV2.h"



static uint32 allocateCommandBlock(CommandBuffer* buf, uint32 count){
    uint32 neededSize = count;
    uint32 class = neededSize - 1;

    // try free list first
    for(uint32 i = class; i < MAX_SIZE_CLASSES; i++){
        if(buf->freeList[i] != __UINT32_MAX__){
            uint32 offset = buf->freeList[i];
            uint32* node = (uint32*)&buf->commandMem[offset];
            buf->freeList[i] = node[0];
            while(i > class){
                i--;
                uint32 splitSize = 1 << i;
                uint32 buddy = offset + splitSize;
                uint32* buddyNode = (uint32*)&buf->commandMem[buddy];
                buddyNode[0] = buf->freeList[i];
                buf->freeList[i] = buddy;
            }
            return offset;
        }
    }

    uint32 offset = (1UL<<buf->commandSize);
    
    if(offset + (1UL<<neededSize) >= (1UL<<buf->commandCapacity)){
        // allocate new space
        uint32 oldCapacity = buf->commandCapacity;
        buf->commandCapacity += 1;
        buf->commandMem = avReallocate(buf->commandMem, (1UL<<(buf->commandCapacity)), "");
        uint32 newRegion = buf->commandCapacity-1;
        uint32 newClass = oldCapacity;
        
        // split off the needed size

    }

    return offset;
}

static void freeCommandBLock(CommandBuffer* buf, uint32 offset, uint32 size){
    uint32 class = size - 1;
    uint32* node = (uint32*)&buf->commandMem[offset];
    node[0] = buf->freeList[class];
    buf->freeList[class] = offset;
}

static void growCommandList(CommandBuffer* buf, CommandList* list){
    uint32 newCapacity = list->capacity + 1;
    uint32 newOffset = allocateCommandBlock(buf, newCapacity);

    Command* newMem = &buf->commandMem[newOffset];

    if(list->commands){
        Command* oldMem =  &buf->commandMem[list->commands];
        avMemcpy(newMem, oldMem, list->count * sizeof(Command));

        freeCommandBLock(buf, list->commands, list->capacity);
    }

    list->commands = newOffset;
    list->capacity = newCapacity;
}

static void pushCOmmand(CommandBuffer* buf, CommandList* list, Command cmd){
    if(list->count >= (1UL << list->capacity)){
        growCommandList(buf, list);
    }
    Command* base = &buf->commandMem[list->commands];
    base[list->count++] = cmd;
}

static void initCommandBuffer(CommandBuffer* buf){
    buf->commandMem = avAllocate(sizeof(Command), "");
    buf->commandCapacity = 1;
    for(uint32 i = 0; i < MAX_SIZE_CLASSES; i++){
        buf->freeList[i] = __UINT32_MAX__;
    }
}

void commandBufferCreate(CommandBuffer* buffer){



}

void commandBufferDestroy(CommandBuffer* buffer){

}


Entity cmdEntityCreate(Scene scene, CommandBuffer* buffer){
    if(avThreadGetID()!=AV_MAIN_THREAD_ID){
        avError("Can only create entities from main thread");
        return INVALID_ENTITY;
    }

}

void cmdEntityAddComponent(Scene scene, CommandBuffer* buffer, Entity entity, ComponentType type, void* constructorData, uint32 constructorDataSize){

}

void cmdEntityRemoveComponent(Scene scene, CommandBuffer* buffer, Entity entity, ComponentType type){
    
}

void cmdEntityDestroy(Scene scene, CommandBuffer* buffer, Entity entity){
    if(avThreadGetID()!=AV_MAIN_THREAD_ID){
        avError("Can only create entities from main thread");
        return INVALID_ENTITY;
    }

}