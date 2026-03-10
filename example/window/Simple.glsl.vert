#version 450 core

layout(location = 0) in vec4 Vert_Color;
layout(location = 1) in vec3 Vert_Pos;

layout(location = 0) out vec4 Frag_Color;

void main() 
{
    gl_Position = vec4(Vert_Pos, 1.0);
    Frag_Color = Vert_Color;
}
