struct ObjectData {
    matrix worldMatrix;
    float4 colorOverride;
    float3 center;
    float radius;
    uint indexCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint padding;
};

struct DrawIndexedArgs {
    uint IndexCountPerInstance;
    uint InstanceCount;
    uint StartIndexLocation;
    int BaseVertexLocation;
    uint StartInstanceLocation;
};

StructuredBuffer<ObjectData> InputObjects : register(t0);
RWStructuredBuffer<DrawIndexedArgs> OutputCommands : register(u0);
RWStructuredBuffer<uint> CommandCounter : register(u1);

cbuffer CullParams : register(b0) {
    matrix viewProj;
    float3 cameraPos;
    uint objectCount;
};

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= objectCount) return;

    ObjectData obj = InputObjects[id.x];

    // Extract the 6 Frustum Planes from the ViewProj Matrix
    float4 planes[6];
    planes[0] = viewProj[3] + viewProj[0]; // Left
    planes[1] = viewProj[3] - viewProj[0]; // Right
    planes[2] = viewProj[3] + viewProj[1]; // Bottom
    planes[3] = viewProj[3] - viewProj[1]; // Top
    planes[4] = viewProj[2];               // Near
    planes[5] = viewProj[3] - viewProj[2]; // Far

    for(int i = 0; i < 6; i++) {
        planes[i] /= length(planes[i].xyz);
    }

    bool visible = true;
    for(int p = 0; p < 6; p++) {
        if (dot(planes[p].xyz, obj.center) + planes[p].w < -(obj.radius * 1.5f)) {
            visible = false;
            break;
        }
    }

    if (visible) {
        uint index;
        InterlockedAdd(CommandCounter[0], 1, index);

        DrawIndexedArgs args;
        args.IndexCountPerInstance = obj.indexCount;
        args.InstanceCount = 1;
        args.StartIndexLocation = obj.startIndexLocation;
        args.BaseVertexLocation = obj.baseVertexLocation;
        args.StartInstanceLocation = id.x; 

        OutputCommands[index] = args;
    }
}