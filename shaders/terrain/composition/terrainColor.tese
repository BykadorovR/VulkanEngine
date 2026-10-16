// tessellation evaluation shader
#version 450 core

layout (quads, fractional_odd_spacing, ccw) in;

// received from Tessellation Control Shader - all texture coordinates for the patch vertices
layout (location = 0) in vec2 TextureCoord[];
layout (location = 1) in vec4 inTilesWeights[];
layout (location = 2) flat in int inTextureID[];
layout (location = 3) flat in ivec4 inNeighboursID[];
layout (location = 4) flat in int inRotation[];

layout(set = 0, binding = 2) uniform UniformCamera {
    mat4 model;
    mat4 view;
    mat4 proj;
} mvp;

layout(set = 0, binding = 3) uniform sampler2D heightMap;

// send to Fragment Shader for coloring
layout (location = 0) out vec2 TexCoord;
layout (location = 1) out vec3 normalCross;
layout (location = 2) out vec4 outTilesWeights;
layout (location = 3) flat out int outRotation;
layout (location = 4) out vec3 fragPosition;
layout (location = 5) out mat3 fragTBN;
layout (location = 8) out vec4 fragLightDirectionalCoord[2];

layout(std140, set = 1, binding = 0) readonly buffer LightMatrixDirectional {
    int lightDirectionalNumber;
    mat4 lightDirectionalVP[];
};

layout( push_constant ) uniform constants {    
    layout(offset = 24) int patchDimX;
    int patchDimY;
    float heightScale;
    float heightShift;
} push;

void main() {
// get vertex coordinate (2D)
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;

    outRotation = inRotation[0];

    // tiles weights corners
    vec4 tw00 = inTilesWeights[0];
    vec4 tw01 = inTilesWeights[1];
    vec4 tw10 = inTilesWeights[2];
    vec4 tw11 = inTilesWeights[3];
    vec4 tw0 = (tw01 - tw00) * u + tw00;
    vec4 tw1 = (tw11 - tw10) * u + tw10;
    vec4 weight = (tw1 - tw0) * v + tw0;

    //check edge left, top, right, bot or inside:
    if (u == 0 && v > 0 && v < 1) {
        //left
        weight = vec4(0, 0, 0, 0);
        weight[inTextureID[0]] = 1;
        weight[inNeighboursID[0][0]] = 1;
    } else if (v == 0 && u > 0 && u < 1) {
        //top
        weight = vec4(0, 0, 0, 0);
        weight[inTextureID[0]] = 1;
        weight[inNeighboursID[0][1]] = 1;
    } else if (u == 1 && v > 0 && v < 1) {
        //right
        weight = vec4(0, 0, 0, 0);
        weight[inTextureID[0]] = 1;
        weight[inNeighboursID[0][2]] = 1;
    } else if (v == 1 && u > 0 && u < 1) {
        //bot
        weight = vec4(0, 0, 0, 0);
        weight[inTextureID[0]] = 1;
        weight[inNeighboursID[0][3]] = 1;
    } else if (v > 0 && v < 1 && u > 0 && u < 1) {
        //inside
        weight = vec4(0, 0, 0, 0);
        weight[inTextureID[0]] = 1;
    }
    outTilesWeights = weight;

    // ----------------------------------------------------------------------
    // retrieve control point texture coordinates for height map
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

    float heightValue = texture(heightMap, TexCoord).x;
    vec2 texCoord = TexCoord;
    TexCoord = vec2(u, v);
    // displace point along normal
    p += normal * (heightValue * push.heightScale - push.heightShift);
    // ----------------------------------------------------------------------
    // output patch point position in clip space
    gl_Position = mvp.proj * mvp.view * mvp.model * p;
    fragPosition = (mvp.model * p).xyz;
    //
    //we sample from full size texture only, because we use stepCoords
    vec2 textureSize = textureSize(heightMap, 0);
    //classic implementation
    //vec2 stepCoords = vec2(1.0, 1.0);
    //my implementation
    vec2 stepCoords = textureSize / (vec2(push.patchDimX, push.patchDimY) * vec2(gl_TessLevelInner[0], gl_TessLevelInner[1]));
    vec2 stepTexture = stepCoords / textureSize;

    float left = texture(heightMap, texCoord + vec2(-1 * stepTexture.x, 0)).y * push.heightScale - push.heightShift;
    float right = texture(heightMap, texCoord + vec2(1 * stepTexture.x, 0)).y * push.heightScale - push.heightShift;
    //in Vulkan 0, 0 is top-left corner
    float top = texture(heightMap, texCoord + vec2(0, -1 * stepTexture.y)).y * push.heightScale - push.heightShift;
    float bottom = texture(heightMap, texCoord + vec2(0, 1 * stepTexture.y)).y * push.heightScale - push.heightShift;
    
    //here we don't change right - left and top - bottom, it's always it.
    //direction of cross product is calculated by right hand rule
    //we don't depend on p
    vec3 tangent = vec3(2.0 * stepCoords.x, right - left, 0.0);
    vec3 bitangent = vec3(0.0, top - bottom, -2.0 * stepCoords.y);
    normalCross = normalize(cross(tangent, bitangent));    
    fragTBN = mat3(tangent, bitangent, normalCross);

    for (int i = 0; i < lightDirectionalNumber; i++)
        fragLightDirectionalCoord[i] = lightDirectionalVP[i] * mvp.model * p;
}