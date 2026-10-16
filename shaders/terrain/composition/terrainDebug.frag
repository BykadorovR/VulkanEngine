#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 tessColor;
layout(location = 2) in vec4 inTilesWeights;
layout(location = 3) flat in int inRotation;

layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 4) uniform sampler2D texSampler[4];

layout(push_constant) uniform constants {    
    layout(offset = 32) int patchEdge;
    int tessColorFlag;
    int enableShadow;
    int enableLighting;
    int pickedPatch;
} push;

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

void main() {
    vec2 texCoord = rotate(inRotation) * fragTexCoord;
    if (push.tessColorFlag > 0) {
        outColor = vec4(tessColor, 1);
    } else {
        vec4 texture0 = vec4(texture(texSampler[0], texCoord).rgb, inTilesWeights[0]);
        vec4 texture1 = vec4(texture(texSampler[1], texCoord).rgb, inTilesWeights[1]);
        vec4 texture2 = vec4(texture(texSampler[2], texCoord).rgb, inTilesWeights[2]);
        vec4 texture3 = vec4(texture(texSampler[3], texCoord).rgb, inTilesWeights[3]);

        outColor = blendFourColors(texture0, texture1, texture2, texture3);
        outColor.a = 1;
    }

    vec2 line = fract(texCoord);
    if (push.patchEdge > 0 && (line.x < 0.01 || line.y < 0.01 || line.x > 0.99 || line.y > 0.99)) {
        outColor = vec4(1, 1, 0, 1);
        // in fragment shader gl_PrimitiveID behaves the same way as in tesselation shaders IF there is no geometry shader
        if (push.pickedPatch == gl_PrimitiveID) {
            outColor = vec4(1, 0, 0, 1);
        }
    }
}