#version 450
#define epsilon 0.0001

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
layout(set = 0, binding = 5) uniform sampler2D normalSampler[4];
layout(set = 0, binding = 6) uniform sampler2D metallicSampler[4];
layout(set = 0, binding = 7) uniform sampler2D roughnessSampler[4];
layout(set = 0, binding = 8) uniform sampler2D occlusionSampler[4];
layout(set = 0, binding = 9) uniform sampler2D emissiveSampler[4];
layout(set = 0, binding = 10) uniform samplerCube irradianceSampler;
layout(set = 0, binding = 11) uniform samplerCube specularIBLSampler;
layout(set = 0, binding = 12) uniform sampler2D specularBRDFSampler;
layout(set = 0, binding = 13) uniform Material {
    float metallicFactor;
    float roughnessFactor;
    // occludedColor = mix(color, color * <sampled occlusion texture value>, <occlusion strength>)
    float occlusionStrength;
    vec3 emissiveFactor;
} material;

layout(set = 0, binding = 14) uniform AlphaMask {
    bool alphaMask;
    float alphaMaskCutoff;
} alphaMask;


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

layout(std140, set = 1, binding = 1) readonly buffer LightBufferDirectional {
    int lightDirectionalNumber;
    LightDirectional lightDirectional[];
};

layout(std140, set = 1, binding = 2) readonly buffer LightBufferPoint {
    int lightPointNumber;
    LightPoint lightPoint[];
};

layout(set = 1, binding = 3) uniform sampler2D shadowDirectionalSampler[2];
layout(set = 1, binding = 4) uniform samplerCube shadowPointSampler[4];
layout(set = 1, binding = 5) uniform ShadowParameters {
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
#define getIrradianceSampler() irradianceSampler
#define getSpecularIBLSampler() specularIBLSampler
#define getSpecularBRDFSampler() specularBRDFSampler
#define getMaterial() material
#define getShadowParameters() shadowParameters
#include "../../shadow.glsl"
#include "../../pbr.glsl"

void main() {
    vec2 texCoord = rotate(inRotation) * fragTexCoord;
    vec4 texture0 = vec4(texture(texSampler[0], texCoord).rgb, inTilesWeights[0]);
    vec4 texture1 = vec4(texture(texSampler[1], texCoord).rgb, inTilesWeights[1]);
    vec4 texture2 = vec4(texture(texSampler[2], texCoord).rgb, inTilesWeights[2]);
    vec4 texture3 = vec4(texture(texSampler[3], texCoord).rgb, inTilesWeights[3]);

    vec4 albedoColor = blendFourColors(texture0, texture1, texture2, texture3);
    albedoColor.a = 1;

    vec4 normalColor0 = vec4(texture(normalSampler[0], texCoord).rgb, inTilesWeights[0]);
    vec4 normalColor1 = vec4(texture(normalSampler[1], texCoord).rgb, inTilesWeights[1]);
    vec4 normalColor2 = vec4(texture(normalSampler[2], texCoord).rgb, inTilesWeights[2]);
    vec4 normalColor3 = vec4(texture(normalSampler[3], texCoord).rgb, inTilesWeights[3]);

    vec3 normalColor = blendFourColors(normalColor0, normalColor1, normalColor2, normalColor3).rgb;

    vec4 metallicColor0 = vec4(texture(metallicSampler[0], texCoord).b, 0, 0, inTilesWeights[0]);
    vec4 metallicColor1 = vec4(texture(metallicSampler[1], texCoord).b, 0, 0, inTilesWeights[1]);
    vec4 metallicColor2 = vec4(texture(metallicSampler[2], texCoord).b, 0, 0, inTilesWeights[2]);
    vec4 metallicColor3 = vec4(texture(metallicSampler[3], texCoord).b, 0, 0, inTilesWeights[3]);

    float metallicColor = blendFourColors(metallicColor0, metallicColor1, metallicColor2, metallicColor3).r;

    vec4 roughnessColor0 = vec4(texture(roughnessSampler[0], texCoord).g, 0, 0, inTilesWeights[0]);
    vec4 roughnessColor1 = vec4(texture(roughnessSampler[1], texCoord).g, 0, 0, inTilesWeights[1]);
    vec4 roughnessColor2 = vec4(texture(roughnessSampler[2], texCoord).g, 0, 0, inTilesWeights[2]);
    vec4 roughnessColor3 = vec4(texture(roughnessSampler[3], texCoord).g, 0, 0, inTilesWeights[3]);

    float roughnessColor = blendFourColors(roughnessColor0, roughnessColor1, roughnessColor2, roughnessColor3).r;

    vec4 occlusionColor0 = vec4(texture(occlusionSampler[0], texCoord).r, 0, 0, inTilesWeights[0]);
    vec4 occlusionColor1 = vec4(texture(occlusionSampler[1], texCoord).r, 0, 0, inTilesWeights[1]);
    vec4 occlusionColor2 = vec4(texture(occlusionSampler[2], texCoord).r, 0, 0, inTilesWeights[2]);
    vec4 occlusionColor3 = vec4(texture(occlusionSampler[3], texCoord).r, 0, 0, inTilesWeights[3]);

    float occlusionColor = blendFourColors(occlusionColor0, occlusionColor1, occlusionColor2, occlusionColor3).r;

    vec4 emissiveColor0 = vec4(texture(emissiveSampler[0], texCoord).rgb, inTilesWeights[0]);
    vec4 emissiveColor1 = vec4(texture(emissiveSampler[1], texCoord).rgb, inTilesWeights[1]);
    vec4 emissiveColor2 = vec4(texture(emissiveSampler[2], texCoord).rgb, inTilesWeights[2]);
    vec4 emissiveColor3 = vec4(texture(emissiveSampler[3], texCoord).rgb, inTilesWeights[3]);

    vec3 emissiveColor = blendFourColors(emissiveColor0, emissiveColor1, emissiveColor2, emissiveColor3).rgb;

    outColor = albedoColor;

    float metallicValue = metallicColor * material.metallicFactor;
    float roughnessValue = roughnessColor * material.roughnessFactor;

    if (alphaMask.alphaMask) {
        if (outColor.a < alphaMask.alphaMaskCutoff) {
            discard;
        }
    }

    if (push.enableLighting > 0) {
        vec3 normal = normalColor;
        if (length(normal) > epsilon) {
            normal = normal * 2.0 - 1.0;
            normal = normalize(fragTBN * normal);
        } else {
            normal = fragNormal;
        }

        if (length(normal) > epsilon) {
            //calculate reflected part for every light source separately and them sum them            
            vec3 viewDir = normalize(push.cameraPosition - fragPosition);

            // reflectance equation
            vec3 Lr = vec3(0.0);
            for (int i = 0; i < lightDirectionalNumber; i++) {
                vec3 lightDir = normalize(getLightDir(i).position - fragPosition);
                vec3 inRadiance = getLightDir(i).color;
                vec3 directional = calculateOutRadiance(lightDir, normal, viewDir, inRadiance, metallicValue, roughnessValue, albedoColor.rgb);
                float shadow = 0.0;
                if (push.enableShadow > 0 && getShadowParameters().enabledDirectional[i] > 0)
                    shadow = calculateTextureShadowDirectional(shadowDirectionalSampler[i], fragLightDirectionalCoord[i], normal, lightDir, 0.05);
                Lr += directional * (1 - shadow);
            }

            for (int i = 0; i < lightPointNumber; i++) {
                vec3 lightDir = normalize(getLightPoint(i).position - fragPosition);
                float distance = length(getLightPoint(i).position - fragPosition);
                if (distance > getLightPoint(i).distance) break;
                float attenuation = 1.0 / (getLightPoint(i).quadratic * distance * distance);
                vec3 inRadiance = getLightPoint(i).color * attenuation;
                vec3 point = calculateOutRadiance(lightDir, normal, viewDir, inRadiance, metallicValue, roughnessValue, albedoColor.rgb);
                float shadow = 0.0;
                if (push.enableShadow > 0 && getShadowParameters().enabledPoint[i] > 0)
                    shadow = calculateTextureShadowPoint(shadowPointSampler[i], fragPosition, getLightPoint(i).position, getLightPoint(i).far, 0.15);
                Lr += point * (1 - shadow);
            }

            outColor.rgb = Lr;
            //add occlusion to resulting color (it doesn't depend on light sources at all), occlusion is stored inside metallic roughness as .r channel or as separate texture .r channel
            //so it doesn't matter, any texture -> .r channel

            //IBL
            outColor.rgb += calculateIBL(occlusionColor.r, normal, viewDir, metallicValue, roughnessValue, albedoColor.rgb);

            //add emissive to resulting reflected radiance from all light sources
            outColor.rgb += emissiveColor * material.emissiveFactor;
        }
    }
}