cbuffer SceneData : register(b0) {
    float4x4 wvpMatrix;
    float4 colorOverride; // NEW: Added this
};

struct PSInput {
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PSInput VSMain(float4 position : POSITION, float4 color : COLOR) {
    PSInput result;
    result.position = mul(position, wvpMatrix);
    result.color = color; // Pass vertex color through
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET {
    // Multiply vertex color (gradient) by object color (tint)
    return input.color * colorOverride;
}