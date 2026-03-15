cbuffer cb : register(b0) {
    matrix wvp;
};

Texture2D envMap : register(t0);
SamplerState sampl : register(s0);

struct VS_IN {
    float3 pos : POSITION;
};

struct PS_IN {
    float4 pos : SV_POSITION;
    float3 localPos : TEXCOORD0;
};

static const float2 invAtan = float2(0.1591, 0.3183);

PS_IN VSMain(VS_IN input) {
    PS_IN output;
    output.localPos = input.pos;
    float4 clipPos = mul(float4(input.pos, 0.0f), wvp);
    output.pos = clipPos.xyww;
    return output;
}

float4 PSMain(PS_IN input) : SV_TARGET {
    float3 dir = normalize(input.localPos);
    float2 uv = float2(atan2(dir.z, -dir.x), asin(-dir.y));
    
    uv *= invAtan;
    uv += 0.5;
    
    float3 color = envMap.SampleLevel(sampl, uv, 0).rgb;
    
    // Raw HDR output
    return float4(color, 1.0);
}