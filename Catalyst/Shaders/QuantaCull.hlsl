// ==========================================
// QUANTA CULL COMPUTE SHADER
// ==========================================
// One thread per cluster per instance. Each thread decides on its own whether
// its slice of the mesh is worth drawing this frame, and only the survivors
// turn into indirect draw arguments. A dense model therefore costs what is
// visible, not what it contains.

cbuffer CullConstants : register(b0) {
    float4x4 vp;
    float3 camPos;
    uint count;                // instances in this batch
    uint globalStartIndex;     // where this batch starts in the object buffer
    uint clusterCount;         // clusters in the mesh being drawn, 0 = no LOD data
    float errorScale;          // pixels per unit of object-space error at unit distance
    float errorThreshold;      // how many pixels of error to accept
    uint cullFlags;            // 1 = frustum, 2 = backface cone, 4 = LOD
};

static const uint CULL_FRUSTUM = 1;
static const uint CULL_CONE    = 2;
static const uint CULL_LOD     = 4;

struct ObjectData {
    float4x4 worldMatrix;
    float4 colorOverride;
    float3 center;
    float radius;
    uint indexCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint albedoIndex;
    uint normalIndex;
    uint metallicIndex;
    uint roughnessIndex;
    float roughnessScale;
    uint debugMeshletSize;
    uint padding[3];
};

// Mirrors MeshCluster in Mesh.h. 96 bytes.
struct MeshCluster {
    uint indexOffset;
    uint indexCount;

    float3 center;
    float radius;

    float3 coneAxis;
    float coneCutoff;

    float3 groupCenter;
    float groupRadius;
    float groupError;

    float3 parentCenter;
    float parentRadius;
    float parentError;

    uint level;
    uint3 padding;
};

struct IndirectCommand {
    uint globalInstanceID;
    uint indexCountPerInstance;
    uint instanceCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint startInstanceLocation;
};

StructuredBuffer<ObjectData> objectBuffer : register(t0);
StructuredBuffer<MeshCluster> clusterBuffer : register(t1);
RWStructuredBuffer<IndirectCommand> commandBuffer : register(u0);
RWStructuredBuffer<uint> counterBuffer : register(u1);

// The largest absolute scale any axis of the world matrix applies. Bounds are
// stored in object space, so they have to be grown by this before they can be
// compared against anything in world space.
float MaxAxisScale(float4x4 world) {
    float3 x = float3(world[0][0], world[1][0], world[2][0]);
    float3 y = float3(world[0][1], world[1][1], world[2][1]);
    float3 z = float3(world[0][2], world[1][2], world[2][2]);
    return sqrt(max(dot(x, x), max(dot(y, y), dot(z, z))));
}

// Screen-space error of a sphere carrying `error` units of deviation. Dividing
// by distance is what makes a distant cluster acceptable and a near one not.
float ProjectError(float error, float3 worldCenter, float worldRadius, float scale) {
    // A root cluster carries an infinite parent error: nothing is ever coarse
    // enough to replace it, so it must never compare below the threshold.
    float result = 3.0e38f;
    if (error < 3.0e38f) {
        float distance = max(length(worldCenter - camPos) - worldRadius, 0.0001f);
        result = error * scale * errorScale / distance;
    }
    return result;
}

// Six-plane test against the sphere, planes pulled straight out of the combined
// view-projection matrix.
bool SphereInFrustum(float3 center, float radius) {
    // vp arrives as transpose(view * proj), and HLSL's column-major packing
    // transposes it back, so in here vp is the row-vector matrix M for which
    // mul(v, vp) is clip space - the same one the vertex shader uses.
    //
    // clip.x is v dotted with COLUMN 0 of M plus M[3][0], and clip.w with
    // column 3. The planes therefore come from the columns. Building them from
    // the rows is the column-vector convention, and produces a frustum aimed
    // somewhere else entirely - which rejects most of what is actually on
    // screen while letting the odd cluster through.
    float4 column0 = float4(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
    float4 column1 = float4(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
    float4 column2 = float4(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
    float4 column3 = float4(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);

    float4 planes[6];
    planes[0] = column3 + column0;   // left:   clip.x + clip.w >= 0
    planes[1] = column3 - column0;   // right:  clip.w - clip.x >= 0
    planes[2] = column3 + column1;   // bottom
    planes[3] = column3 - column1;   // top
    planes[4] = column2;             // near:   clip.z >= 0 in D3D
    planes[5] = column3 - column2;   // far

    bool inside = true;
    [unroll]
    for (int i = 0; i < 6; ++i) {
        float planeLength = length(planes[i].xyz);
        // A degenerate plane says nothing, so it is left out of the verdict
        // rather than allowed to reject the sphere.
        if (planeLength >= 1e-8f &&
            dot(planes[i].xyz, center) + planes[i].w < -radius * planeLength) {
            inside = false;
        }
    }
    return inside;
}

// The command signature carries a single root constant, so the object index and
// the cluster index share it: object in the low 16 bits, cluster in the high 16.
// Only the debug view reads the cluster half, so a very dense mesh wrapping past
// 65535 clusters just repeats colours - it never mis-addresses the object.
uint PackDrawId(uint objectIndex, uint clusterIndex) {
    return (objectIndex & 0xFFFFu) | (clusterIndex << 16);
}

void AppendDraw(uint globalId, uint indexCount, uint startIndex, int baseVertex) {
    uint commandIndex;
    InterlockedAdd(counterBuffer[0], 1, commandIndex);

    commandBuffer[commandIndex].globalInstanceID = globalId;
    commandBuffer[commandIndex].indexCountPerInstance = indexCount;
    commandBuffer[commandIndex].instanceCount = 1;
    commandBuffer[commandIndex].startIndexLocation = startIndex;
    commandBuffer[commandIndex].baseVertexLocation = baseVertex;
    commandBuffer[commandIndex].startInstanceLocation = 0;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    // Without cluster data there is nothing to select between, so each instance
    // becomes a single draw of the whole mesh, frustum-tested as a unit.
    if (clusterCount == 0) {
        if (id.x >= count) {
            return;
        }
        uint globalId = id.x + globalStartIndex;
        ObjectData obj = objectBuffer[globalId];

        if (cullFlags & CULL_FRUSTUM) {
            float scale = MaxAxisScale(obj.worldMatrix);
            if (!SphereInFrustum(obj.center, obj.radius * scale)) {
                return;
            }
        }
        AppendDraw(PackDrawId(globalId, 0), obj.indexCount, obj.startIndexLocation, obj.baseVertexLocation);
        return;
    }

    uint instanceIndex = id.x / clusterCount;
    uint clusterIndex = id.x % clusterCount;
    if (instanceIndex >= count) {
        return;
    }

    uint globalId = instanceIndex + globalStartIndex;
    ObjectData obj = objectBuffer[globalId];
    MeshCluster cluster = clusterBuffer[clusterIndex];

    float scale = MaxAxisScale(obj.worldMatrix);

    // Level of detail first: it is the cheapest way to reject the overwhelming
    // majority of clusters, since only one level of the hierarchy survives.
    if (cullFlags & CULL_LOD) {
        float3 groupCenter = mul(float4(cluster.groupCenter, 1.0f), obj.worldMatrix).xyz;
        float3 parentCenter = mul(float4(cluster.parentCenter, 1.0f), obj.worldMatrix).xyz;

        float ownError = ProjectError(cluster.groupError, groupCenter, cluster.groupRadius * scale, scale);
        float parentError = ProjectError(cluster.parentError, parentCenter, cluster.parentRadius * scale, scale);

        // Draw this cluster only where it is fine enough to be acceptable and
        // its parent is not. Exactly one level of the DAG passes both tests, so
        // the surface is covered once and only once.
        if (!(ownError <= errorThreshold && parentError > errorThreshold)) {
            return;
        }
    }

    float3 worldCenter = mul(float4(cluster.center, 1.0f), obj.worldMatrix).xyz;
    float worldRadius = cluster.radius * scale;

    if (cullFlags & CULL_FRUSTUM) {
        if (!SphereInFrustum(worldCenter, worldRadius)) {
            return;
        }
    }

    // A cutoff of 1 means the cluster's normals fan out too widely for the cone
    // to say anything, so the test is skipped rather than guessed at.
    if ((cullFlags & CULL_CONE) && cluster.coneCutoff < 1.0f) {
        float3 axis = normalize(mul(float4(cluster.coneAxis, 0.0f), obj.worldMatrix).xyz);
        float3 toCluster = worldCenter - camPos;
        float distance = length(toCluster);
        if (distance > worldRadius) {
            // Grow the cutoff by the angle the sphere itself subtends, so a
            // cluster is only rejected when every triangle in it faces away.
            if (dot(axis, toCluster / distance) >= cluster.coneCutoff + worldRadius / distance) {
                return;
            }
        }
    }

    AppendDraw(PackDrawId(globalId, clusterIndex), cluster.indexCount, cluster.indexOffset, obj.baseVertexLocation);
}
