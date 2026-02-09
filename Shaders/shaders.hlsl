struct VSInput {
    float3 position : POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT; // <--- NEW Input
};

struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT; // <--- Pass to PS
};

cbuffer ConstantBuffer : register(b0) {
    float4x4 wvpMatrix;
    float4x4 worldMatrix; 
    float4 colorOverride;
    float3 lightDir;
    float lightIntensity;
    float3 cameraPos;
    float padding;
};

Texture2D g_texture : register(t0);
// Texture2D g_normalMap : register(t1); // Future
SamplerState g_sampler : register(s0);

// --- VERTEX SHADER ---
PSInput VSMain(VSInput input) {
    PSInput result;
    result.position = mul(float4(input.position, 1.0f), wvpMatrix);
    result.worldPos = mul(float4(input.position, 1.0f), worldMatrix).xyz;
    
    // Transform Normal and Tangent to World Space
    result.normal = normalize(mul(input.normal, (float3x3)worldMatrix));
    result.tangent = normalize(mul(input.tangent, (float3x3)worldMatrix));
    
    result.color = input.color * colorOverride;
    result.uv = input.uv;
    return result;
}

// --- PIXEL SHADER ---
float4 PSMain(PSInput input) : SV_TARGET {
    float4 texColor = g_texture.Sample(g_sampler, input.uv);
    
    // --- NORMAL MAPPING MATH ---
    // 1. Build TBN Matrix
    float3 T = normalize(input.tangent);
    float3 N = normalize(input.normal);
    // Gram-Schmidt re-orthogonalization to ensure T is perp to N
    T = normalize(T - dot(T, N) * N);
    float3 B = cross(N, T); // Bitangent
    float3x3 TBN = float3x3(T, B, N);
    
    // 2. Sample Normal Map (Simulated for now using N)
    // float3 normalMap = g_normalMap.Sample(g_sampler, input.uv).rgb;
    // normalMap = normalMap * 2.0 - 1.0;
    // float3 bumpedNormal = normalize(mul(normalMap, TBN));
    
    // For now, use basic N until we handle the descriptor heap for 2 textures
    float3 finalNormal = N; 
    
    // --- LIGHTING ---
    float3 L = normalize(-lightDir);
    float3 V = normalize(cameraPos - input.worldPos);
    
    float3 ambient = 0.3f * float3(1,1,1);
    float diff = max(dot(finalNormal, L), 0.0f);
    float3 diffuse = diff * float3(1,1,1) * lightIntensity;
    
    float3 halfDir = normalize(L + V);
    float spec = pow(max(dot(finalNormal, halfDir), 0.0f), 16);
    float3 specular = 0.3f * spec * float3(1,1,1) * lightIntensity;
    
    return float4((ambient + diffuse + specular) * texColor.rgb * input.color.rgb, texColor.a);
}