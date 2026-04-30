#pragma once 
#include "defines.h"

#define COMPONENT_MASK_SIZE 8
#define MAX_COMPONENT_COUNT (COMPONENT_MASK_SIZE * 64)
typedef struct ComponentMask{
    uint64 bits[COMPONENT_MASK_SIZE];
} ComponentMask;

#define MASK_HAS_COMPONENT(mask, componentId) (((componentId) < MAX_COMPONENT_COUNT)&&((mask).bits[(componentId) >> 6] & (1ULL<<((componentId) & (63))))!=0)
#define MASK_ADD_COMPONENT(mask, componentId) if((componentId) < MAX_COMPONENT_COUNT) ((mask).bits[(componentId) >> 6] |= 1ULL << ((componentId) & 63))
#define MASK_REMOVE_COMPONENT(mask, componentId) if((componentId) < MAX_COMPONENT_COUNT) ((mask).bits[(componentId) >> 6] &= ~((1ULL<<((componentId) & (63)))))

#define ITERATE_MASK(mask, index)                                      \
    for (uint32 __index__ = 0; __index__ < COMPONENT_MASK_SIZE; __index__++)        \
    for (uint64 __bits__ = (mask).bits[__index__]; __bits__; __bits__ &= (__bits__ - 1)) \
    for (uint32 index = (__index__ * 64 + __builtin_ctzll(__bits__)); index != ((uint32)-1); index = ((uint32)-1))

#define componentMaskMake(...) componentMaskMake_(__VA_ARGS__ __VA_OPT__(,) MAX_COMPONENT_COUNT)

static inline bool32 componentMaskContains(ComponentMask mask, ComponentMask componentMask){
    for(uint32 i = 0; i < COMPONENT_MASK_SIZE; i++) {
        if((mask.bits[i] & componentMask.bits[i]) != componentMask.bits[i]) return 0;
    }
    return 1;
}

static inline bool32 componentMaskEquals(ComponentMask maskA, ComponentMask maskB){
    for(uint32 i = 0; i < COMPONENT_MASK_SIZE; i++){
        if(maskA.bits[i] != maskB.bits[i]) return false;
    }
    return true;
}

static inline ComponentMask componentMaskAnd(ComponentMask maskA, ComponentMask maskB){
    ComponentMask mask = {0};
    for(uint32 i = 0; i < COMPONENT_MASK_SIZE; i++){
        mask.bits[i] = maskA.bits[i] & maskB.bits[i];
    }
    return mask;
}
static inline ComponentMask componentMaskOr(ComponentMask maskA, ComponentMask maskB){
    ComponentMask mask = {0};
    for(uint32 i = 0; i < COMPONENT_MASK_SIZE; i++){
        mask.bits[i] = maskA.bits[i] | maskB.bits[i];
    }
    return mask;
}

AV_API ComponentMask getRegisteredComponents();

static inline ComponentMask componentMaskInvert(ComponentMask mask){
    for(uint32 i = 0; i < COMPONENT_MASK_SIZE; i++){
        mask.bits[i] = ~mask.bits[i];
    }
    return componentMaskAnd(mask, getRegisteredComponents());
}

static inline uint32 componentMaskCount(ComponentMask mask){
    uint32 count = 0;
    for(uint32 i = 0; i < COMPONENT_MASK_SIZE; i++){
        count += __builtin_popcountll(mask.bits[i]);
    }
    return count;
}

static inline bool32 componentMaskIsEmpty(ComponentMask mask){
    for(uint32 i = 0; i < COMPONENT_MASK_SIZE; i++){
        if(mask.bits[i]) return false;
    }
    return true;
}