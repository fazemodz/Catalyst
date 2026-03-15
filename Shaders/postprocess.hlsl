Texture2D hdrTexture : register(t0);
SamplerState sampl   : register(s0);

// We pack these into strict float4 vectors so HLSL cannot invisibly pad them!
cbuffer PostProcessParams : register(b0) {
    float4 ppSettings1; // x: Exposure, yzw: Color Tint (RGB)
    float4 ppSettings2; // x: Bloom Threshold, y: Bloom Intensity, zw: Screen Size (Width, Height)
};

struct PS_IN {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD;
};

PS_IN VSMain(uint id : SV_VertexID) {
    PS_IN output;
    output.uv = float2((id << 1) & 2, id & 2);
    output.pos = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float4 PSMain(PS_IN input) : SV_TARGET {
    // 1. Unpack variables safely
    float exposure = ppSettings1.x;
    float3 colorTint = ppSettings1.yzw;
    float bloomThreshold = ppSettings2.x;
    float bloomIntensity = ppSettings2.y;
    float2 screenSize = ppSettings2.zw;

    // 2. Read base scene color
    float3 hdrColor = hdrTexture.SampleLevel(sampl, input.uv, 0).rgb;

    // 3. Smooth Single-Pass Bloom Extraction
    float2 tex_offset = 1.0 / screenSize;
    float3 bloom = float3(0, 0, 0);

    // Widen the spread for a softer, dreamier lens glow
    float blurSpread = 4.0;

    for(int x = -2; x <= 2; x++) {
        for(int y = -2; y <= 2; y++) {
            float2 offset = float2(x, y) * tex_offset * blurSpread;
            float3 sampleColor = hdrTexture.SampleLevel(sampl, input.uv + offset, 0).rgb;
            float brightness = max(sampleColor.r, max(sampleColor.g, sampleColor.b));
            float glowAmount = max(0.0, brightness - bloomThreshold);

            bloom += sampleColor * (glowAmount / max(brightness, 0.00001));
        }
    }

    // Average out the 25 samples
    bloom /= 25.0;

    // Add the glowing light back into the base image
    hdrColor += (bloom * bloomIntensity);

    // 4. Apply Camera Exposure and Tint
    hdrColor *= exposure;
    hdrColor *= colorTint;

    // 5. ACES Filmic Tonemapping
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    float3 mapped = clamp((hdrColor*(a*hdrColor+b))/(hdrColor*(c*hdrColor+d)+e), 0.0, 1.0);

    // 6. Gamma Correction
    mapped = pow(mapped, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));

    return float4(mapped, 1.0);
}