#include "ecsStaging.h"

#include <AvUtils/avMath.h>

static uint32 splitBlockDown(CommandBuffer* buf, uint32 offset, uint32 toClass, uint32 fromClass){
    // sanity check (optional in release)
    // fromClass must be >= toClass
    while (fromClass > toClass) {
        fromClass--;

        // size of the new split level
        uint32 splitSize = 1U << fromClass;

        // buddy is the upper half
        uint32 buddy = offset + splitSize;

        // push buddy into freelist of this class
        uint32* node = (uint32*)&buf->commandMem[buddy];
        node[0] = buf->freeList[fromClass];
        buf->freeList[fromClass] = buddy;
    }

    return offset;
}

static void addNewMemoryToFreeList(CommandBuffer* buf, uint32 oldCapacity, uint32 newCapacity) {
    // For each new level we gained
    for (uint32 i = oldCapacity; i < newCapacity && i < MAX_SIZE_CLASSES; i++) {
        uint32 offset = 1U << i;

        uint32* node = (uint32*)&buf->commandMem[offset];
        node[0] = buf->freeList[i];
        buf->freeList[i] = offset;
    }
}

static void fillFastCache(CommandBuffer* buf, uint32 class) {
    // only valid for cached classes
    if (class >= FAST_CLASS_COUNT) return;

    FastCache* cache = &buf->fastCache[class];

    // fill until full or freelist empty
    while (cache->count < FAST_CACHE_SIZE && buf->freeList[class] != __UINT32_MAX__) {

        uint32 offset = buf->freeList[class];

        // pop from freelist
        uint32* node = (uint32*)&buf->commandMem[offset];
        buf->freeList[class] = node[0];

        // push into cache
        cache->offsets[cache->count++] = offset;
    }

    if (cache->count == 0) {
        for (uint32 i = class + 1; i < MAX_SIZE_CLASSES; i++) {
            if (buf->freeList[i] != __UINT32_MAX__) {
                uint32 offset = buf->freeList[i];

                // pop
                uint32* node = (uint32*)&buf->commandMem[offset];
                buf->freeList[i] = node[0];

                // split down → this will populate freelists
                splitBlockDown(buf, offset, class, i);

                // now refill from freelist
                while (cache->count < FAST_CACHE_SIZE &&
                       buf->freeList[class] != __UINT32_MAX__) {

                    uint32 o = buf->freeList[class];
                    uint32* n = (uint32*)&buf->commandMem[o];
                    buf->freeList[class] = n[0];

                    cache->offsets[cache->count++] = o;
                }

                break;
            }
        }
    }
}

static uint32 allocateCommandBlock(CommandBuffer* buf, uint32 class){
    // if(class < FAST_CLASS_COUNT){
    //     FastCache* cache = &buf->fastCache[class];
    //     if(cache->count==0){
    //         fillFastCache(buf, class);
    //     }
    //     if(cache->count > 0){
    //         return cache->offsets[--cache->count];
    //     }
    // }

    // try free list first
    for(uint32 i = class; i < MAX_SIZE_CLASSES; i++){
        if(buf->freeList[i] != __UINT32_MAX__){
            uint32 offset = buf->freeList[i];
            uint32* node = (uint32*)&buf->commandMem[offset];
            buf->freeList[i] = node[0];            
            if(i != class){
                return splitBlockDown(buf, offset, class, i);
            }
            return offset;
        }
    }
    // no space left for class
    // allocate new space
    uint32 oldCapacity = buf->commandCapacity;
    uint32 newCapacity = AV_MAX(buf->commandCapacity + 1, class + 1); // for allocating classes that are way larger than capacity
    buf->commandCapacity = newCapacity;
    buf->commandMem = avReallocate(buf->commandMem, sizeof(Command)*(1UL<<(buf->commandCapacity)), "");
 
    
    addNewMemoryToFreeList(buf, oldCapacity, newCapacity);

    // retry allocation (now it must succeed)
    return allocateCommandBlock(buf, class);
}

static void freeCommandBlock(CommandBuffer* buf, uint32 offset, uint32 class){
    // if(class < FAST_CLASS_COUNT){
    //     FastCache* cache = &buf->fastCache[class];
    //     if(cache->count < FAST_CACHE_SIZE){
    //         cache->offsets[cache->count++] = offset;
    //         return;
    //     }
    // }
    
    uint32* node = (uint32*)&buf->commandMem[offset];
    node[0] = buf->freeList[class];
    buf->freeList[class] = offset;
}

static void growCommandList(CommandBuffer* buf, CommandList* list){
    uint32 newCapacity = list->capacity + 1;
    uint32 newOffset = allocateCommandBlock(buf, newCapacity);

    Command* newMem = &buf->commandMem[newOffset];

    if(list->commands != __UINT32_MAX__){
        Command* oldMem =  &buf->commandMem[list->commands];
        avMemcpy(newMem, oldMem, list->count * sizeof(Command));

        freeCommandBlock(buf, list->commands, list->capacity);
    }

    list->commands = newOffset;
    list->capacity = newCapacity;
}

static void growAndInsertCommand(CommandBuffer* buf, CommandList* list, Command cmd, uint32 idx){
    uint32 newCapacity = list->capacity + 1;
    uint32 newOffset = allocateCommandBlock(buf, newCapacity);
    Command* newMem = &buf->commandMem[newOffset];
    if(list->commands == __UINT32_MAX__){
        newMem[0] = cmd;
        list->commands = newOffset;
        list->count = 1;
        list->capacity = 0;
        return;
    }
    Command* oldMem = &buf->commandMem[list->commands];
    avMemcpy(newMem, oldMem, idx*sizeof(Command));
    newMem[idx] = cmd;
    avMemcpy(newMem + idx + 1, oldMem + idx, (list->count - idx) * sizeof(Command));
    freeCommandBlock(buf, list->commands, list->capacity);
    list->commands = newOffset;
    list->capacity = newCapacity;
    list->count++;
}

static uint32 findInsertIndex(CommandBuffer* buf, CommandList* list, Command cmd){
    if(list->commands==__UINT32_MAX__ || list->count==0) return 0;
    Command* base = &buf->commandMem[list->commands];
    if (list->count <= 8) {
        uint32 i = 0; // linear search
        while (i < list->count && base[i].entityId < cmd.entityId) i++;
        return i;
    }

    //binary search
    uint32 first = 0;
    uint32 last = list->count - 1;
    while(first <= last){
        uint32 mid = ((first + last)>>1);
        if(base[mid].entityId <= cmd.entityId){
            first = mid + 1;
        }else {
            last = mid - 1;
        }
    }
    return first;
}

static void pushCommand(CommandBuffer* buf, CommandList* list, Command cmd){
    uint32 idx = findInsertIndex(buf, list, cmd);
    if(list->capacity==__UINT32_MAX__ || list->count >= (1UL << list->capacity)){
        growAndInsertCommand(buf, list, cmd, idx);
        return;
    }

    Command* base = &buf->commandMem[list->commands];
    avMemmove(base + idx + 1, base + idx, (list->count - idx) * sizeof(Command));
    base[idx] = cmd;
    list->count++;
}

static void initCommandBuffer(CommandBuffer* buf){
    buf->commandMem = avAllocate(sizeof(Command), "");
    buf->commandCapacity = 0;
    for(uint32 i = 0; i < MAX_SIZE_CLASSES; i++){
        buf->freeList[i] = __UINT32_MAX__;
    }
    buf->freeList[0] = 0;
    for (uint32 i = 0; i < FAST_CLASS_COUNT; i++) {
        buf->fastCache[i].count = 0;
    }
}

static void deinitCommandBuffer(CommandBuffer* buf){
    avFree(buf->commandMem);
}

static void resetCommandBuffer(CommandBuffer* buf){
    for(uint32 i = 0; i < MAX_SIZE_CLASSES; i++){
        buf->freeList[i] = __UINT32_MAX__;
    }
    buf->freeList[buf->commandCapacity] = 0;
    *((uint32*)&buf->commandMem[0]) = __UINT32_MAX__;
}

static void initCommandList(CommandBuffer* buf, CommandList* list){
    list->capacity = __UINT32_MAX__;
    list->count = 0;
    list->commands = __UINT32_MAX__;
}

static void initCommandBucket(CommandBuffer* buffer, CommandBucket* bucket){
    for(uint32 i = 0; i < MAX_COMPONENT_COUNT; i++){
        initCommandList(buffer, &bucket->commands[i]);
    }
    bucket->mask = (ComponentMask){0};
}

static void drainFastCacheClass(CommandBuffer* buf, uint32 class) {
    if (class >= FAST_CLASS_COUNT) return;

    FastCache* cache = &buf->fastCache[class];

    while (cache->count > 0) {
        uint32 offset = cache->offsets[--cache->count];

        uint32* node = (uint32*)&buf->commandMem[offset];
        node[0] = buf->freeList[class];
        buf->freeList[class] = offset;
    }
}

static void drainAllFastCaches(CommandBuffer* buf) {
    for (uint32 class = 0; class < FAST_CLASS_COUNT; class++) {
        drainFastCacheClass(buf, class);
    }
}

static void fillAllFastCaches(CommandBuffer* buf){
    for (uint32 class = 0; class < FAST_CLASS_COUNT; class++) {
        fillFastCache(buf, class);
    }
}

static void resetFastCache(CommandBuffer* buf){
    for (uint32 i = 0; i < FAST_CLASS_COUNT; i++) {
        buf->fastCache[i].count = 0;
    }
}


static uint32 dataBlobStore(CommandBuffer* buf, uint32 size, void* data){
    if(buf->dataOffset + size >= buf->dataBlobCapacity){
        buf->dataBlobCapacity *= 2;
        buf->dataBlob = avReallocate(buf->dataBlob, buf->dataBlobCapacity, "");
    }
    uint32 offset = buf->dataOffset;
    buf->dataOffset += size;
    avMemcpy(buf->dataBlob + offset, data, size);
    return offset;
}

static void dataBlobReset(CommandBuffer* buf){
    buf->dataOffset = 0;
}

static void dataBlobInit(CommandBuffer* buf){
    buf->dataBlobCapacity = 1;
    buf->dataBlob = avAllocate(1, "");
    buf->dataOffset = 0;
}

static void dataBlobDeinit(CommandBuffer* buf){
    buf->dataBlobCapacity = 0;
    buf->dataOffset = 0;
    avFree(buf->dataBlob);
}

static void commandSubmit(CommandBuffer* buffer, CommandType type, ComponentType comp, Command command){
    switch(type){
        default:
        case CMD_COMPONENT_ADD:
            pushCommand(buffer, &buffer->addCommands.commands[comp], command);
            MASK_ADD_COMPONENT(buffer->addCommands.mask, comp);
            return;
        case CMD_COMPONENT_REMOVE:
            pushCommand(buffer, &buffer->removeCommands.commands[comp], command);
            MASK_ADD_COMPONENT(buffer->removeCommands.mask, comp);
            return;
        case CMD_ENTITY_DESTROY:
            pushCommand(buffer, &buffer->destroyCommands, command);
            return;
    }
}

void commandBufferReset(CommandBuffer* buffer){
    initCommandBucket(buffer, &buffer->addCommands);
    initCommandBucket(buffer, &buffer->removeCommands);
    initCommandList(buffer, &buffer->destroyCommands);
    resetFastCache(buffer);
    resetCommandBuffer(buffer);
    dataBlobReset(buffer);
}

void commandBufferCreate(CommandBuffer* buffer){
    initCommandBuffer(buffer);
    commandBufferReset(buffer);
    dataBlobInit(buffer);
}

void commandBufferDestroy(CommandBuffer* buffer){
    deinitCommandBuffer(buffer);
    dataBlobDeinit(buffer);
}

void cmdEntityAddComponent(Scene scene, CommandBuffer* buffer, Entity entity, ComponentType type, void* constructorData, uint32 constructorDataSize){
    (void)scene;
    
    uint32 offset = dataBlobStore(buffer, constructorDataSize, constructorData);

    Command command = {
        .dataOffset = offset,
        .dataSize = constructorDataSize,
        .entityId = entity,
    };
    commandSubmit(buffer, CMD_COMPONENT_ADD, type, command);
}

void cmdEntityRemoveComponent(Scene scene, CommandBuffer* buffer, Entity entity, ComponentType type){
    Command command = {
        .entityId = entity,
    };
    commandSubmit(buffer, CMD_COMPONENT_REMOVE, type, command);
}

void cmdEntityDestroy(Scene scene, CommandBuffer* buffer, Entity entity){
    Command command = {
        .entityId = entity,
    };
    commandSubmit(buffer, CMD_ENTITY_DESTROY, 0, command);
}

static Entity findLowestEntityID(CommandBuffer* buffers, uint32 threadCount, uint32*** indices, uint32* destroyIndices){
    Entity entity = INVALID_ENTITY;
    for(uint32 t = 0; t < threadCount; t++){
        CommandBuffer* buf = &buffers[t];
        const CommandBucket* buckets[2];
        buckets[CMD_COMPONENT_ADD] = &buf->addCommands;
        buckets[CMD_COMPONENT_REMOVE] = &buf->removeCommands;
        for(uint32 b = 0; b < sizeof(buckets)/sizeof(CommandBucket*); b++){
            const CommandBucket* bucket = buckets[b];
            for(uint32 c = 0; c < MAX_COMPONENT_COUNT; c++){
                if(!MASK_HAS_COMPONENT(bucket->mask, c)) continue;
                const CommandList* list = &bucket->commands[c];
                if(list->count==0) continue;
                Command* base = &buf->commandMem[list->commands];
                Entity head = base[indices[t][b][c]].entityId;
                if(head < entity){
                    entity = head;
                }
            }
        }
        CommandList* list = &buf->destroyCommands;
        if(list->count!=0) {
            Command* base = &buf->commandMem[list->commands];
            Entity head = base[destroyIndices[t]].entityId;
            if(head < entity){
                entity = head;
            }
        }
    }
    return entity;
}


typedef struct {
    CommandBuffer* buffer;
    Command* base;
    uint32 index;
    uint32 count;
    Entity current;

    uint32 component;
    CommandType type;
} StreamHead;

static inline void advanceStream(StreamHead* h) {
    h->index++;

    if (h->index < h->count) {
        h->current = h->base[h->index].entityId;
    } else {
        h->current = INVALID_ENTITY; // or max sentinel
    }
}

static void collectStreamHeads(CommandBuffer* buffers, uint32 threadCount, StreamHead* heads, uint32* headCount){
    for(uint32 t = 0; t < threadCount; t++){
        ITERATE_MASK(buffers[t].addCommands.mask, c){
            CommandBuffer* buffer = &buffers[t];
            CommandList* list = &buffer->addCommands.commands[c];
            StreamHead head = {
                .base = &buffer->commandMem[list->commands],
                .component = c,
                .buffer = buffer,
                .count = list->count,
                .index = 0,
            };
            head.current = head.base[0].entityId;
            head.type = CMD_COMPONENT_ADD;
            heads[(*headCount)++] = head;
        }
        ITERATE_MASK(buffers[t].removeCommands.mask, c){
            CommandBuffer* buffer = &buffers[t];
            CommandList* list = &buffer->removeCommands.commands[c];
            StreamHead head = {
                .base = &buffer->commandMem[list->commands],
                .component = c,
                .buffer = buffer,
                .count = list->count,
                .index = 0,
            };
            head.current = head.base[0].entityId;
            head.type = CMD_COMPONENT_REMOVE;
            heads[(*headCount)++] = head;
        }
        CommandList* list = &buffers[t].destroyCommands;
        if(list->count == 0) continue;
        StreamHead head = {
            .base = &buffers[t].commandMem[list->commands],
            .count = list->count,
            .buffer = &buffers[t],
            .index = 0,
        };
        head.current = head.base[0].entityId;
        head.type = CMD_ENTITY_DESTROY;
        heads[(*headCount)++] = head;
    }
}


static void collectEntityOperations(Entity entity, StreamHead* heads, uint32* headCount, ComponentMask* removeComponents, ComponentMask* addComponents, byte** constructorData, uint32* constructorSizes, uint32* constructorCount, bool32* destroyEntity){
    for(uint32 i = 0; i < *headCount; i++){
        StreamHead* head = &heads[i];
        if(head->current != entity) continue;
        while (head->index < head->count && head->base[head->index].entityId == entity) {
            switch(head->type){
                case CMD_COMPONENT_ADD:
                    if(MASK_HAS_COMPONENT(*addComponents, head->component)){
                        avError("Tried to add duplicate components");
                        break;
                    }
                    MASK_ADD_COMPONENT(*addComponents, head->component);
                    constructorData[*constructorCount] = head->buffer->dataBlob + head->base[head->index].dataOffset;
                    constructorSizes[*constructorCount] = head->base[head->index].dataSize;
                    (*constructorCount)++;
                    break;
                case CMD_COMPONENT_REMOVE:
                    if(MASK_HAS_COMPONENT(*removeComponents, head->component)){
                        avWarn("Tried to double remove components");
                        break;
                    }
                    MASK_ADD_COMPONENT(*removeComponents, head->component);
                    break;
                case CMD_ENTITY_DESTROY:
                    if(*destroyEntity){
                        avWarn("Tried to double destroy entity");
                        break;
                    }
                    *destroyEntity = true;
                    break;
            }
            head->index++;

        }

        if(head->index < head->count){
            head->current = head->base[head->index].entityId;
        }else{
            head->current = INVALID_ENTITY;
            heads[i] = heads[--(*headCount)];
            i--;
        } 
    }
}

EntityChunk* getLocalEntityChunk(Entity localEntity);
bool32 createEmptyEntity(Scene scene, Entity entity, ComponentMask mask, EntityType** entityType, LocalEntity* locEntity, EntityChunk** chunkPtr);
void performDestructorMasked(Scene scene, uint32 i, EntityChunk* chunk, EntityType* type, ComponentMask mask);
EntityType* findOrCreateEntityType(Scene scene, ComponentMask mask);
bool32 moveEntity(Scene scene, Entity src, LocalEntity dst);
LocalEntity createLocalEntity(EntityType* type, EntityChunk** chunkPtr, uint32* localIndexPtr);
void performConstructorMasked(Scene scene, uint32 i, EntityChunk* chunk, EntityType* type, ComponentMask mask, void** constructorData, uint32* constructorDataSize);
bool32 removeEntity(Scene scene, Entity entity, EntityChunk* chunk, EntityType* type);

void applyCommandBuffers(Scene scene, CommandBuffer* buffers, uint32 threadCount){
    StreamHead heads[threadCount * (2*MAX_COMPONENT_COUNT + 1)];
    uint32 headCount = 0;


    uint32 addCommands = 0;
    uint32 removeCommands = 0;
    uint32 destroyCommands = 0;
    for(uint32 i = 0; i < threadCount; i++){
        CommandBuffer* buffer = buffers + i;
        for(uint32 j = 0; j < MAX_COMPONENT_COUNT; j++){
            addCommands += buffer->addCommands.commands[j].count;
            removeCommands += buffer->removeCommands.commands[j].count;
        }
        destroyCommands += buffer->destroyCommands.count;
    }
    avDebug("%u add, %u remove, %u destroy", addCommands, removeCommands, destroyCommands);

    collectStreamHeads(buffers, threadCount, heads, &headCount);
    
    if(headCount == 0) return;

    while(headCount > 0){
        uint32 best = 0;
        for(uint32 i = 1; i < headCount; i++){
            if(heads[i].current < heads[best].current){
                best = i;
            }
        }

        ComponentMask removeComponents = {0};
        ComponentMask addComponents = {0};
        byte* constructorData[MAX_COMPONENT_COUNT];
        uint32 constructorSizes[MAX_COMPONENT_COUNT];
        uint32 constructorCount = 0;
        bool32 destroyEntity = false;

        Entity entity = heads[best].current;
        if(entity == INVALID_ENTITY){
            avError("Tried to process invalid entity");
            heads[best] = heads[--headCount];
            continue;
        }
        collectEntityOperations(entity, heads, &headCount, &removeComponents, &addComponents, constructorData, constructorSizes, &constructorCount, &destroyEntity);
        
        //process entity
        LocalEntity localEntity = INVALID_ENTITY;
        bool8 staged = false;
        if(!getEntityDetails(scene, entity, NULL, &localEntity, NULL, &staged)){
            avError("Tried to access invalid entity");
            break;
        }

        EntityChunk* chunk;
        EntityType* type;
        if(staged){
            // we need to create new entity;
            if(!componentMaskIsEmpty(removeComponents)){
                avWarn("Tried to remove component not in entity");
            }
            if(destroyEntity && componentMaskIsEmpty(addComponents)){
                freeEntityID(scene, entity);
                continue; // no need to create entity as it has never been initialized and directly removed
            }

            ComponentMask mask = addComponents;
            if(!createEmptyEntity(scene, entity, mask, &type, &localEntity, &chunk)){
                avError("failed to create entity");
            }
            performConstructorMasked(scene, ENTITY_LOCAL_INDEX(localEntity), chunk, type, addComponents, (void**)constructorData, constructorSizes);
            if(destroyEntity){
                performDestructorMasked(scene, ENTITY_LOCAL_INDEX(localEntity), chunk, type, addComponents);
                removeEntity(scene, entity, chunk, type);
            }

        }else{
            chunk = getLocalEntityChunk(localEntity);
            type = getEntityType(scene, chunk->entityType);

            ComponentMask doubleRemoves = componentMaskAnd(componentMaskInvert(type->mask), removeComponents);
            ComponentMask doubleAdds = componentMaskAnd(type->mask, addComponents);
            if(!componentMaskIsEmpty(doubleRemoves)) avWarn("Tried to remove component not in entity");
            if(!componentMaskIsEmpty(doubleAdds)) avError("Tried to add duplicate component");

            ComponentMask componentSwaps = componentMaskAnd(addComponents, removeComponents);
            ComponentMask actualRemoves = componentMaskAnd(componentMaskInvert(componentSwaps), removeComponents);
            ComponentMask newMask = componentMaskOr(componentMaskAnd(type->mask, componentMaskInvert(actualRemoves)), addComponents);

            performDestructorMasked(scene, ENTITY_LOCAL_INDEX(localEntity), chunk, type, removeComponents);
            type = findOrCreateEntityType(scene, newMask);
            localEntity = createLocalEntity(type, &chunk, NULL);
            moveEntity(scene, entity, localEntity);


            performConstructorMasked(scene, ENTITY_LOCAL_INDEX(localEntity), chunk, type, addComponents, (void**)constructorData, constructorSizes);

            if(destroyEntity){
                performDestructorMasked(scene, ENTITY_LOCAL_INDEX(localEntity), chunk, type, componentMaskOr(componentMaskAnd(type->mask, componentMaskInvert(removeComponents)), addComponents));
                removeEntity(scene, entity, chunk, type);
            }
        }


    }
    for(uint32 i = 0; i < threadCount; i++){
        resetCommandBuffer(&buffers[i]);
    }

}