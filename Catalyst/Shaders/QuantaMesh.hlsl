// ==========================================
// QUANTA MESH SHADER
// ==========================================

cbuffer GlobalBufferData : register(b0) {
    float4x4 viewProj;
    float4x4 lightSpaceMatrix;
    float3 lightDir;
    float lightIntensity;
    float3 cameraPos;
    float padding;
    float4 shadowParams;   // x: uv texel, y: world texel, z: enabled, w: spare
};

cbuffer MaterialConstants : register(b1) {
    float4 materialColor;
    float metallic;
    float roughness;
    uint albedoIndex;
    uint normalIndex;
    uint metallicIndex;
    uint roughnessIndex;
    float2 padding2;
};

cbuffer InstanceData : register(b2) {
    uint globalInstanceID;
};

struct ObjectData {
    float4x4 worldMatrix;        
    float4 colorOverride;        
    float3 center;               
    float radius;                
    uint indexCount;             
    uint startIndexLocation;     
    int baseVertexLocation;      
    uint albedoIndex;
    uint normalIndex;
    uint metallicIndex;
    uint roughnessIndex;
    float roughnessScale;
    uint debugMeshletSize;
    uint padding[3];
};

StructuredBuffer<ObjectData> ObjectBuffer : register(t0);
Texture2D BindlessTextures[1024] : register(t0, space1);
Texture2D ShadowMap : register(t7);
SamplerState LinearSampler : register(s0);
// Comparison sampler: the hardware does the depth test and bilinear-filters the
// result, so one tap already returns a 2x2 average rather than a hard bit.
SamplerComparisonState ShadowSampler : register(s1);

struct VS_IN {
    float3 pos : POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
    float2 normal : NORMAL;    // octahedral, see PackedVertex in Mesh.h
    float2 tangent : TANGENT;  // octahedral
};

// Unfolds the octahedral pair back into a unit vector.
float3 OctahedralDecode(float2 encoded) {
    float3 n = float3(encoded.x, encoded.y, 1.0f - abs(encoded.x) - abs(encoded.y));
    float fold = saturate(-n.z);
    n.x += (n.x >= 0.0f) ? -fold : fold;
    n.y += (n.y >= 0.0f) ? -fold : fold;
    return normalize(n);
}

struct PS_IN {
    float4 pos : SV_POSITION;
    float4 worldPos : POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    uint instanceID : BLENDINDICES; 
};

PS_IN VSMain(VS_IN input) {
    PS_IN output;
    
    ObjectData obj = ObjectBuffer[globalInstanceID & 0xFFFFu];
    
    float4 worldPos = mul(float4(input.pos, 1.0f), obj.worldMatrix);
    output.pos = mul(worldPos, viewProj);
    output.worldPos = worldPos;
    
    output.color = obj.colorOverride * materialColor;
    output.uv = input.uv;
    output.normal = mul(OctahedralDecode(input.normal), (float3x3)obj.worldMatrix);
    output.tangent = mul(OctahedralDecode(input.tangent), (float3x3)obj.worldMatrix);
    
    output.instanceID = globalInstanceID;
    
    return output;
}

// Percentage-closer filtering against the directional light's depth map.
// Returns 1 in full light, 0 in full shadow.
float ShadowCalculation(float3 worldPos, float3 normal) {
    float lit = 1.0f;

    if (shadowParams.z >= 0.5f) {
        // Normal-offset bias. Moving the lookup off the surface along its own
        // normal scales with how slanted the surface is relative to the light,
        // which clears acne at grazing angles without the peter-panning that a
        // large constant depth bias causes.
        float slant = saturate(1.0f - dot(normal, -normalize(lightDir)));
        float3 offsetPos = worldPos + normal * (shadowParams.y * (1.0f + 2.0f * slant));

        float4 lightClip = mul(float4(offsetPos, 1.0f), lightSpaceMatrix);
        float3 projected = (lightClip.w > 0.0f) ? (lightClip.xyz / lightClip.w) : float3(2.0f, 2.0f, 2.0f);

        // Outside the map there is nothing to be occluded by. The sampler's
        // white border answers "lit" anyway, but this keeps it explicit.
        bool inside = lightClip.w > 0.0f &&
                      abs(projected.x) <= 1.0f && abs(projected.y) <= 1.0f &&
                      projected.z >= 0.0f && projected.z <= 1.0f;

        if (inside) {
            float2 shadowUv = float2(projected.x * 0.5f + 0.5f, -projected.y * 0.5f + 0.5f);
            float compareDepth = projected.z;

            // 3x3 hardware-filtered taps, so a 6x6 effective kernel.
            float sum = 0.0f;
            [unroll]
            for (int y = -1; y <= 1; ++y) {
                [unroll]
                for (int x = -1; x <= 1; ++x) {
                    float2 offset = float2(x, y) * shadowParams.x;
                    sum += ShadowMap.SampleCmpLevelZero(ShadowSampler, shadowUv + offset, compareDepth);
                }
            }
            lit = sum / 9.0f;
        }
    }

    return lit;
}

// Target 1 is the raytracing G-buffer. Pipelines that bind a single render
// target simply discard it, so one shader serves both the plain and the
// G-buffer PSO variants.
struct PS_OUT {
    float4 color     : SV_TARGET0;
    float4 gbuffer   : SV_TARGET1; // xyz: world normal * 0.5 + 0.5, w: roughness
    float4 worldPos  : SV_TARGET2; // xyz: world position (full float precision)
};

// Cheap integer hash spread over the colour wheel so neighbouring meshlets
// never land on the same hue.
float3 MeshletDebugColor(uint meshletIndex) {
    uint h = meshletIndex * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    const float hue = float(h & 0xFFFFu) / 65535.0f;
    const float3 phase = float3(0.0f, 2.0943951f, 4.1887902f);
    return saturate(0.55f + 0.45f * cos(6.2831853f * hue + phase));
}

PS_OUT PSMain(PS_IN input, uint primitiveId : SV_PrimitiveID) {
    ObjectData obj = ObjectBuffer[input.instanceID & 0xFFFFu];
    float4 albedo = BindlessTextures[obj.albedoIndex].Sample(LinearSampler, input.uv) * input.color;
    float3 tangent = normalize(input.tangent);
    float3 normal = normalize(input.normal);
    tangent = normalize(tangent - normal * dot(tangent, normal));
    float3 bitangent = normalize(cross(normal, tangent));

    float3 sampledNormal = BindlessTextures[obj.normalIndex].Sample(LinearSampler, input.uv).xyz * 2.0f - 1.0f;
    float3x3 tbn = float3x3(tangent, bitangent, normal);
    float3 shadingNormal = normalize(mul(sampledNormal, tbn));

    // Objects carry a roughness scale so a surface can be made reflective
    // without authoring a roughness texture for it.
    float roughnessValue = saturate(BindlessTextures[obj.roughnessIndex].Sample(LinearSampler, input.uv).r * obj.roughnessScale);
    float diff = max(dot(shadingNormal, -lightDir), 0.0);
    float shadow = ShadowCalculation(input.worldPos.xyz, normal);
    float3 ambient = float3(0.2, 0.2, 0.2) * albedo.rgb;
    float3 diffuse = diff * albedo.rgb * lightIntensity * shadow;

    float3 viewDir = normalize(cameraPos - input.worldPos.xyz);
    float3 halfVector = normalize(viewDir - lightDir);
    float specPower = lerp(64.0f, 8.0f, roughnessValue);
    float specStrength = (1.0f - roughnessValue) * 0.35f;
    float specular = pow(max(dot(shadingNormal, halfVector), 0.0f), specPower) * specStrength;

    float3 finalColor = ambient + diffuse + specular * lightIntensity * shadow;

    // Virtualised-geometry debug view. Every cluster is now its own indirect
    // draw, so SV_PrimitiveID restarts at zero for each one and cannot name the
    // cluster; the cull shader packs the cluster index into the draw id instead.
    if (obj.debugMeshletSize > 0) {
        const uint meshletIndex = input.instanceID >> 16;
        finalColor = MeshletDebugColor(meshletIndex) * (0.45f + 0.55f * saturate(diff));
    }

    PS_OUT output;
    output.color   = float4(finalColor, albedo.a);
    output.gbuffer = float4(normalize(shadingNormal) * 0.5f + 0.5f, roughnessValue);
    // Written rather than reconstructed from depth: with a 0.1..5000 depth range
    // the reconstruction error dwarfs any usable ray bias.
    output.worldPos = float4(input.worldPos.xyz, 1.0f);
    return output;
}
