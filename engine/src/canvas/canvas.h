#pragma once
#include "defines.h"
#include "utils/matrix.h"

typedef struct Canvas{
    float x, y;
    float width, height;
    Mat3x3 transform;
    Mat3x3 projection;
} Canvas;


bool8 canvasSetView(Canvas* canvas, int32 width, int32 height);

bool8 canvasBegin(Canvas* canvas);
