#version 450 core

layout(location = 0) in vec4 Frag_Color;

layout(location = 0) out vec4 FragOut_Color;

void main() 
{
    FragOut_Color = Frag_Color;
}
