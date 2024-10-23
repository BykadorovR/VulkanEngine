// tessellation evaluation shader
#version 450 core

layout (quads, fractional_odd_spacing, ccw) in;

// received from Tessellation Control Shader - all texture coordinates for the patch vertices
layout (location = 0) in vec2 TextureCoord[];
layout (location = 1) in vec3 tessColor[];

layout(set = 0, binding = 1) uniform UniformCamera {
    mat4 model;
    mat4 view;
    mat4 proj;
} mvp;

layout(set = 0, binding = 2) uniform sampler2D heightMap;

// send to Fragment Shader for coloring
layout (location = 0) out float Height;
layout (location = 1) out vec2 TexCoord;
layout (location = 2) out vec3 outTessColor;
layout (location = 3) flat out int outNeighborID[3][3];

struct PatchDescription {
    mat4 rotation;
    int textureID;
};

layout(std140, set = 0, binding = 4) readonly buffer PatchDescriptionBuffer {
    PatchDescription patchDescription[];
};

layout( push_constant ) uniform constants {
    layout(offset = 16) int patchDimX;
    int patchDimY;
    float heightScale;
    float heightShift;
} push;


float calculateHeightTexture(in vec2 TexCoord) {
    vec2 texCoord = TexCoord / vec2(push.patchDimX, push.patchDimY);
    return texture(heightMap, texCoord).x;
}

float calculateHeightPosition(in vec2 p) {
    vec2 textureSize = textureSize(heightMap, 0);
    vec2 pos = p + (textureSize - vec2(1.0)) / vec2(2.0);
    ivec2 integral = ivec2(floor(pos));
    vec2 texCoord = fract(pos);
    ivec2 index0 = integral;
    ivec2 index1 = integral + ivec2(1, 0);
    ivec2 index2 = integral + ivec2(0, 1);
    ivec2 index3 = integral + ivec2(1, 1);
    float sample0 = texelFetch(heightMap, index0, 0).x;
    float sample1 = texelFetch(heightMap, index1, 0).x;
    float sample2 = texelFetch(heightMap, index2, 0).x;
    float sample3 = texelFetch(heightMap, index3, 0).x;
    float fxy1 = sample0 + texCoord.x * (sample1 - sample0);
    float fxy2 = sample2 + texCoord.x * (sample3 - sample2);
    float heightValue = fxy1 + texCoord.y * (fxy2 - fxy1);
    return heightValue;
}

int getPatchID(int x, int y) {
    int newID = gl_PrimitiveID + x + y * push.patchDimX;
    newID = min(newID, push.patchDimX * push.patchDimY - 1);
    newID = max(newID, 0);
    return newID;
}

void main() {
    // get vertex coordinate (2D)
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;

    // debug color
    vec3 tc00 = tessColor[0];
    vec3 tc01 = tessColor[1];
    vec3 tc10 = tessColor[2];
    vec3 tc11 = tessColor[3];
    // bilinearly interpolate texture coordinate across patch
    vec3 tc0 = (tc01 - tc00) * u + tc00;
    vec3 tc1 = (tc11 - tc10) * u + tc10;
    outTessColor = (tc1 - tc0) * v + tc0;

    // ----------------------------------------------------------------------
    // retrieve control point texture coordinates
    vec2 t00 = TextureCoord[0];
    vec2 t01 = TextureCoord[1];
    vec2 t10 = TextureCoord[2];
    vec2 t11 = TextureCoord[3];

    // bilinearly interpolate texture coordinate across patch
    vec2 t0 = (t01 - t00) * u + t00;
    vec2 t1 = (t11 - t10) * u + t10;
    TexCoord = (t1 - t0) * v + t0;    

    // IMPORTANT: need to divide, otherwise we will have the whole heightmap for every tile
    // lookup texel at patch coordinate for height and scale + shift as desired

    // ----------------------------------------------------------------------
    // retrieve control point position coordinates
    vec4 p00 = gl_in[0].gl_Position;
    vec4 p01 = gl_in[1].gl_Position;
    vec4 p10 = gl_in[2].gl_Position;
    vec4 p11 = gl_in[3].gl_Position;

    // compute patch surface normal
    vec4 uVec = p01 - p00;
    vec4 vVec = p10 - p00;
    vec4 normal = normalize( vec4(cross(vVec.xyz, uVec.xyz), 0) );

    // bilinearly interpolate position coordinate across patch
    vec4 p0 = (p01 - p00) * u + p00;
    vec4 p1 = (p11 - p10) * u + p10;
    vec4 p = (p1 - p0) * v + p0;

    float heightValue = calculateHeightTexture(TexCoord);
    // calculate the same way as in C++, but result is the same as in the line above
    //float heightValue = calculateHeightPosition(p.xz);

    outNeighborID[0][0] = patchDescription[getPatchID(-1, -1)].textureID;
    outNeighborID[1][0] = patchDescription[getPatchID(0, -1)].textureID;
    outNeighborID[2][0] = patchDescription[getPatchID(1, -1)].textureID;
    outNeighborID[0][1] = patchDescription[getPatchID(-1, 0)].textureID;
    outNeighborID[1][1] = patchDescription[getPatchID(0, 0)].textureID;
    outNeighborID[2][1] = patchDescription[getPatchID(1, 0)].textureID;
    outNeighborID[0][2] = patchDescription[getPatchID(-1, 1)].textureID;
    outNeighborID[1][2] = patchDescription[getPatchID(0, 1)].textureID;
    outNeighborID[2][2] = patchDescription[getPatchID(1, 1)].textureID;

    mat4 rotationMatTmp = patchDescription[gl_PrimitiveID].rotation;
    mat2 rotationMat = mat2(rotationMatTmp[0][0], rotationMatTmp[0][1],
                            rotationMatTmp[1][0], rotationMatTmp[1][1]);
    // we can't do fract here, because here we opperate with vertices and result in pixels can be wrong
    TexCoord = rotationMat * TexCoord;
    // we don't want to deal with negative values in fragment shaders (that we will have after * scale - shift)
    // so we use this value for texturing and levels in fragment shader (0 - 60 grass, 60 - 120 - mountain, etc)
    Height = heightValue * 255.0;
    // displace point along normal
    p += normal * (heightValue * push.heightScale - push.heightShift);
    // ----------------------------------------------------------------------
    // output patch point position in clip space
    gl_Position = mvp.proj * mvp.view * mvp.model * p;
}