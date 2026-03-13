#version 450 core

layout(binding = 1) uniform sampler2D u_TexSampler[2];

layout(location = 0) flat in uint f_TexIndex;
layout(location = 1) in vec2 f_TexCoord;

layout(location = 0) out vec4 f_OutColor;

void main()
{
    vec4 Color = texture(u_TexSampler[f_TexIndex], f_TexCoord);
    f_OutColor = Color;
}
