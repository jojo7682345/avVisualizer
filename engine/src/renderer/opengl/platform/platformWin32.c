#include "renderer/renderer.h"
#include "rendererPlatform.h"
#include "windows.h"


typedef struct {
    HWND hwnd;
    HDC hdc;
} PlatformWindow;

PlatformWindow* state;

bool8 platformRendererStartup(uint64* memoryRequirement, void* statePtr, void* platformState){
    (*memoryRequirement) = sizeof(PlatformWindow);
    if(statePtr==0){
        return true;
    }
    void platformWin32GetHWND(void* platformState, HWND* hwnd);
    platformWin32GetHWND(platformState, &((PlatformWindow*)statePtr)->hwnd);
    ((PlatformWindow*)statePtr)->hdc = GetDC(((PlatformWindow*)statePtr)->hwnd);
    state = statePtr;
}

void platformChoosePixelFormat(){

    HDC hdc = state->hdc;

    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 24;

    int pf = ChoosePixelFormat(hdc, &pfd);
    SetPixelFormat(hdc, pf, &pfd);
}

void platformCreateGlContext(){
    HGLRC glrc = wglCreateContext(state->hdc);
    wglMakeCurrent(state->hdc, glrc);
}

void platformSwapBuffers(){
    SwapBuffers(state->hdc);
}