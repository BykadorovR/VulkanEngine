#version 450
#define epsilon 0.0001

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragTexCoord;
layout(location = 4) in mat3 fragTBN;
//mat3 takes 3 slots
layout(location = 7) in vec4 fragLightDirectionalCoord[2];

layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 1) uniform sampler2D texSampler;
layout(set = 0, binding = 2) uniform sampler2D normalSampler;
layout(set = 0, binding = 3) uniform sampler2D metallicSampler;
layout(set = 0, binding = 4) uniform sampler2D roughnessSampler;
layout(set = 0, binding = 5) uniform sampler2D occlusionSampler;
layout(set = 0, binding = 6) uniform sampler2D emissiveSampler;
layout(set = 0, binding = 7) uniform samplerCube irradianceSampler;
layout(set = 0, binding = 8) uniform samplerCube specularIBLSampler;
layout(set = 0, binding = 9) uniform sampler2D specularBRDFSampler;

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

layout(std140, set = 2, binding = 1) readonly buffer LightBufferDirectional {
    int lightDirectionalNumber;
    LightDirectional lightDirectional[];
};

layout(std140, set = 2, binding = 2) readonly buffer LightBufferPoint {
    int lightPointNumber;
    LightPoint lightPoint[];
};

layout(set = 0, binding = 10) uniform AlphaMask {
    bool alphaMask;
    float alphaMaskCutoff;
} alphaMask;


layout(set = 2, binding = 3) uniform sampler2D shadowDirectionalSampler[2];
layout(set = 2, binding = 4) uniform samplerCube shadowPointSampler[4];
layout(set = 2, binding = 5) uniform ShadowParameters {
    int enabledDirectional[2];
    int enabledPoint[4];
    //0 - simple, 1 - vsm
    int algorithmDirectional;
    int algorithmPoint;
} shadowParameters;

//coefficients from base color
layout(set = 0, binding = 11) uniform Material {
    float metallicFactor;
    float roughnessFactor;
    // occludedColor = mix(color, color * <sampled occlusion texture value>, <occlusion strength>)
    float occlusionStrength;
    vec3 emissiveFactor;
} material;

layout( push_constant ) uniform constants {
    int enableShadow;
    int enableLighting;
    vec3 cameraPosition;
} push;

#define getLightDir(index) lightDirectional[index]
#define getLightPoint(index) lightPoint[index]
#define getIrradianceSampler() irradianceSampler
#define getSpecularIBLSampler() specularIBLSampler
#define getSpecularBRDFSampler() specularBRDFSampler
#define getMaterial() material
#define getShadowParameters() shadowParameters
#include "../shadow.glsl"
#include "../pbr.glsl"

//TODO: add support for shadows, right now there is no PBR objects on which shadows should be casted that's why everything is fine
void main() {
    vec4 albedoTexture = texture(texSampler, fragTexCoord) * vec4(fragColor, 1.0);
    vec4 normalTexture = texture(normalSampler, fragTexCoord);
    vec4 metallicTexture = texture(metallicSampler, fragTexCoord);
    vec4 roughnessTexture = texture(roughnessSampler, fragTexCoord);
    vec4 occlusionTexture = texture(occlusionSampler, fragTexCoord);
    vec4 emissiveTexture = texture(emissiveSampler, fragTexCoord);
    // ao .r, roughness .g, metallic .b
    float metallicValue = metallicTexture.b * material.metallicFactor;
    float roughnessValue = roughnessTexture.g * material.roughnessFactor;

    outColor = albedoTexture;
    if (alphaMask.alphaMask) {
        if (outColor.a < alphaMask.alphaMaskCutoff) {
            discard;
        }
    }

    if (push.enableLighting > 0) {
        vec3 normal = normalTexture.rgb;
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
                vec3 directional = calculateOutRadiance(lightDir, normal, viewDir, inRadiance, metallicValue, roughnessValue, albedoTexture.rgb);
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
                vec3 point = calculateOutRadiance(lightDir, normal, viewDir, inRadiance, metallicValue, roughnessValue, albedoTexture.rgb);
                float shadow = 0.0;
                if (push.enableShadow > 0 && getShadowParameters().enabledPoint[i] > 0)
                    shadow = calculateTextureShadowPoint(shadowPointSampler[i], fragPosition, getLightPoint(i).position, getLightPoint(i).far, 0.15);
                Lr += point * (1 - shadow);
            }

            outColor.rgb = Lr;
            //add occlusion to resulting color (it doesn't depend on light sources at all), occlusion is stored inside metallic roughness as .r channel or as separate texture .r channel
            //so it doesn't matter, any texture -> .r channel

            //IBL
            outColor.rgb += calculateIBL(occlusionTexture.r, normal, viewDir, metallicValue, roughnessValue, albedoTexture.rgb);

            //add emissive to resulting reflected radiance from all light sources
            outColor.rgb += emissiveTexture.rgb * material.emissiveFactor;
        }
    }
}