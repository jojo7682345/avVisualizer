#pragma once
#include "defines.h"

static inline uint8 getMsb(uint64 x){
    if(x == 0) return (uint8) -1;
    return 64 - __builtin_clzll(x);
}

static inline uint64 getMsbValue(uint64 x) {
    if (x == 0) return 0;
    return 1ULL << getMsb(x);
}

static inline void* alignPtr(void* ptr, uint64 align){
    uint64 p = (uint64)ptr;
    uint64 aligned = (p + (align - 1)) & ~(align - 1);
    return (void*)aligned;
}