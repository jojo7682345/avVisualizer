#pragma once
#include "vector.h"

typedef struct mat4f {
    union{
        float m[4][4];
    };
} mat4;

typedef struct mat3f {
    union{
        float m[3][3];
    };
} mat3;

typedef struct mat2f {
    union{
        float m[2][2];
    };
} mat2;

#define mat4Identity (mat4) {.m={{1.0f, 0.0f, 0.0f, 0.0f},{0.0f, 1.0f, 0.0f, 0.0f},{0.0f, 0.0f, 1.0f, 0.0f},{0.0f, 0.0f, 0.0f, 1.0f}}}
#define mat3Identity (mat3) {.m={{1.0f, 0.0f, 0.0f},{0.0f, 1.0f, 0.0f},{0.0f, 0.0f, 1.0f}}}
#define mat2Identity (mat2) {.m={{1.0f, 0.0f},{0.0f, 1.0f}}}

vec3 vec3Add(vec3 a, vec3 b);
vec3 vec3Sub(vec3 a, vec3 b);
vec3 vec3Scale(vec3 v, float s);

float vec3Dot(vec3 a, vec3 b);
vec3 vec3Cross(vec3 a, vec3 b);
vec3 vec3Normalize(vec3 v);

mat4 mat4Mul(mat4 a, mat4 b);

mat4 mat4Translate(vec3 t);
mat4 mat4Scale(vec3 s);
mat4 mat4Rotate(float angle, vec3 axis);

mat4 lookAt(vec3 eye, vec3 center, vec3 up);
mat4 perspective(float fov, float aspect, float zNear, float zFar);