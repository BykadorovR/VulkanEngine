#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 inTilesWeights;
layout(location = 3) flat in int inRotation;
layout(location = 4) in vec3 fragPosition;
layout(location = 5) in mat3 fragTBN;
//mat3 takes 3 slots
layout(location = 8) in vec4 fragLightDirectionalCoord[2];

layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 4) uniform sampler2D texSampler[4];

layout(push_constant) uniform constants {
    layout(offset = 40) int enableShadow;
    int enableLighting;
    vec3 cameraPosition;
} push;

struct LightDirectional {
    //
    vec3 color; //radiance
    vec3 position;
};

struct LightPoint {
    //attenuation
    float quadratic;
    int distance;
    //parameters
    float far;
    //
    vec3 color; //radiance
    vec3 position;
};

struct LightAmbient {
    vec3 color; //radiance
};

layout(std140, set = 1, binding = 1) readonly buffer LightBufferDirectional {
    int lightDirectionalNumber;
    LightDirectional lightDirectional[];
};

layout(std140, set = 1, binding = 2) readonly buffer LightBufferPoint {
    int lightPointNumber;
    LightPoint lightPoint[];
};

layout(std140, set = 1, binding = 3) readonly buffer LightBufferAmbient {
    int lightAmbientNumber;
    LightAmbient lightAmbient[];
};

layout(set = 1, binding = 4) uniform sampler2D shadowDirectionalSampler[2];
layout(set = 1, binding = 5) uniform samplerCube shadowPointSampler[4];
layout(set = 1, binding = 6) uniform ShadowParameters {
    int enabledDirectional[2];
    int enabledPoint[4];
    //0 - simple, 1 - vsm
    int algorithmDirectional;
    int algorithmPoint;
} shadowParameters;

mat2 rotate(float a) {
    float s = sin(radians(a));
    float c = cos(radians(a));
    mat2 m = mat2(c, s, -s, c);
    return m;
}

vec4 blendTwoColors(vec4 colorA, vec4 colorB) {
    float alphaA = colorA.a;
    float alphaB = colorB.a;

    float alphaOut = alphaA + alphaB * (1.0 - alphaA);
    if (alphaOut < 0.001) {
        return vec4(0.0);
    }
    vec3 colorOut = (colorA.rgb * alphaA + colorB.rgb * alphaB * (1.0 - alphaA)) / alphaOut;
    return vec4(colorOut, alphaOut);
}

vec4 blendFourColors(vec4 color1, vec4 color2, vec4 color3, vec4 color4) {
    vec4 result12 = blendTwoColors(color1, color2);
    vec4 result123 = blendTwoColors(result12, color3);
    return blendTwoColors(result123, color4);
}

#define getLightDir(index) lightDirectional[index]
#define getLightPoint(index) lightPoint[index]
#define getLightAmbient(index) lightAmbient[index]
#define getMaterial() material
#define getShadowParameters() shadowParameters
#include "../../shadow.glsl"

void main() {
    vec2 texCoord = rotate(inRotation) * fragTexCoord;
    vec4 texture0 = vec4(texture(texSampler[0], texCoord).rgb, inTilesWeights[0]);
    vec4 texture1 = vec4(texture(texSampler[1], texCoord).rgb, inTilesWeights[1]);
    vec4 texture2 = vec4(texture(texSampler[2], texCoord).rgb, inTilesWeights[2]);
    vec4 texture3 = vec4(texture(texSampler[3], texCoord).rgb, inTilesWeights[3]);

    outColor = blendFourColors(texture0, texture1, texture2, texture3);
    outColor.a = 1;

    vec3 normal = fragNormal;
    vec3 lightFactor = vec3(0.0, 0.0, 0.0);
    for (int i = 0; i < lightDirectionalNumber; i++) {
        float bias = 0.005;
        vec3 lightDir = normalize(getLightDir(i).position - fragPosition);
        float shadow = 0.0;
        if (push.enableShadow > 0 && getShadowParameters().enabledDirectional[i] > 0)
            shadow = calculateTextureShadowDirectional(shadowDirectionalSampler[i], fragLightDirectionalCoord[i], normal, lightDir, bias); 
        lightFactor += getLightDir(i).color * (1 - shadow);
    }
    for (int i = 0; i < lightPointNumber; i++) {
        float bias = 0.05;
        float distance = length(getLightPoint(i).position - fragPosition);
        if (distance > getLightPoint(i).distance) break;
        vec3 lightDir = normalize(getLightPoint(i).position - fragPosition);
        float shadow = 0.0;
        if (push.enableShadow > 0 && getShadowParameters().enabledPoint[i] > 0)
            shadow = calculateTextureShadowPoint(shadowPointSampler[i], fragPosition, getLightPoint(i).position, getLightPoint(i).far, bias); 
        float attenuation = 1.0 / (getLightPoint(i).quadratic * distance * distance);
        lightFactor += getLightPoint(i).color * attenuation * (1 - shadow);
    }

    outColor *= vec4(lightFactor, 1.0);
}