cbuffer ConstantBuffer : register(b0) {
    matrix wvpMatrix; 
};

struct VS_IN {
    float3 pos : POSITION;
};

float4 VSMain(VS_IN input) : SV_POSITION {
    return mul(float4(input.pos, 1.0f), wvpMatrix);
}