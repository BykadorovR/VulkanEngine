#version 450

layout(set = 0, binding = 0) uniform UniformCamera {
    mat4 model;
    mat4 view;
    mat4 proj;
} mvp;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inJointIndices;
layout(location = 5) in vec4 inJointWeights;
layout(location = 6) in vec4 inTangent;

layout(location = 0) out vec3 TexCoords;

void main() {
    TexCoords = inPosition;
    vec4 position = mvp.proj * mvp.view * mvp.model * vec4(inPosition, 1.0);
    // little trick to force skybox to have z = 1 so it's drawn on the background
    //gl_Position = position.xyww;
    gl_Position = position;
}  