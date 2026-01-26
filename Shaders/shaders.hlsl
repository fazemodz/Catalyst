cbuffer SceneData : register(b0) {
    float4x4 wvpMatrix; // World-View-Projection combined
};

struct PSInput {
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PSInput VSMain(float4 position : POSITION, float4 color : COLOR) {
    PSInput result;
    // Standard multiplication order for DirectX (Row-Major)
    result.position = mul(position, wvpMatrix);
    result.color = color;
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET {
    return input.color;
}