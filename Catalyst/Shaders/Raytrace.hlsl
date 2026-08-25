// Hardware raytracing via DXR 1.1 inline RayQuery.
//
// Reads the raster G-buffer (depth + world normal/roughness), traces one shadow
// ray and one reflection ray per pixel, and writes two screen-space targets that
// the post-process composite folds into the lit image.
//
// Requires shader model 6.5, so this is the one shader the engine does not
// compile at runtime with FXC. Rebuild it with:
//   dxc -T cs_6_5 -E CSMain Shaders/Raytrace.hlsl -Fo Shaders/Raytrace.cso

struct InstanceShading {
    float3 baseColor;
    float  reflectivity;
    uint   vertexBufferIndex;   // slots into MeshBuffers[]
    uint   indexBufferIndex;
    uint2  padding;
};

// Vertex is { float3 position; float4 color; float2 uv; float3 normal; float3 tangent; }
static const uint kVertexStride = 60;
static const uint kVertexNormalOffset = 36;

RaytracingAccelerationStructure Scene    : register(t0);
Texture2D<float>                Depth    : register(t1);
Texture2D<float4>               GBuffer  : register(t2); // xyz: normal*0.5+0.5, w: roughness
Texture2D<float4>               Positions : register(t3); // xyz: world position, w: 1 where geometry was drawn
StructuredBuffer<InstanceShading> Instances : register(t4);

// Bindless mesh geometry: two raw buffers per mesh (vertices then indices), so a
// reflection hit can read the triangle it actually landed on.
ByteAddressBuffer MeshBuffers[] : register(t0, space1);

RWTexture2D<float>  ShadowMask  : register(u0);
RWTexture2D<float4> Reflections : register(u1);

cbuffer RayConstants : register(b0) {
    float4x4 invViewProj;
    float4   cameraPos;          // xyz
    float4   lightDir;           // xyz normalised, w: surface bias
    float4   outputSize;         // xy: pixels, z: reflection max distance, w: reflection intensity
    float4   skyZenith;          // rgb
    float4   skyHorizon;         // rgb
    float4   aoParams;           // x: radius, y: sample count, z: strength, w: enabled
    float4   shadowParams;       // x: softness, y: sample count, z: shadows on, w: reflections on
};

// Cheap integer hash, used to decorrelate the ambient-occlusion sample
// directions per pixel. The seed deliberately does not include a frame counter:
// a stable pattern is far less distracting than one that boils frame to frame.
uint HashUint(uint value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

float NextRandom(inout uint state) {
    state = HashUint(state);
    return float(state & 0x00FFFFFFu) / float(0x01000000u);
}

// Cosine-weighted direction in the hemisphere around `normal`, which matches the
// cosine term in the occlusion integral so no extra weighting is needed.
float3 CosineHemisphere(float3 normal, float u1, float u2) {
    const float radius = sqrt(u1);
    const float phi    = 6.28318530718f * u2;

    const float3 helper  = abs(normal.x) > 0.9f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 tangent = normalize(cross(normal, helper));
    const float3 bitan   = cross(normal, tangent);

    return normalize(tangent * (radius * cos(phi)) +
                     bitan   * (radius * sin(phi)) +
                     normal  * sqrt(max(0.0f, 1.0f - u1)));
}

// Vogel (sunflower) disk. Uniform random sampling clumps badly at low sample
// counts, which is what made the penumbra grainy; this spreads the samples
// evenly and the per-pixel `rotation` decorrelates neighbours so whatever error
// is left is high frequency and averages out under a small blur.
float2 VogelDisk(uint sampleIndex, uint sampleCount, float rotation) {
    const float goldenAngle = 2.39996322973f;
    const float radius = sqrt((float(sampleIndex) + 0.5f) / float(sampleCount));
    const float theta  = float(sampleIndex) * goldenAngle + rotation;
    return float2(radius * cos(theta), radius * sin(theta));
}

float3 SampleSky(float3 direction) {
    const float t = saturate(direction.y * 0.5f + 0.5f);
    return lerp(skyHorizon.rgb, skyZenith.rgb, t);
}

// World position comes straight from the raster pass. Reconstructing it from
// the depth buffer was not viable: with a 0.1..5000 depth range the error is
// larger than any ray bias that still lets contact shadows land.
bool LoadWorldPosition(uint2 pixel, out float3 worldPosition) {
    const float4 stored = Positions.Load(int3(pixel, 0));
    worldPosition = stored.xyz;
    return stored.w > 0.5f; // w == 0 means the raster pass never wrote this pixel
}

// Interpolates the hit triangle's world-space normal from its three vertices.
float3 LoadHitNormal(uint vertexBufferIndex, uint indexBufferIndex, uint primitiveIndex,
                     float2 barycentrics, float3x4 objectToWorld) {
    const uint3 indices = MeshBuffers[indexBufferIndex].Load3(primitiveIndex * 12);

    const float3 n0 = asfloat(MeshBuffers[vertexBufferIndex].Load3(indices.x * kVertexStride + kVertexNormalOffset));
    const float3 n1 = asfloat(MeshBuffers[vertexBufferIndex].Load3(indices.y * kVertexStride + kVertexNormalOffset));
    const float3 n2 = asfloat(MeshBuffers[vertexBufferIndex].Load3(indices.z * kVertexStride + kVertexNormalOffset));

    // barycentrics give the weights of vertices 1 and 2; vertex 0 takes the rest.
    const float3 objectNormal = n0 * (1.0f - barycentrics.x - barycentrics.y) +
                                n1 * barycentrics.x +
                                n2 * barycentrics.y;

    // Rotate into world space. Uniform-ish scales only, which is what the
    // instance transforms here are.
    const float3x3 rotation = float3x3(objectToWorld[0].xyz, objectToWorld[1].xyz, objectToWorld[2].xyz);
    return normalize(mul(rotation, objectNormal));
}

bool TraceOccluded(float3 origin, float3 direction, float minDistance, float maxDistance) {
    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = direction;
    ray.TMin      = minDistance;
    ray.TMax      = maxDistance;

    // Any hit occludes, so the search can stop at the first one.
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(Scene, RAY_FLAG_NONE, 0xFF, ray);
    query.Proceed();
    return query.CommittedStatus() != COMMITTED_NOTHING;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 threadId : SV_DispatchThreadID) {
    const uint2 pixel = threadId.xy;
    if (pixel.x >= (uint)outputSize.x || pixel.y >= (uint)outputSize.y) {
        return;
    }

    float3 worldPosition;
    if (!LoadWorldPosition(pixel, worldPosition)) {
        // Sky: fully lit, nothing to reflect onto.
        ShadowMask[pixel]  = 1.0f;
        Reflections[pixel] = float4(0.0f, 0.0f, 0.0f, 1.0f); // alpha = ambient occlusion
        return;
    }

    const float4 packedNormal = GBuffer.Load(int3(pixel, 0));
    const float3 normal       = normalize(packedNormal.xyz * 2.0f - 1.0f);
    const float  roughness    = saturate(packedNormal.w);

    // ---- shadow ray --------------------------------------------------------
    // World position comes back from a depth buffer with a 0.1..5000 range, so
    // its absolute error grows with distance. A fixed offset is nowhere near
    // enough to escape the surface - scale the bias by the view distance and
    // start the ray there as well.
    const float surfaceBias = lightDir.w;

    const float3 toLight = normalize(-lightDir.xyz);
    const float  nDotL   = dot(normal, toLight);

    // Soften the terminator rather than cutting it off: a hard nDotL <= 0 test
    // leaves a visible stair-stepped line along curved surfaces.
    const float facing = smoothstep(0.0f, 0.25f, nDotL);

    float shadow = 1.0f;   // fully lit when shadow tracing is switched off
    if (shadowParams.z > 0.5f && facing > 0.0f) {
        shadow = 0.0f;
        const float3 origin = worldPosition + normal * surfaceBias;
        const uint  shadowSamples = (uint)max(1.0f, shadowParams.y);
        const float softness      = max(0.0f, shadowParams.x);

        if (shadowSamples == 1 || softness <= 0.0001f) {
            shadow = TraceOccluded(origin, toLight, surfaceBias, 1e4f) ? 0.0f : 1.0f;
        } else {
            // Basis around the light direction so samples spread across its disk.
            const float3 helper  = abs(toLight.x) > 0.9f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
            const float3 tangent = normalize(cross(toLight, helper));
            const float3 bitan   = cross(toLight, tangent);

            uint randomState = HashUint(pixel.x * 0x9E3779B9u ^ pixel.y * 0x85EBCA6Bu);
            const float rotation = NextRandom(randomState) * 6.28318530718f;

            uint lit = 0;
            for (uint sampleIndex = 0; sampleIndex < shadowSamples; ++sampleIndex) {
                const float2 disk = VogelDisk(sampleIndex, shadowSamples, rotation) * softness;
                const float3 direction = normalize(toLight + tangent * disk.x + bitan * disk.y);
                if (!TraceOccluded(origin, direction, surfaceBias, 1e4f)) {
                    ++lit;
                }
            }
            shadow = float(lit) / float(shadowSamples);
        }
        shadow *= facing;
    }
    ShadowMask[pixel] = shadow;

    // ---- reflection ray ----------------------------------------------------
    // Rough surfaces reflect little enough that tracing them is wasted work.
    float3 reflectedColor = float3(0.0f, 0.0f, 0.0f);
    const float reflectivity = shadowParams.w > 0.5f
        ? saturate((1.0f - roughness) * outputSize.w)
        : 0.0f;
    if (reflectivity > 0.01f) {
        const float3 viewDirection      = normalize(worldPosition - cameraPos.xyz);
        const float3 reflectedDirection = normalize(reflect(viewDirection, normal));

        RayDesc ray;
        ray.Origin    = worldPosition + normal * surfaceBias;
        ray.Direction = reflectedDirection;
        ray.TMin      = surfaceBias;
        ray.TMax      = outputSize.z;

        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
        query.TraceRayInline(Scene, RAY_FLAG_NONE, 0xFF, ray);
        query.Proceed();

        if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            const InstanceShading hit = Instances[query.CommittedInstanceID()];
            const float3 hitPosition = ray.Origin + reflectedDirection * query.CommittedRayT();
            const float3 hitNormal   = LoadHitNormal(hit.vertexBufferIndex, hit.indexBufferIndex,
                                                     query.CommittedPrimitiveIndex(),
                                                     query.CommittedTriangleBarycentrics(),
                                                     query.CommittedObjectToWorld3x4());

            // Shade the reflected surface the same way the raster pass shades a
            // primary hit, including a second ray so reflections carry shadows.
            const float  hitNDotL   = saturate(dot(hitNormal, toLight));
            const bool   hitShadowed = hitNDotL <= 0.0f ||
                                       TraceOccluded(hitPosition + hitNormal * surfaceBias, toLight,
                                                     surfaceBias, 1e4f);
            const float3 hitAmbient = hit.baseColor * 0.2f;
            const float3 hitDiffuse = hit.baseColor * hitNDotL * (hitShadowed ? 0.0f : 1.0f);
            reflectedColor = hitAmbient + hitDiffuse;
        } else {
            reflectedColor = SampleSky(reflectedDirection);
        }

        reflectedColor *= reflectivity;
    }

    // ---- ambient occlusion -------------------------------------------------
    // Short cosine-weighted rays around the normal. Packed into the alpha of the
    // reflection target so no third render target is needed; the composite
    // blurs it on the way in to hide the sampling noise.
    float ambientOcclusion = 1.0f;
    const uint aoSamples = (uint)max(1.0f, aoParams.y);
    if (aoParams.w > 0.5f && aoParams.z > 0.001f && aoParams.x > 0.0f) {
        uint randomState = HashUint(pixel.x * 73856093u ^ pixel.y * 19349663u);
        const float jitter = NextRandom(randomState);
        uint occluded = 0;
        for (uint sampleIndex = 0; sampleIndex < aoSamples; ++sampleIndex) {
            // Stratified in u1 so the samples cover the hemisphere evenly
            // instead of clumping the way pure random pairs do.
            const float u1 = frac((float(sampleIndex) + jitter) / float(aoSamples));
            const float u2 = NextRandom(randomState);
            const float3 sampleDirection = CosineHemisphere(normal, u1, u2);
            if (TraceOccluded(worldPosition + normal * surfaceBias, sampleDirection,
                              surfaceBias, aoParams.x)) {
                ++occluded;
            }
        }
        ambientOcclusion = 1.0f - (float(occluded) / float(aoSamples));
    }

    Reflections[pixel] = float4(reflectedColor, ambientOcclusion);
}
