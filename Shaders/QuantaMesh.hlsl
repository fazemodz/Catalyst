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
    uint padding;                
};

StructuredBuffer<ObjectData> ObjectBuffer : register(t0);
Texture2D BindlessTextures[1024] : register(t0, space1);
SamplerState LinearSampler : register(s0);
Texture2D ShadowMap : register(t7);
SamplerComparisonState ShadowSampler : register(s1);

struct VS_IN {
    float3 pos : POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
};

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
    
    ObjectData obj = ObjectBuffer[globalInstanceID];
    
    float4 worldPos = mul(float4(input.pos, 1.0f), obj.worldMatrix);
    output.pos = mul(worldPos, viewProj);
    output.worldPos = worldPos;
    
    output.color = obj.colorOverride * materialColor;
    output.uv = input.uv;
    output.normal = mul(input.normal, (float3x3)obj.worldMatrix);
    output.tangent = mul(input.tangent, (float3x3)obj.worldMatrix);
    
    output.instanceID = globalInstanceID;
    
    return output;
}

float ShadowCalculation(float4 worldPos) {
    float shadow = 0.0f; 
    
    float4 lightSpacePos = mul(worldPos, lightSpaceMatrix);
    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.x =  projCoords.x * 0.5f + 0.5f;
    projCoords.y = -projCoords.y * 0.5f + 0.5f;
    if(projCoords.z > 1.0f) {
        return 1.0f; 
    }

    float currentDepth = projCoords.z;
    float bias = max(0.005f * (1.0f - dot(float3(0,1,0), -lightDir)), 0.001f);
    
    float2 texelSize = 1.0f / 2048.0f; 
    
    [unroll]
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float2 uv = projCoords.xy + float2(x,y) * texelSize;
            shadow += ShadowMap.SampleCmpLevelZero(ShadowSampler, uv, currentDepth - bias);
        }
    }
    return shadow / 9.0f;
}

float4 PSMain(PS_IN input) : SV_TARGET {
    float4 albedo = BindlessTextures[albedoIndex].Sample(LinearSampler, input.uv) * input.color;
    
    float3 norm = normalize(input.normal);
    float diff = max(dot(norm, -lightDir), 0.0);
    
    float shadow = ShadowCalculation(input.worldPos);
    
    float3 ambient = float3(0.2, 0.2, 0.2) * albedo.rgb;
    float3 diffuse = diff * albedo.rgb * lightIntensity * shadow;
    
    float3 finalColor = ambient + diffuse;
    return float4(finalColor, albedo.a);
}