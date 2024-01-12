#version 450
#define epsilon 0.0001

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec3 fragTexCoord;
layout(location = 4) in mat3 fragTBN;
//mat3 takes 3 slots
layout(location = 7) in vec4 fragLightDirectionalCoord[2];

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outColorBloom;
layout(set = 1, binding = 0) uniform samplerCube texSampler;
layout(set = 1, binding = 1) uniform samplerCube normalSampler;
layout(set = 1, binding = 2) uniform samplerCube specularSampler;

void main() {
    vec3 normal = texture(normalSampler, fragTexCoord).rgb;
    normal = normal * 2.0 - 1.0;
    normal = normalize(fragTBN * normal);

    outColor = vec4(normal, 1.0);

    // check whether fragment output is higher than threshold, if so output as brightness color
    float brightness = dot(outColor.rgb, vec3(0.2126, 0.7152, 0.0722));
    if(brightness > 1.0)
        outColorBloom = vec4(outColor.rgb, 1.0);
    else
        outColorBloom = vec4(0.0, 0.0, 0.0, 1.0);
}