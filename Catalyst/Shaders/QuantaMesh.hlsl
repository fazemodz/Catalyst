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
    float roughnessScale;
    uint debugMeshletSize;
    uint padding[3];
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
    ObjectData obj = ObjectBuffer[input.instanceID];
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
    float shadow = ShadowCalculation(input.worldPos);
    float3 ambient = float3(0.2, 0.2, 0.2) * albedo.rgb;
    float3 diffuse = diff * albedo.rgb * lightIntensity * shadow;

    float3 viewDir = normalize(cameraPos - input.worldPos.xyz);
    float3 halfVector = normalize(viewDir - lightDir);
    float specPower = lerp(64.0f, 8.0f, roughnessValue);
    float specStrength = (1.0f - roughnessValue) * 0.35f;
    float specular = pow(max(dot(shadingNormal, halfVector), 0.0f), specPower) * specStrength;

    float3 finalColor = ambient + diffuse + specular * lightIntensity * shadow;

    // Virtualised-geometry debug view: meshlets are contiguous runs of
    // debugMeshletSize triangles, so the primitive index names the meshlet.
    if (obj.debugMeshletSize > 0) {
        const uint meshletIndex = primitiveId / obj.debugMeshletSize;
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
