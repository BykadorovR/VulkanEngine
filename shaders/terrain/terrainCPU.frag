#version 450

layout(location = 0) in float fragHeight;
layout(location = 0) out vec4 outColor;

void main() {
    float color = (fragHeight + 16) / 64.0;
    outColor = vec4(color, color, color, 1.0);
}