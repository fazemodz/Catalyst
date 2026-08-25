cbuffer SceneData : register(b0) {
    matrix viewProj;
};

struct VS_IN {
    float3 pos     : POSITION;
    float4 color   : COLOR;
    float2 uv      : TEXCOORD;
    float3 normal  : NORMAL;
    float3 tangent : TANGENT;
};

struct PS_IN {
    float4 pos   : SV_POSITION;
    float4 color : COLOR;
    float2 uv    : TEXCOORD;
};

PS_IN VSMain(VS_IN input) {
    PS_IN output;
    output.pos = mul(float4(input.pos, 1.0f), viewProj);
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

Texture2D albedo : register(t0);
SamplerState sampl : register(s0);

float4 PSMain(PS_IN input) : SV_TARGET {
    return albedo.Sample(sampl, input.uv) * input.color;
}