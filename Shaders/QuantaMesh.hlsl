// ============================================================================
// QuantaMesh: GPU-Driven, Bindless Uber-Shader
// ============================================================================

// Bindless Textures: One global array of textures.
// We use 'space1' to avoid register collisions with your shadow maps or global textures.
Texture2D BindlessTextures[1024] : register(t0, space1);

// Standard samplers for materials and shadow filtering
SamplerState sampl                     : register(s0);
SamplerComparisonState shadowSampler   : register(s1); 

// Global Scene Data
cbuffer GlobalBuffer : register(b0) {
    matrix viewProj;
    matrix lightSpaceMatrix;
    float3 lightDir;
    float lightIntensity;
    float3 cameraPos;
    float globalPadding;
};

// Material-specific data (The "Unreal-style" property block)
cbuffer MaterialConstants : register(b1) {
    float4 materialColor;
    float  metallic;
    float  roughness;
    uint   albedoIndex;
    uint   normalIndex;
    uint   metallicIndex;
    uint   roughnessIndex;
    float4 materialPadding;
};

// The GPU array of objects populated by the Compute Shader
struct ObjectData {
    matrix worldMatrix;
    float4 colorOverride;
    float3 center;
    float radius;
    uint indexCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint padding;
};

StructuredBuffer<ObjectData> ObjectBuffer : register(t0);

// Shadow Map (Explicitly bound, not bindless)
Texture2D shadowMap : register(t7); 

struct VS_IN {
    float3 pos     : POSITION;
    float4 color   : COLOR;
    float2 uv      : TEXCOORD;
    float3 normal  : NORMAL;
    float3 tangent : TANGENT;
};

struct PS_IN {
    float4 pos      : SV_POSITION;
    float4 worldPos : POSITION;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD;
    float3 normal   : NORMAL;
    float3 tangent  : TANGENT;
    uint   instanceID : INSTANCE_ID;
};

PS_IN VSMain(VS_IN input, uint instanceID : SV_InstanceID) {
    PS_IN output;
    
    // Fetch object data via Indirect Index
    ObjectData obj = ObjectBuffer[instanceID];
    
    float4 worldPos = mul(float4(input.pos, 1.0f), obj.worldMatrix);
    output.pos = mul(worldPos, viewProj);
    output.worldPos = worldPos;
    output.color = obj.colorOverride * materialColor;
    output.uv = input.uv;
    output.normal = mul(input.normal, (float3x3)obj.worldMatrix);
    output.tangent = mul(input.tangent, (float3x3)obj.worldMatrix);
    output.instanceID = instanceID;
    
    return output;
}

float4 PSMain(PS_IN input) : SV_TARGET {
    // Fetch textures using Bindless IDs
    float4 albedo = BindlessTextures[albedoIndex].Sample(sampl, input.uv) * input.color;
    float3 N = normalize(input.normal);
    float3 L = normalize(-lightDir);
    float3 V = normalize(cameraPos - input.worldPos.xyz);
    
    // Basic Diffuse Lighting
    float nDotL = max(dot(N, L), 0.0);
    float3 diffuse = albedo.rgb * nDotL * lightIntensity;
    float3 ambient = albedo.rgb * 0.1;

    // Shadow Calculation
    float4 posLightSpace = mul(float4(input.worldPos.xyz, 1.0), lightSpaceMatrix);
    float3 projCoords = posLightSpace.xyz / posLightSpace.w;
    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = -projCoords.y * 0.5 + 0.5;

    float shadow = 1.0;
    if(projCoords.z <= 1.0 && projCoords.x >= 0.0 && projCoords.x <= 1.0 && projCoords.y >= 0.0 && projCoords.y <= 1.0) {
        float currentDepth = projCoords.z;
        float bias = max(0.005 * (1.0 - dot(N, L)), 0.001);
        float2 texelSize = 1.0 / 2048.0;
        float shadowSum = 0.0;
        for(int x = -1; x <= 1; ++x) {
            for(int y = -1; y <= 1; ++y) {
                shadowSum += shadowMap.SampleCmpLevelZero(shadowSampler, projCoords.xy + float2(x, y) * texelSize, currentDepth - bias);
            }
        }
        shadow = shadowSum / 9.0;
    }
    float d1 = BindlessTextures[normalIndex].Sample(sampl, input.uv).r;
    float d2 = BindlessTextures[metallicIndex].Sample(sampl, input.uv).r;
    float d3 = BindlessTextures[roughnessIndex].Sample(sampl, input.uv).r;

    float3 finalColor = ambient + (diffuse * shadow) + (d1+d2+d3) * 0.000001f;
    
    return float4(finalColor, albedo.a);
}