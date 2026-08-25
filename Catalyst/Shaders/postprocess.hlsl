Texture2D hdrTexture    : register(t0);
Texture2D shadowMask    : register(t1); // raytraced, r = visibility
Texture2D reflections   : register(t2); // raytraced, rgb = reflected radiance
SamplerState sampl      : register(s0);

// We pack these into strict float4 vectors so HLSL cannot invisibly pad them!
cbuffer PostProcessParams : register(b0) {
    float4 ppSettings1; // x: Exposure, yzw: Color Tint (RGB)
    float4 ppSettings2; // x: Bloom Threshold, y: Bloom Intensity, zw: HDR Target Size (Width, Height)
    float4 ppSettings3; // xy: UV extent the scene was rendered into, z: raytracing on, w: shadow strength
    float4 ppSettings4; // x: ambient occlusion strength, yzw: unused
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
    float2 uvScale = ppSettings3.xy;

    // 2. Read base scene color.
    // The scene is rasterised into the top-left uvScale fraction of the HDR
    // target (the rest of it is cleared black), so remap the fullscreen UVs
    // into that sub-rect and keep every tap inside it.
    float2 tex_offset = 1.0 / screenSize;
    float2 uvMin = tex_offset * 0.5;
    float2 uvMax = max(uvScale - tex_offset * 0.5, uvMin);
    float2 baseUV = clamp(input.uv * uvScale, uvMin, uvMax);

    float3 hdrColor = hdrTexture.SampleLevel(sampl, baseUV, 0).rgb;

    // Raytraced shadows and reflections. The shadow term multiplies the lit
    // colour rather than only its direct component, so it is floored to keep
    // shadowed surfaces sitting in ambient instead of going black.
    if (ppSettings3.z > 0.5f) {
        // The shadow mask is stochastic like the AO channel, so it needs the
        // same treatment: point-sampling it put the raw sampling noise straight
        // on screen. A 3x3 average is enough now the ray directions are
        // stratified, and it doubles as antialiasing on the shadow edge.
        float visibility = 0.0f;
        [unroll] for (int sx = -1; sx <= 1; ++sx) {
            [unroll] for (int sy = -1; sy <= 1; ++sy) {
                const float2 tap = clamp(baseUV + float2(sx, sy) * tex_offset, uvMin, uvMax);
                visibility += shadowMask.SampleLevel(sampl, tap, 0).r;
            }
        }
        visibility /= 9.0f;

        const float shadowFloor = 1.0f - saturate(ppSettings3.w);
        hdrColor *= lerp(shadowFloor, 1.0f, visibility);

        // Ambient occlusion rides in the reflection alpha. Averaging a small
        // neighbourhood costs one extra tap ring and removes most of the
        // per-pixel sampling noise without a separate denoise pass.
        const float aoStrength = saturate(ppSettings4.x);
        if (aoStrength > 0.001f) {
            // 5x5 box over the AO channel. The occlusion signal is very low
            // frequency, so a wide cheap blur buys far more than a clever
            // narrow one would.
            float occlusion = 0.0f;
            [unroll] for (int ox = -2; ox <= 2; ++ox) {
                [unroll] for (int oy = -2; oy <= 2; ++oy) {
                    const float2 tap = clamp(baseUV + float2(ox, oy) * tex_offset, uvMin, uvMax);
                    occlusion += reflections.SampleLevel(sampl, tap, 0).a;
                }
            }
            occlusion /= 25.0f;
            hdrColor *= lerp(1.0f, occlusion, aoStrength);
        }

        hdrColor += reflections.SampleLevel(sampl, baseUV, 0).rgb;
    }

    // 3. Smooth Single-Pass Bloom Extraction
    float3 bloom = float3(0, 0, 0);

    // Widen the spread for a softer, dreamier lens glow
    float blurSpread = 4.0;

    for(int x = -2; x <= 2; x++) {
        for(int y = -2; y <= 2; y++) {
            float2 offset = float2(x, y) * tex_offset * blurSpread;
            float3 sampleColor = hdrTexture.SampleLevel(sampl, clamp(baseUV + offset, uvMin, uvMax), 0).rgb;
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