#version 450 core

layout(std140, binding = 0) uniform uniform_buffer {
    uint TexIndex;
} u_UBO;

layout(location = 0) in vec3 v_Pos;
layout(location = 1) in vec2 v_TexCoord;

layout(location = 0) flat out uint f_TexIndex;
layout(location = 1) out vec2 f_TexCoord;

void main() 
{
    gl_Position = vec4(v_Pos, 1.0);
    f_TexIndex = u_UBO.TexIndex;
    f_TexCoord = v_TexCoord;
}
