#pragma once

typedef struct vec4f {
    union{
        float r;
        float x;
    };
    union {
        float g;
        float y;
    };
    union {
        float b;
        float z;
    };
    union {
        float a;
        float w;
    };
} vec4;

typedef struct vec3f {
    union{
        float r;
        float x;
        float u;
        float i;
    };
    union {
        float g;
        float y;
        float v;
        float j;
    };
    union {
        float b;
        float z;
        float w;
        float k;
    };
} vec3;

typedef struct vec2f {
    union{
        float x;
        float u;
    };
    union {
        float y;
        float v;
    };
} vec2;

#define vec2(x,y) (vec2){x,y}
#define vec3(x,y,z) (vec3){x,y,z}
#define vec4(x,y,z,w) (vec4){x,y,z, w}

