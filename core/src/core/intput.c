#include "core/input.h"
#include "core/event.h"
#include <AvUtils/avMemory.h>

typedef struct keyboard_state {
    bool8 keys[256];
} keyboard_state;

typedef struct mouse_state {
    int16 x;
    int16 y;
    bool8 buttons[BUTTON_MAX_BUTTONS];
    bool8 dragging[BUTTON_MAX_BUTTONS];
} mouse_state;

typedef struct input_state {
    keyboard_state keyboardCurrent;
    keyboard_state keyboardPrevious;
    mouse_state mouseCurrent;
    mouse_state mousePrevious;

    //stack keymap_stack;
    // keymap active_keymap;
    bool8 allowKeyRepeats;
} input_state;

// Internal input state pointer
static input_state* state;


bool8 inputSystemInitialize(uint64* memory_requirement, void* statePtr, void* config) {
    *memory_requirement = sizeof(input_state);
    if (statePtr == 0) {
        return true;
    }
    avMemset(statePtr, 0, sizeof(input_state));
    state = statePtr;

    // Create the keymap stack and an active keymap to apply to.
    //stack_create(&state->keymap_stack, sizeof(keymap));
    // state->active_keymap = keymap_create();

    state->allowKeyRepeats = false;

    return true;
}

void inputSystemShutdown(void* state) {
    // TODO: Add shutdown routines when needed.
    state = 0;
}

void inputUpdate() {
    if (!state) {
        return;
    }

    // // Handle hold bindings.
    // for (uint32 k = 0; k < KEYS_MAX_KEYS; ++k) {
    //     keys key = (keys)k;
    //     if (input_is_key_down(key) && input_was_key_down(key)) {
    //         uint32 map_count = state->keymap_stack.element_count;
    //         keymap* maps = (keymap*)state->keymap_stack.memory;
    //         for (int32 m = map_count - 1; m >= 0; --m) {
    //             keymap* map = &maps[m];
    //             keymap_binding* binding = &map->entries[key].bindings[0];
    //             bool8 unset = false;
    //             while (binding) {
    //                 // If an unset is detected, stop processing.
    //                 if (binding->type == KEYMAP_BIND_TYPE_UNSET) {
    //                     unset = true;
    //                     break;
    //                 } else if (binding->type == KEYMAP_BIND_TYPE_HOLD) {
    //                     if (binding->callback && checkModifiers(binding->modifiers)) {
    //                         binding->callback(key, binding->type, binding->modifiers, binding->user_data);
    //                     }
    //                 }

    //                 binding = binding->next;
    //             }
    //             // If an unset is detected or the map is marked to override all, stop processing.
    //             if (unset || map->overrides_all) {
    //                 break;
    //             }
    //         }
    //     }
    // }

    // Copy current states to previous states.
    avMemcpy(&state->keyboardPrevious, &state->keyboardCurrent, sizeof(keyboard_state));
    avMemcpy(&state->mousePrevious, &state->mouseCurrent, sizeof(mouse_state));
}

static bool8 checkModifiers(modifiers modifiers) {
    if (modifiers & MODIFIER_SHIFT_BIT) {
        if (!inputIsKeyDown(KEY_SHIFT) && !inputIsKeyDown(KEY_LSHIFT) && !inputIsKeyDown(KEY_RSHIFT)) {
            return false;
        }
    }
    if (modifiers & MODIFIER_CONTROL_BIT) {
        if (!inputIsKeyDown(KEY_CONTROL) && !inputIsKeyDown(KEY_LCONTROL) && !inputIsKeyDown(KEY_RCONTROL)) {
            return false;
        }
    }
    if (modifiers & MODIFIER_ALT_BIT) {
        if (!inputIsKeyDown(KEY_LALT) && !inputIsKeyDown(KEY_RALT)) {
            return false;
        }
    }

    return true;
}

void inputProcessKey(Keys key, bool8 pressed) {
    if (!state) {
        return;
    }
    // keymap_entry* map_entry = &state->active_keymap.entries[key];

    // Only handle this if the state actually changed, or if repeats are allowed.
    bool8 is_repeat = pressed && state->keyboardCurrent.keys[key];
    bool8 changed = state->keyboardCurrent.keys[key] != pressed;
    if (state->allowKeyRepeats || changed) {
        // Update internal state.
        state->keyboardCurrent.keys[key] = pressed;

        // if (key == KEY_LALT) {
        //     KINFO("Left alt %s.", pressed ? "pressed" : "released");
        // } else if (key == KEY_RALT) {
        //     KINFO("Right alt %s.", pressed ? "pressed" : "released");
        // }

        // if (key == KEY_LCONTROL) {
        //     KINFO("Left ctrl %s.", pressed ? "pressed" : "released");
        // } else if (key == KEY_RCONTROL) {
        //     KINFO("Right ctrl %s.", pressed ? "pressed" : "released");
        // }

        // if (key == KEY_LSHIFT) {
        //     KINFO("Left shift %s.", pressed ? "pressed" : "released");
        // } else if (key == KEY_RSHIFT) {
        //     KINFO("Right shift %s.", pressed ? "pressed" : "released");
        // }

        // Check for key bindings
        // Iterate keymaps top-down on the stack.
        // uint32 map_count = state->keymap_stack.element_count;
        // keymap* maps = (keymap*)state->keymap_stack.memory;
        // for (int32 m = map_count - 1; m >= 0; --m) {
        //     keymap* map = &maps[m];
        //     keymap_binding* binding = &map->entries[key].bindings[0];
        //     bool8 unset = false;
        //     while (binding) {
        //         // If an unset is detected, stop processing.
        //         if (binding->type == KEYMAP_BIND_TYPE_UNSET) {
        //             unset = true;
        //             break;
        //         } else if (pressed && binding->type == KEYMAP_BIND_TYPE_PRESS) {
        //             if (binding->callback && checkModifiers(binding->modifiers)) {
        //                 binding->callback(key, binding->type, binding->modifiers, binding->user_data);
        //             }
        //         } else if (!pressed && binding->type == KEYMAP_BIND_TYPE_RELEASE) {
        //             if (binding->callback && checkModifiers(binding->modifiers)) {
        //                 binding->callback(key, binding->type, binding->modifiers, binding->user_data);
        //             }
        //         }

        //         binding = binding->next;
        //     }
        //     // If an unset is detected or the map is marked to override all, stop processing.
        //     if (unset || map->overrides_all) {
        //         break;
        //     }
        // }

        // Fire off an event for immediate processing.
        EventContext context;
        context.data.u16[0] = key;
        context.data.u16[1] = is_repeat ? 1 : 0;
        eventFire(pressed ? EVENT_CODE_KEY_PRESSED : EVENT_CODE_KEY_RELEASED, 0, context);
    }
}

void inputProcessButton(Buttons button, bool8 pressed) {
    // If the state changed, fire an event.
    if (state->mouseCurrent.buttons[button] != pressed) {
        state->mouseCurrent.buttons[button] = pressed;

        // Fire the event.
        EventContext context;
        context.data.u16[0] = button;
        context.data.i16[1] = state->mouseCurrent.x;
        context.data.i16[2] = state->mouseCurrent.y;
        eventFire(pressed ? EVENT_CODE_BUTTON_PRESSED : EVENT_CODE_BUTTON_RELEASED, 0, context);
    }

    // Check for drag releases.
    if (!pressed) {
        if (state->mouseCurrent.dragging[button]) {
            // Issue a drag end event.

            state->mouseCurrent.dragging[button] = false;
            // KTRACE("mouse drag ended at: x:%hi, y:%hi, button: %hu", state->mouse_current.x, state->mouse_current.y, button);

            EventContext context;
            context.data.i16[0] = state->mouseCurrent.x;
            context.data.i16[1] = state->mouseCurrent.y;
            context.data.u16[2] = button;
            eventFire(EVENT_CODE_MOUSE_DRAG_END, 0, context);
        } else {
            // If not a drag release, then it is a click.

            // Fire the event.
            EventContext context;
            context.data.u16[0] = button;
            context.data.i16[1] = state->mouseCurrent.x;
            context.data.i16[2] = state->mouseCurrent.y;
            eventFire(EVENT_CODE_BUTTON_CLICKED, 0, context);
        }
    }
}

void inputProcessMouseMove(int16 x, int16 y) {
    // Only process if actually different
    if (state->mouseCurrent.x != x || state->mouseCurrent.y != y) {
        // NOTE: Enable this if debugging.
        // KDEBUG("Mouse pos: %i, %i!", x, y);

        // Update internal state->
        state->mouseCurrent.x = x;
        state->mouseCurrent.y = y;

        // Fire the event.
        EventContext context;
        context.data.i16[0] = x;
        context.data.i16[1] = y;
        eventFire(EVENT_CODE_MOUSE_MOVED, 0, context);

        for (uint16 i = 0; i < BUTTON_MAX_BUTTONS; ++i) {
            // Check if the button is down first.
            if (state->mouseCurrent.buttons[i]) {
                if (!state->mousePrevious.dragging[i] && !state->mouseCurrent.dragging[i]) {
                    // Start a drag for this button.

                    state->mouseCurrent.dragging[i] = true;

                    EventContext drag_context;
                    drag_context.data.i16[0] = state->mouseCurrent.x;
                    drag_context.data.i16[1] = state->mouseCurrent.y;
                    drag_context.data.u16[2] = i;
                    eventFire(EVENT_CODE_MOUSE_DRAG_BEGIN, 0, drag_context);
                    // KTRACE("mouse drag began at: x:%hi, y:%hi, button: %hu", state->mouse_current.x, state->mouse_current.y, i);
                } else if (state->mouseCurrent.dragging[i]) {
                    // Issue a continuance of the drag operation.
                    EventContext drag_context;
                    drag_context.data.i16[0] = state->mouseCurrent.x;
                    drag_context.data.i16[1] = state->mouseCurrent.y;
                    drag_context.data.u16[2] = i;
                    eventFire(EVENT_CODE_MOUSE_DRAGGED, 0, drag_context);
                    // KTRACE("mouse drag continued at: x:%hi, y:%hi, button: %hu", state->mouse_current.x, state->mouse_current.y, i);
                }
            }
        }
    }
}

void inputProcessMouseWheel(int8 z_delta) {
    // NOTE: no internal state to update.

    // Fire the event.
    EventContext context;
    context.data.i8[0] = z_delta;
    eventFire(EVENT_CODE_MOUSE_WHEEL, 0, context);
}

void inputKeyRepeatsEnable(bool8 enable) {
    if (state) {
        state->allowKeyRepeats = enable;
    }
}

bool8 inputIsKeyDown(Keys key) {
    if (!state) {
        return false;
    }
    return state->keyboardCurrent.keys[key] == true;
}

bool8 inputIsKeyUp(Keys key) {
    if (!state) {
        return true;
    }
    return state->keyboardCurrent.keys[key] == false;
}

bool8 inputWasKeyDown(Keys key) {
    if (!state) {
        return false;
    }
    return state->keyboardPrevious.keys[key] == true;
}

bool8 inputWasKeyUp(Keys key) {
    if (!state) {
        return true;
    }
    return state->keyboardPrevious.keys[key] == false;
}

// mouse input
bool8 inputIsButtonDown(Buttons button) {
    if (!state) {
        return false;
    }
    return state->mouseCurrent.buttons[button] == true;
}

bool8 inputIsButtonUp(Buttons button) {
    if (!state) {
        return true;
    }
    return state->mouseCurrent.buttons[button] == false;
}

bool8 inputWasButtonDown(Buttons button) {
    if (!state) {
        return false;
    }
    return state->mousePrevious.buttons[button] == true;
}

bool8 inputWasButtonUp(Buttons button) {
    if (!state) {
        return true;
    }
    return state->mousePrevious.buttons[button] == false;
}

bool8 inputIsButtonDragging(Buttons button) {
    if (!state) {
        return false;
    }

    return state->mouseCurrent.dragging[button];
}

void inputGetMousePosition(int32* x, int32* y) {
    if (!state) {
        *x = 0;
        *y = 0;
        return;
    }
    *x = state->mouseCurrent.x;
    *y = state->mouseCurrent.y;
}

void inputGetPreviousMousePosition(int32* x, int32* y) {
    if (!state) {
        *x = 0;
        *y = 0;
        return;
    }
    *x = state->mousePrevious.x;
    *y = state->mousePrevious.y;
}

const char* inputKeycodeStr(Keys key) {
    switch (key) {
        case KEY_BACKSPACE:
            return "backspace";
        case KEY_ENTER:
            return "enter";
        case KEY_TAB:
            return "tab";
        case KEY_SHIFT:
            return "shift";
        case KEY_CONTROL:
            return "ctrl";
        case KEY_PAUSE:
            return "pause";
        case KEY_CAPITAL:
            return "capslock";
        case KEY_ESCAPE:
            return "esc";

        case KEY_CONVERT:
            return "ime_convert";
        case KEY_NONCONVERT:
            return "ime_noconvert";
        case KEY_ACCEPT:
            return "ime_accept";
        case KEY_MODECHANGE:
            return "ime_modechange";

        case KEY_SPACE:
            return "space";
        case KEY_PAGEUP:
            return "pageup";
        case KEY_PAGEDOWN:
            return "pagedown";
        case KEY_END:
            return "end";
        case KEY_HOME:
            return "home";
        case KEY_LEFT:
            return "left";
        case KEY_UP:
            return "up";
        case KEY_RIGHT:
            return "right";
        case KEY_DOWN:
            return "down";
        case KEY_SELECT:
            return "select";
        case KEY_PRINT:
            return "print";
        case KEY_EXECUTE:
            return "execute";
        case KEY_PRINTSCREEN:
            return "printscreen";
        case KEY_INSERT:
            return "insert";
        case KEY_DELETE:
            return "delete";
        case KEY_HELP:
            return "help";

        case KEY_0:
            return "0";
        case KEY_1:
            return "1";
        case KEY_2:
            return "2";
        case KEY_3:
            return "3";
        case KEY_4:
            return "4";
        case KEY_5:
            return "5";
        case KEY_6:
            return "6";
        case KEY_7:
            return "7";
        case KEY_8:
            return "8";
        case KEY_9:
            return "9";

        case KEY_A:
            return "a";
        case KEY_B:
            return "b";
        case KEY_C:
            return "c";
        case KEY_D:
            return "d";
        case KEY_E:
            return "e";
        case KEY_F:
            return "f";
        case KEY_G:
            return "g";
        case KEY_H:
            return "h";
        case KEY_I:
            return "i";
        case KEY_J:
            return "j";
        case KEY_K:
            return "k";
        case KEY_L:
            return "l";
        case KEY_M:
            return "m";
        case KEY_N:
            return "n";
        case KEY_O:
            return "o";
        case KEY_P:
            return "p";
        case KEY_Q:
            return "q";
        case KEY_R:
            return "r";
        case KEY_S:
            return "s";
        case KEY_T:
            return "t";
        case KEY_U:
            return "u";
        case KEY_V:
            return "v";
        case KEY_W:
            return "w";
        case KEY_X:
            return "x";
        case KEY_Y:
            return "y";
        case KEY_Z:
            return "z";

        case KEY_LSUPER:
            return "l_super";
        case KEY_RSUPER:
            return "r_super";
        case KEY_APPS:
            return "apps";

        case KEY_SLEEP:
            return "sleep";

            // Numberpad keys
        case KEY_NUMPAD0:
            return "numpad_0";
        case KEY_NUMPAD1:
            return "numpad_1";
        case KEY_NUMPAD2:
            return "numpad_2";
        case KEY_NUMPAD3:
            return "numpad_3";
        case KEY_NUMPAD4:
            return "numpad_4";
        case KEY_NUMPAD5:
            return "numpad_5";
        case KEY_NUMPAD6:
            return "numpad_6";
        case KEY_NUMPAD7:
            return "numpad_7";
        case KEY_NUMPAD8:
            return "numpad_8";
        case KEY_NUMPAD9:
            return "numpad_9";
        case KEY_MULTIPLY:
            return "numpad_mult";
        case KEY_ADD:
            return "numpad_add";
        case KEY_SEPARATOR:
            return "numpad_sep";
        case KEY_SUBTRACT:
            return "numpad_sub";
        case KEY_DECIMAL:
            return "numpad_decimal";
        case KEY_DIVIDE:
            return "numpad_div";

        case KEY_F1:
            return "f1";
        case KEY_F2:
            return "f2";
        case KEY_F3:
            return "f3";
        case KEY_F4:
            return "f4";
        case KEY_F5:
            return "f5";
        case KEY_F6:
            return "f6";
        case KEY_F7:
            return "f7";
        case KEY_F8:
            return "f8";
        case KEY_F9:
            return "f9";
        case KEY_F10:
            return "f10";
        case KEY_F11:
            return "f11";
        case KEY_F12:
            return "f12";
        case KEY_F13:
            return "f13";
        case KEY_F14:
            return "f14";
        case KEY_F15:
            return "f15";
        case KEY_F16:
            return "f16";
        case KEY_F17:
            return "f17";
        case KEY_F18:
            return "f18";
        case KEY_F19:
            return "f19";
        case KEY_F20:
            return "f20";
        case KEY_F21:
            return "f21";
        case KEY_F22:
            return "f22";
        case KEY_F23:
            return "f23";
        case KEY_F24:
            return "f24";

        case KEY_NUMLOCK:
            return "num_lock";
        case KEY_SCROLL:
            return "scroll_lock";
        case KEY_NUMPAD_EQUAL:
            return "numpad_equal";

        case KEY_LSHIFT:
            return "l_shift";
        case KEY_RSHIFT:
            return "r_shift";
        case KEY_LCONTROL:
            return "l_ctrl";
        case KEY_RCONTROL:
            return "r_ctrl";
        case KEY_LALT:
            return "l_alt";
        case KEY_RALT:
            return "r_alt";

        case KEY_SEMICOLON:
            return ";";

        case KEY_APOSTROPHE:
            return "'";
        case KEY_EQUAL:
            return "=";
        case KEY_COMMA:
            return ",";
        case KEY_MINUS:
            return "-";
        case KEY_PERIOD:
            return ".";
        case KEY_SLASH:
            return "/";

        case KEY_GRAVE:
            return "`";

        case KEY_LBRACKET:
            return "[";
        case KEY_PIPE:
            return "\\";
        case KEY_RBRACKET:
            return "]";

        default:
            return "undefined";
    }
}
