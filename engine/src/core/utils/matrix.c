#include "matrix.h"
#include <math.h>

vec3 vec3Add(vec3 a, vec3 b) {
    return (vec3){a.x+b.x, a.y+b.y, a.z+b.z};
}

vec3 vec3Sub(vec3 a, vec3 b) {
    return (vec3){a.x-b.x, a.y-b.y, a.z-b.z};
}

vec3 vec3Scale(vec3 v, float s) {
    return (vec3){v.x*s, v.y*s, v.z*s};
}

float vec3Dot(vec3 a, vec3 b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

vec3 vec3Cross(vec3 a, vec3 b) {
    return (vec3){
        a.y*b.z - a.z*b.y,
        a.z*b.x - a.x*b.z,
        a.x*b.y - a.y*b.x
    };
}

vec3 vec3Normalize(vec3 v) {
    float len = sqrtf(vec3Dot(v,v));
    return (vec3){v.x/len, v.y/len, v.z/len};
}

mat4 mat4Mul(mat4 a, mat4 b) {
    mat4 r = {0};

    for(int i=0;i<4;i++)
    for(int j=0;j<4;j++)
    for(int k=0;k<4;k++)
        r.m[i][j] += a.m[i][k]*b.m[k][j];

    return r;
}

mat4 mat4Translate(vec3 t) {
    mat4 r = mat4Identity;

    r.m[3][0] = t.x;
    r.m[3][1] = t.y;
    r.m[3][2] = t.z;

    return r;
}

mat4 mat4Scale(vec3 s) {
    mat4 r = mat4Identity;

    r.m[0][0] = s.x;
    r.m[1][1] = s.y;
    r.m[2][2] = s.z;

    return r;
}

mat4 mat4Rotate(float angle, vec3 axis) {
    axis = vec3Normalize(axis);

    float c = cosf(angle);
    float s = sinf(angle);
    float t = 1.0f - c;

    mat4 r = mat4Identity;

    r.m[0][0] = c + axis.x*axis.x*t;
    r.m[0][1] = axis.x*axis.y*t - axis.z*s;
    r.m[0][2] = axis.x*axis.z*t + axis.y*s;

    r.m[1][0] = axis.y*axis.x*t + axis.z*s;
    r.m[1][1] = c + axis.y*axis.y*t;
    r.m[1][2] = axis.y*axis.z*t - axis.x*s;

    r.m[2][0] = axis.z*axis.x*t - axis.y*s;
    r.m[2][1] = axis.z*axis.y*t + axis.x*s;
    r.m[2][2] = c + axis.z*axis.z*t;

    return r;
}

mat4 lookAt(vec3 eye, vec3 center, vec3 up) {
    vec3 f = vec3Normalize(vec3Sub(center,eye));
    vec3 s = vec3Normalize(vec3Cross(f,up));
    vec3 u = vec3Cross(s,f);

    mat4 r = mat4Identity;

    r.m[0][0] = s.x;
    r.m[1][0] = s.y;
    r.m[2][0] = s.z;

    r.m[0][1] = u.x;
    r.m[1][1] = u.y;
    r.m[2][1] = u.z;

    r.m[0][2] = -f.x;
    r.m[1][2] = -f.y;
    r.m[2][2] = -f.z;

    r.m[3][0] = -vec3Dot(s,eye);
    r.m[3][1] = -vec3Dot(u,eye);
    r.m[3][2] =  vec3Dot(f,eye);

    return r;
}

mat4 perspective(float fov, float aspect, float zNear, float zFar) {
    float tanHalf = tanf(fov*0.5f);

    mat4 r = {0};

    r.m[0][0] = 1.0f/(aspect*tanHalf);
    r.m[1][1] = -1.0f/tanHalf;

    r.m[2][2] = zFar/(zNear - zFar);
    r.m[2][3] = -1.0f;

    r.m[3][2] = (zNear*zFar)/(zNear - zFar);

    return r;
}