#pragma once
#include "defines.h"

typedef struct Color {
    byte r;
    byte g;
    byte b;
    byte a;
} Color3b;
typedef struct Color4f {
    float r;
    float g;
    float b;
    float a;
}Color4f;
void rendererStartup(uint64* memoryRequirement, void* state, void* config);
void rendererShutdown(void* state);

void rendererBeginFrame(int width, int height);
void rendererEndFrame();


void rendererDrawRect(float x, float y, float w, float h, Color4f color);
void rendererDrawTriangle(float x1, float y1, float x2, float y2, float x3, float y3, Color4f color);
void rendererDrawLine(float x1, float y1, float x2, float y2, float thickness, Color4f color);
void rendererDrawPolyline(float* points, int count, float thickness, Color4f color);
void rendererDrawCircle(float cx, float cy, float radius, int segments, Color4f color);
void renderText(float x, float y, const char* str, Color4f color);