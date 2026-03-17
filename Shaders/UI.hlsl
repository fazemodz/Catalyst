Texture2D BindlessTextures[1024] : register(t0, space1);
SamplerState PointSampler : register(s0);

cbuffer UIConstants : register(b0) {
    float4x4 ProjectionMatrix;
    uint TextureIndex;
};

struct VS_INPUT {
    float2 pos : POSITION;
    float2 uv  : TEXCOORD;
    float4 col : COLOR; 
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD;
    float4 col : COLOR;
};

PS_INPUT VSMain(VS_INPUT input) {
    PS_INPUT output;
    output.pos = mul(float4(input.pos, 0.0f, 1.0f), ProjectionMatrix);
    output.uv  = input.uv;
    output.col = input.col;
    return output;
}

float4 PSMain(PS_INPUT input) : SV_Target {
    float4 texColor = BindlessTextures[TextureIndex].Sample(PointSampler, input.uv);
    return input.col * texColor;
}