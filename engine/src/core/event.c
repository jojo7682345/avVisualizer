#include "core/event.h"
#include "containers/darray.h"
#include <AvUtils/avMemory.h>
#include "engine.h"

typedef struct RegisteredEvent {
    void* listener;
    onEventCallback callback;
} RegisteredEvent;

typedef struct EventCodeEntry {
    RegisteredEvent* events;
} EventCodeEntry;

// This should be more than enough codes...
#define MAX_MESSAGE_CODES 16384

// State structure.
typedef struct EventSystemState {
    // Lookup table for event codes.
    EventCodeEntry registered[MAX_MESSAGE_CODES];
} EventSystemState;

/**
 * Event system internal state_ptr->
 */
static EventSystemState* state;

bool8 eventSystemInitialize(uint64* memory_requirement, void* statePtr, void* config) {
    *memory_requirement = sizeof(EventSystemState);
    if (statePtr == 0) {
        return true;
    }
    avMemset(statePtr, 0, sizeof(struct EventSystemState));
    state = statePtr;

    // Notify the engine that the event system is ready for use.
    engine_on_event_system_initialized();
    

    return true;
}

void eventSystemShutdown(void* state) {
    if (state) {
        // Free the events arrays. And objects pointed to should be destroyed on their own.
        for (uint16 i = 0; i < MAX_MESSAGE_CODES; ++i) {
            if (((EventSystemState*)state)->registered[i].events != 0) {
                darrayDestroy(((EventSystemState*)state)->registered[i].events);
                ((EventSystemState*)state)->registered[i].events = 0;
            }
        }
    }
    state = 0;
}

bool8 eventRegister(uint16 code, void* listener, onEventCallback on_event) {
    if (!state) {
        return false;
    }

    if (state->registered[code].events == 0) {
        state->registered[code].events = darray_create(RegisteredEvent);
    }

    uint64 registered_count = darrayLength(state->registered[code].events);
    for (uint64 i = 0; i < registered_count; ++i) {
        if (state->registered[code].events[i].listener == listener && state->registered[code].events[i].callback == on_event) {
            //KWARN("Event has already been registered with the code %hu and the callback of %p", code, on_event);
            return false;
        }
    }

    // If at this point, no duplicate was found. Proceed with registration.
    RegisteredEvent event;
    event.listener = listener;
    event.callback = on_event;
    darrayPush(state->registered[code].events, event);

    return true;
}

bool8 eventUnregister(uint16 code, void* listener, onEventCallback on_event) {
    if (!state) {
        return false;
    }

    // On nothing is registered for the code, boot out.
    if (state->registered[code].events == 0) {
        // TODO: warn
        return false;
    }

    uint64 registered_count = darrayLength(state->registered[code].events);
    for (uint64 i = 0; i < registered_count; ++i) {
        RegisteredEvent e = state->registered[code].events[i];
        if (e.listener == listener && e.callback == on_event) {
            // Found one, remove it
            RegisteredEvent popped_event;
            darrayPopAt(state->registered[code].events, i, &popped_event);
            return true;
        }
    }

    // Not found.
    return false;
}

bool8 eventFire(uint16 code, void* sender, EventContext context) {
    if (!state) {
        return false;
    }

    // If nothing is registered for the code, boot out.
    if (state->registered[code].events == 0) {
        return false;
    }

    uint64 registered_count = darrayLength(state->registered[code].events);
    for (uint64 i = 0; i < registered_count; ++i) {
        RegisteredEvent e = state->registered[code].events[i];
        if (e.callback(code, sender, e.listener, context)) {
            // Message has been handled, do not send to other listeners.
            return true;
        }
    }

    // Not found.
    return false;
}