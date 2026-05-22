#include "spinRwLock.h"
#include "defines.h"

#include <stdatomic.h>


struct SpinRwLock {
    _Atomic uint32 readers;
    _Atomic bool32 writer;
};

void spinRwLockInit(SpinRwLock* lock){
    atomic_init(&lock->readers, 0);
    atomic_init(&lock->writer, 0);
}

void spinRwLockReadLock(SpinRwLock* lock){
    for(;;){
        while(atomic_load_explicit(&lock->writer, memory_order_acquire)){
            __asm__ volatile("pause");
        }

        atomic_fetch_add_explicit(&lock->readers, 1, memory_order_acquire);

        if(!atomic_load_explicit(&lock->writer, memory_order_acquire)){
            return;
        }

        atomic_fetch_sub_explicit(&lock->readers, 1, memory_order_release);
    }
}

void spinRwLockReadUnlock(SpinRwLock* lock){
    atomic_fetch_sub_explicit(&lock->readers, 1, memory_order_release);
}

void spinRwLockWriteLock(SpinRwLock* lock){
    bool32 expected;
    for(;;){
        expected = false;
        if(atomic_compare_exchange_weak_explicit(&lock->writer, &expected, true, memory_order_acquire, memory_order_relaxed)){
            break;
        }
        __asm__ volatile("pause");
    }

    while(atomic_load_explicit(&lock->readers, memory_order_acquire)!=0){
        __asm__ volatile("pause");
    }
}

void spinRwLockWriteUnlock(SpinRwLock* lock){
    atomic_store_explicit(&lock->writer, false, memory_order_release);
}

