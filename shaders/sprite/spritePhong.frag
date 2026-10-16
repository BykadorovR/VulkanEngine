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
layout(set = 0, binding = 3) uniform sampler2D specularSampler;

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


//coefficients from base color
layout(set = 0, binding = 4) uniform Material {
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    float shininess;
} material;

layout( push_constant ) uniform constants {
    int enableShadow;
    int enableLighting;
    vec3 cameraPosition;
} push;

#define getLightDir(index) lightDirectional[index]
#define getLightPoint(index) lightPoint[index]
#define getLightAmbient(index) lightAmbient[index]
#define getMaterial() material
#define getShadowParameters() shadowParameters
#include "../shadow.glsl"
#include "../phong.glsl"

void main() {
    //base color
    outColor = texture(texSampler, fragTexCoord) * vec4(fragColor, 1.0);
    float specularTexture = texture(specularSampler, fragTexCoord).b;
    if (push.enableLighting > 0) {
        vec3 normal = texture(normalSampler, fragTexCoord).rgb;
        if (length(normal) > epsilon) {
            normal = normal * 2.0 - 1.0;
            normal = normalize(fragTBN * normal);
        } else {
            normal = fragNormal;
        }

        if (length(normal) > epsilon) {
            vec3 lightFactor = vec3(0.0, 0.0, 0.0);
            //calculate directional light
            lightFactor += directionalLight(lightDirectionalNumber, fragPosition, normal, specularTexture, push.cameraPosition, 
                                            push.enableShadow, fragLightDirectionalCoord, shadowDirectionalSampler, 0.05);
            //calculate point light
            lightFactor += pointLight(lightPointNumber, fragPosition, normal, specularTexture, 
                                      push.cameraPosition, push.enableShadow, shadowPointSampler, 0.15);
            //calculate ambient light
            for (int i = 0;i < lightAmbientNumber; i++) {
                lightFactor += lightAmbient[i].color;
            }

            outColor *= vec4(lightFactor, 1.0);
        }
    }    
}