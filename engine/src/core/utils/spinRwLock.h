#pragma once

#include <stdatomic.h>

typedef struct SpinRwLock SpinRwLock;

#define SPIN_RW_LOCK_MEMORY_REQUIREMENT 8

void spinRwLockInit(SpinRwLock* lock);
void spinRwLockReadLock(SpinRwLock* lock);
void spinRwLockReadUnlock(SpinRwLock* lock);
void spinRwLockWriteLock(SpinRwLock* lock);
void spinRwLockWriteUnlock(SpinRwLock* lock);