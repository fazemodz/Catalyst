cbuffer Constants : register(b0) {
    matrix viewProj;
    float3 topColor;
    float pad1;
    float3 bottomColor;
    float pad2;
};

struct VSInput { 
    float3 pos : POSITION; 
    float3 norm : NORMAL;
    float2 uv : TEXCOORD;
};

struct PSInput { 
    float4 pos : SV_POSITION; 
    float3 localPos : TEXCOORD; 
};

PSInput VSMain(VSInput input) {
    PSInput output;
    output.localPos = input.pos;
    
    float4 clipPos = mul(float4(input.pos, 1.0f), viewProj);
    output.pos = clipPos.xyww; 
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET {
    float3 dir = normalize(input.localPos);
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    return float4(lerp(bottomColor, topColor, t), 1.0f);
}