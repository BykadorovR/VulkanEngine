#version 450

layout(set = 0, binding = 0) uniform UniformData {
	vec2 scale;
	vec2 translate;
} data;

layout (location = 0) in vec2 inPos;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec4 inColor;

layout (location = 0) out vec2 outUV;
layout (location = 1) out vec4 outColor;

void main() 
{
	outUV = inUV;
	outColor = inColor;
	gl_Position = vec4(inPos * data.scale + data.translate, 0.0, 1.0);
}