#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec3 texCoords;
layout(set = 0, binding = 1) uniform samplerCube cubeSampler;

layout(location = 0) out vec4 outColor;

void main() {    
    outColor = texture(cubeSampler, texCoords) * vec4(fragColor, 1.0);
}