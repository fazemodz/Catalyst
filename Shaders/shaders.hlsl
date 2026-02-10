struct VSInput {
    float3 position : POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
};

// Output from Vertex Shader / Input to Pixel Shader
struct VSOutput {
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
};

cbuffer ConstantBuffer : register(b0) {
    float4x4 wvpMatrix;
    float4x4 worldMatrix; 
    float4 colorOverride;
    float3 lightDir;
    float lightIntensity;
    float3 cameraPos;
    float visualizationMode;
};

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

// --- VERTEX SHADER ---
VSOutput VSMain(VSInput input) {
    VSOutput result;
    result.position = mul(float4(input.position, 1.0f), wvpMatrix);
    result.worldPos = mul(float4(input.position, 1.0f), worldMatrix).xyz;
    
    result.normal = normalize(mul(input.normal, (float3x3)worldMatrix));
    result.tangent = normalize(mul(input.tangent, (float3x3)worldMatrix));
    
    result.color = input.color * colorOverride;
    result.uv = input.uv;
    return result;
}

// --- HELPER: Random Color Generator ---
float3 HashColor(uint seed) {
    uint n = seed * 1327217885;
    n = (n << 13) ^ n;
    n *= 1327217885;
    float r = ((n * 13) % 255) / 255.0f;
    float g = ((n * 23) % 255) / 255.0f;
    float b = ((n * 57) % 255) / 255.0f;
    return float3(r, g, b);
}

// --- PIXEL SHADER ---
// Note: primID is passed as a separate argument!
float4 PSMain(VSOutput input, uint primID : SV_PrimitiveID) : SV_TARGET {
    
    // --- VIRTUAL GEOMETRY VISUALIZER ---
    if (visualizationMode > 0.5f) {
        uint clusterID = primID / 126; // Approx cluster ID
        float3 clusterColor = HashColor(clusterID);
        
        float3 N = normalize(input.normal);
        float3 L = normalize(-lightDir);
        float diff = max(dot(N, L), 0.2f);
        return float4(clusterColor * diff, 1.0f);
    }

    // --- STANDARD RENDER PATH ---
    float4 texColor = g_texture.Sample(g_sampler, input.uv);
    
    float3 T = normalize(input.tangent);
    float3 N = normalize(input.normal);
    // Gram-Schmidt
    T = normalize(T - dot(T, N) * N);
    
    // Simple Lighting
    float3 finalNormal = N; 
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