#include "core/platform.h"

#ifdef _WIN32

#include "core/systems/input.h"
#include "core/systems/event.h"
#include "engine.h"

#define WIN32_LEAN_AND_MEAN
#include <stdlib.h>
#include <windows.h>
#include <windowsx.h>  // param input extraction
#include <AvUtils/avLogging.h>

typedef struct Win32HandleInfo {
    HINSTANCE instance;
    HWND hwnd;
} Win32HandleInfo;

typedef struct PlatformState {
    Win32HandleInfo handle;
    CONSOLE_SCREEN_BUFFER_INFO stdOutputCsbi;
    CONSOLE_SCREEN_BUFFER_INFO errOutputCsbi;
    // darray
} PlatformState;

static PlatformState *state;

// Clock
static double clockFrequency;
static LARGE_INTEGER startTime;


void clock_setup(void) {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    clockFrequency = 1.0 / (double)frequency.QuadPart;
    QueryPerformanceCounter(&startTime);
}

LRESULT CALLBACK win32ProcessMessage(HWND hwnd, uint32 msg, WPARAM w_param, LPARAM l_param);

bool8 platformSystemStartup(uint64 *memoryRequirement, void *state_ptr, void *config) {
    PlatformConfig *typedConfig = (PlatformConfig *)config;
    *memoryRequirement = sizeof(PlatformState);
    if (state_ptr == 0) {
        return true;
    }
    state = state_ptr;
    state->handle.instance = GetModuleHandleA(0);

    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &state->stdOutputCsbi);
    GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE),  &state->errOutputCsbi);

    // Only available in the Creators update for Windows 10+.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // NOTE: Older versions of windows might have to use this:

    // Setup and register window class.
    HICON icon = LoadIcon(state->handle.instance, IDI_APPLICATION);
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_DBLCLKS;  // Get double-clicks
    wc.lpfnWndProc = win32ProcessMessage;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = state->handle.instance;
    wc.hIcon = icon;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);  // NULL; // Manage the cursor manually
    wc.hbrBackground = NULL;                   // Transparent
    wc.lpszClassName = "avVisualizerClass";

    if (!RegisterClassA(&wc)) {
        MessageBoxA(0, "Window registration failed", "Error", MB_ICONEXCLAMATION | MB_OK);
        return false;
    }

    // Create window
    uint32 clientX = typedConfig->x;
    uint32 clientY = typedConfig->y;
    uint32 clientWidth = typedConfig->width;
    uint32 clientHeight = typedConfig->height;

    uint32 windowX = clientX;
    uint32 windowY = clientY;
    uint32 windowWidth = clientWidth;
    uint32 windowHeight = clientHeight;

    uint32 windowStyle = WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION;
    uint32 windowExStyle = WS_EX_APPWINDOW;

    windowStyle |= WS_MAXIMIZEBOX;
    windowStyle |= WS_MINIMIZEBOX;
    windowStyle |= WS_THICKFRAME;

    // Obtain the size of the border.
    RECT borderRect = {0, 0, 0, 0};
    AdjustWindowRectEx(&borderRect, windowStyle, 0, windowExStyle);

    // In this case, the border rectangle is negative.
    windowX += borderRect.left;
    windowY += borderRect.top;

    // Grow by the size of the OS border.
    windowWidth += borderRect.right - borderRect.left;
    windowHeight += borderRect.bottom - borderRect.top;

    HWND handle = CreateWindowExA(
        windowExStyle, "avVisualizerClass", typedConfig->applicationName,
        windowStyle, windowX, windowY, windowWidth, windowHeight,
        0, 0, state->handle.instance, 0);

    if (handle == 0) {
        MessageBoxA(NULL, "Window creation failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);

        avAssert(0,"Window creation failed!");
        return false;
    } else {
        state->handle.hwnd = handle;
    }

    // Show the window
    bool32 shouldActivate = 1;  // TODO: if the window should not accept input, this should be false.
    int32 showWindowCommandFlags = shouldActivate ? SW_SHOW : SW_SHOWNOACTIVATE;
    // If initially minimized, use SW_MINIMIZE : SW_SHOWMINNOACTIVE;
    // If initially maximized, use SW_SHOWMAXIMIZED : SW_MAXIMIZE
    ShowWindow(state->handle.hwnd, showWindowCommandFlags);

    // Clock setup
    clock_setup();

    extern void initPlatformCpp(HWND hwnd);
    initPlatformCpp(handle);

    return true;
}

void platformWin32GetHWND(void* platformState, HWND* hwnd){
    (*hwnd) = state->handle.hwnd;
}

void platformGetFramebufferSize(uint32* width, uint32* height){
    RECT r;
    GetClientRect(state->handle.hwnd, &r);
    *width = r.right - r.left;
    *height = r.bottom - r.top;
}

void platformSystemShutdown(void *platformState) {
    if (state && state->handle.hwnd) {
        DestroyWindow(state->handle.hwnd);
        state->handle.hwnd = 0;
    }
}

bool8 platformPumpMessages(void) {
    if (state) {
        MSG message;
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
    }
    return true;
}

void platformConsoleWrite(const char *message, uint8 colour) {
    HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (state) {
        csbi = state->stdOutputCsbi;
    } else {
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    }

    // FATAL,ERROR,WARN,INFO,DEBUG,TRACE
    static uint8 levels[6] = {64, 4, 6, 2, 1, 8};
    SetConsoleTextAttribute(consoleHandle, levels[colour]);
    OutputDebugStringA(message);
    uint64 length = strlen(message);
    DWORD number_written = 0;
    WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), message, (DWORD)length, &number_written, 0);

    SetConsoleTextAttribute(consoleHandle, csbi.wAttributes);
}

void platformConsoleWriteError(const char *message, uint8 colour) {
    HANDLE consoleHandle = GetStdHandle(STD_ERROR_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (state) {
        csbi = state->errOutputCsbi;
    } else {
        GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE), &csbi);
    }

    // FATAL,ERROR,WARN,INFO,DEBUG,TRACE
    static uint8 levels[6] = {64, 4, 6, 2, 1, 8};
    SetConsoleTextAttribute(consoleHandle, levels[colour]);
    OutputDebugStringA(message);
    uint64 length = strlen(message);
    DWORD numberWritten = 0;
    WriteConsoleA(GetStdHandle(STD_ERROR_HANDLE), message, (DWORD)length, &numberWritten, 0);

    SetConsoleTextAttribute(consoleHandle, csbi.wAttributes);
}

double platformGetAbsoluteTime(void) {
    if (!clockFrequency) {
        clock_setup();
    }

    LARGE_INTEGER now_time;
    QueryPerformanceCounter(&now_time);
    return (double)now_time.QuadPart * clockFrequency;
}

void platformSleep(uint64 ms) {
    Sleep(ms);
}

static bool32 resizing = false;
static uint32 pendingWidth = 0;
static uint32 pendingHeight = 0;

LRESULT CALLBACK win32ProcessMessage(HWND hwnd, uint32 msg, WPARAM w_param, LPARAM l_param) {
    
    extern LRESULT platformCppProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT res = 0;
    if(res = platformCppProc(hwnd, msg, w_param, l_param)){
        return res;
    }

    switch (msg) {
        case WM_ERASEBKGND:
            // Notify the OS that erasing will be handled by the application to prevent flicker.
            return 1;
        case WM_CLOSE:
            // TODO: Fire an event for the application to quit.
            EventContext data = {};
            eventFire(EVENT(EVENT_CODE_APPLICATION_QUIT, 0, data));
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        // case WM_DPICHANGED:
        //     // x- and y-axis DPI are always the same, so just grab one.
        //     int32 x_dpi = GET_X_LPARAM(w_param);

        //     // Store off the device pixel ratio.
        //     state->device_pixel_ratio = (double)x_dpi / USER_DEFAULT_SCREEN_DPI;
        //     KINFO("Display device pixel ratio is: %.2f", state->device_pixel_ratio);

        //     return 0;
        case WM_SIZE: {
            // Get the updated size.
            RECT r;
            GetClientRect(hwnd, &r);
            pendingWidth = r.right - r.left;
            pendingHeight = r.bottom - r.top;

            {
                HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

                MONITORINFO monitor_info = {0};
                monitor_info.cbSize = sizeof(MONITORINFO);
                if (!GetMonitorInfoA(monitor, &monitor_info)) {
                    //KWARN("Failed to get monitor info. ");
                }

                //KINFO("monitor: %u", monitor_info.rcMonitor.left);
            }

            if(!resizing){
                // Fire the event. The application layer should pick this up, but not handle it
                // as it shouldn be visible to other parts of the application.
                EventContext context;
                context.data.u16[0] = (uint16)pendingWidth;
                context.data.u16[1] = (uint16)pendingWidth;
                eventFireOverwrite(EVENT(EVENT_CODE_RESIZED, 0, context));
            }
        } break;
        case WM_ENTERSIZEMOVE:
            resizing = true;
            break;
        case WM_EXITSIZEMOVE:
            resizing = false;
            EventContext context;
            context.data.u16[0] = (uint16)pendingWidth;
            context.data.u16[1] = (uint16)pendingWidth;
            eventFireOverwrite(EVENT(EVENT_CODE_RESIZED, 0, context));
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            // Key pressed/released
            bool8 pressed = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
            Keys key = (uint16)w_param;

            // Check for extended scan code.
            bool8 is_extended = (HIWORD(l_param) & KF_EXTENDED) == KF_EXTENDED;

            // Keypress only determines if _any_ alt/ctrl/shift key is pressed. Determine which one if so.
            if (w_param == VK_MENU) {
                key = is_extended ? KEY_RALT : KEY_LALT;
            } else if (w_param == VK_SHIFT) {
                // Annoyingly, KF_EXTENDED is not set for shift keys.
                uint32 left_shift = MapVirtualKey(VK_LSHIFT, MAPVK_VK_TO_VSC);
                uint32 scancode = ((l_param & (0xFF << 16)) >> 16);
                key = scancode == left_shift ? KEY_LSHIFT : KEY_RSHIFT;
            } else if (w_param == VK_CONTROL) {
                key = is_extended ? KEY_RCONTROL : KEY_LCONTROL;
            }

            // Pass to the input subsystem for processing.
            inputProcessKey(key, pressed);

            // Return 0 to prevent default window behaviour for some keypresses, such as alt.
            return 0;
        }
        case WM_MOUSEMOVE: {
            // Mouse move
            int32 x_position = GET_X_LPARAM(l_param);
            int32 y_position = GET_Y_LPARAM(l_param);

            // Pass over to the input subsystem.
            inputProcessMouseMove(x_position, y_position);
        } break;
        case WM_MOUSEWHEEL: {
            int32 z_delta = GET_WHEEL_DELTA_WPARAM(w_param);
            if (z_delta != 0) {
                // Flatten the input to an OS-independent (-1, 1)
                z_delta = (z_delta < 0) ? -1 : 1;
                inputProcessMouseWheel(z_delta);
            }
        } break;
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP: {
            bool8 pressed = msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN;
            Buttons mouse_button = BUTTON_MAX_BUTTONS;
            switch (msg) {
                case WM_LBUTTONDOWN:
                case WM_LBUTTONUP:
                    mouse_button = BUTTON_LEFT;
                    break;
                case WM_MBUTTONDOWN:
                case WM_MBUTTONUP:
                    mouse_button = BUTTON_MIDDLE;
                    break;
                case WM_RBUTTONDOWN:
                case WM_RBUTTONUP:
                    mouse_button = BUTTON_RIGHT;
                    break;
            }

            // Pass over to the input subsystem.
            if (mouse_button != BUTTON_MAX_BUTTONS) {
                inputProcessButton(mouse_button, pressed);
            }
        } break;
    }

    return DefWindowProcA(hwnd, msg, w_param, l_param);
}



#endif