#version 330 core

layout (location = 0) in vec2 in_pos;
layout (location = 1) in vec4 in_color;

uniform mat4 u_projection;

out vec4 frag_color;

void main()
{
    frag_color = in_color;
    gl_Position = u_projection * vec4(in_pos, 0.0, 1.0);
}