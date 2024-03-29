const float PI = 3.14159265359;

// roughness emulation
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom + epsilon; //to avoid division by zero

    return nom / denom;
}

// geometry obstruction / geometry shadowing
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

// geometry obstruction / geometry shadowing
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}   

vec3 calculateOutRadiance(vec3 lightDir, vec3 normal, vec3 viewDir, vec3 inRadiance, float metallicValue, float roughnessValue, vec3 albedo) {
    // calculate reflectance at normal incidence; if dia-electric (like plastic) use F0 
    // of 0.04 and if it's a metal, use the albedo color as F0 (metallic workflow)    
    vec3 F0 = vec3(0.04);        
    F0 = mix(F0, albedo, metallicValue);

    vec3 halfway = normalize(viewDir + lightDir);
    // Cook-Torrance BRDF
    float NDF = DistributionGGX(normal, halfway, roughnessValue);
    //NDF will be multiplied by inRadiance so need to limit by 1.0
    NDF = clamp(NDF, 0.0, 1.0);
    float G   = GeometrySmith(normal, viewDir, lightDir, roughnessValue);
    vec3 F    = fresnelSchlick(max(dot(halfway, viewDir), 0.0), F0);

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(normal, viewDir), 0.0) * max(dot(normal, lightDir), 0.0) + 0.0001; // + 0.0001 to prevent divide by zero
    vec3 specular = numerator / denominator;
        
    // kS is equal to Fresnel
    vec3 kS = F;
    // for energy conservation, the diffuse and specular light can't
    // be above 1.0 (unless the surface emits light); to preserve this
    // relationship the diffuse component (kD) should equal 1.0 - kS.
    vec3 kD = vec3(1.0) - kS;
    // multiply kD by the inverse metalness such that only non-metals 
    // have diffuse lighting, or a linear blend if partly metal (pure metals
    // have no diffuse light).
    kD *= (1.0 - metallicValue);
    
    // scale light by NdotL
    float NdotL = max(dot(normal, lightDir), 0.0);    

    // add to outgoing radiance Lr
    vec3 Lr = (kD * albedo / PI + specular) * inRadiance * NdotL;  // note that we already multiplied the BRDF by the Fresnel (kS) so we won't multiply by kS again
    return Lr;
}

vec3 calculateIBL(float occlusion, vec3 normal, vec3 viewDir, float metallicValue, float roughnessValue, vec3 albedo) {
    vec3 F0 = vec3(0.04);     
    F0 = mix(F0, albedo, metallicValue);
    vec3 kS = fresnelSchlickRoughness(max(dot(normal, viewDir), 0.0), F0, roughnessValue);
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallicValue;
    vec3 irradiance = texture(getIrradianceSampler(), normal).rgb;
    vec3 diffuse = kD * irradiance * albedo;

    vec3 R = reflect(-viewDir, normal);   
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(getSpecularIBLSampler(), R,  roughnessValue * MAX_REFLECTION_LOD).rgb;
    vec2 envBRDF  = texture(getSpecularBRDFSampler(), vec2(max(dot(normal, viewDir), 0.0), roughnessValue)).rg;
    
    vec3 specular = prefilteredColor * (kS * envBRDF.x + envBRDF.y);
    vec3 ambientColor = mix(diffuse + specular, (diffuse + specular) * occlusion, getMaterial().occlusionStrength);
    return ambientColor;
}