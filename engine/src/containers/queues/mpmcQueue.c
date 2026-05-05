#include "mpmcQueue.h"
#include <AvUtils/avMemory.h>

bool32 mpmcInit(MpmcQueue* q, uint32 size, uint32 stride, void* backingBuffer, void* seqBuffer){
    if ((size & (size - 1)) != 0) return false;

    q->size = size;
    q->mask = size - 1;
    q->stride = stride;

    q->data = (uint8*)backingBuffer;
    q->seq = (_Atomic uint32*)seqBuffer;

    atomic_store(&q->head, 0);
    atomic_store(&q->tail, 0);

    for (uint32 i = 0; i < size; i++) {
        atomic_store(&q->seq[i], i);
    }

    return true;
}
bool32 mpmcPush(MpmcQueue* q, void* element){
    uint32 pos;

    while (1) {
        pos = atomic_load_explicit(&q->tail, memory_order_relaxed);

        uint32 idx = pos & q->mask;
        uint32 seq = atomic_load_explicit(&q->seq[idx], memory_order_acquire);

        int32 diff = (int32)seq - (int32)pos;

        if (diff == 0) {
            if (atomic_compare_exchange_weak_explicit(
                    &q->tail,
                    &pos,
                    pos + 1,
                    memory_order_relaxed,
                    memory_order_relaxed))
            {
                void* dst = q->data + (idx * q->stride);
                avMemcpy(dst, element, q->stride);

                atomic_store_explicit(
                    &q->seq[idx],
                    pos + 1,
                    memory_order_release);

                return true;
            }
        }
        else if (diff < 0) {
            return false; // full
        }
    }
}
bool32 mpmcPop(MpmcQueue* q, void* outElement){
    uint32 pos;

    while (1) {
        pos = atomic_load_explicit(&q->head, memory_order_relaxed);

        uint32 idx = pos & q->mask;
        uint32 seq = atomic_load_explicit(&q->seq[idx], memory_order_acquire);

        int32 diff = (int32)seq - (int32)(pos + 1);

        if (diff == 0) {
            if (atomic_compare_exchange_weak_explicit(
                    &q->head,
                    &pos,
                    pos + 1,
                    memory_order_relaxed,
                    memory_order_relaxed))
            {
                void* src = q->data + (idx * q->stride);
                avMemcpy(outElement, src, q->stride);

                atomic_store_explicit(
                    &q->seq[idx],
                    pos + q->mask + 1,
                    memory_order_release);

                return true;
            }
        }
        else if (diff < 0) {
            return false; // empty
        }
    }
}