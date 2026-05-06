#include "clock.h"

#include "core/platform/platform.h"

void clockUpdate(Clock* clock) {
    if (clock->startTime != 0) {
        clock->elapsed = platformGetAbsoluteTime() - clock->startTime;
    }
}

void clockStart(Clock* clock) {
    clock->startTime = platformGetAbsoluteTime();
    clock->elapsed = 0;
}

void clockStop(Clock* clock) {
    clock->startTime = 0;
}