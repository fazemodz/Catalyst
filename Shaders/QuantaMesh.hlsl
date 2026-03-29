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
    uint albedoIndex;
    uint normalIndex;
    uint metallicIndex;
    uint roughnessIndex;
    uint padding[5];
};

StructuredBuffer<ObjectData> ObjectBuffer : register(t0);
Texture2D BindlessTextures[1024] : register(t0, space1);
SamplerState LinearSampler : register(s0);

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
    // The shadow map path is not currently bound through the active descriptor heap
    // for this pass, so sampling it here produces invalid lighting on planes/floors.
    // Keep the lighting path stable until shadows are fully wired back in.
    return 1.0f;
}

float4 PSMain(PS_IN input) : SV_TARGET {
    ObjectData obj = ObjectBuffer[input.instanceID];
    float4 albedo = BindlessTextures[obj.albedoIndex].Sample(LinearSampler, input.uv) * input.color;
    float3 tangent = normalize(input.tangent);
    float3 normal = normalize(input.normal);
    tangent = normalize(tangent - normal * dot(tangent, normal));
    float3 bitangent = normalize(cross(normal, tangent));

    float3 sampledNormal = BindlessTextures[obj.normalIndex].Sample(LinearSampler, input.uv).xyz * 2.0f - 1.0f;
    float3x3 tbn = float3x3(tangent, bitangent, normal);
    float3 shadingNormal = normalize(mul(sampledNormal, tbn));

    float roughnessValue = saturate(BindlessTextures[obj.roughnessIndex].Sample(LinearSampler, input.uv).r);
    float diff = max(dot(shadingNormal, -lightDir), 0.0);
    float shadow = ShadowCalculation(input.worldPos);
    float3 ambient = float3(0.2, 0.2, 0.2) * albedo.rgb;
    float3 diffuse = diff * albedo.rgb * lightIntensity * shadow;

    float3 viewDir = normalize(cameraPos - input.worldPos.xyz);
    float3 halfVector = normalize(viewDir - lightDir);
    float specPower = lerp(64.0f, 8.0f, roughnessValue);
    float specStrength = (1.0f - roughnessValue) * 0.35f;
    float specular = pow(max(dot(shadingNormal, halfVector), 0.0f), specPower) * specStrength;

    float3 finalColor = ambient + diffuse + specular * lightIntensity * shadow;
    return float4(finalColor, albedo.a);
}
