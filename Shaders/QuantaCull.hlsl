// ==========================================
// QUANTA CULL COMPUTE SHADER
// ==========================================

cbuffer CullConstants : register(b0) {
    float4x4 vp;
    float3 camPos;
    uint count;
    uint globalStartIndex;
};

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
    uint padding[5];
};

// The new 24-byte structure containing the Root Constant ID at the top!
struct IndirectCommand {
    uint globalInstanceID;       // Sent to register b2 via Command Signature
    uint indexCountPerInstance;
    uint instanceCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint startInstanceLocation;
};

StructuredBuffer<ObjectData> objectBuffer : register(t0);
RWStructuredBuffer<IndirectCommand> commandBuffer : register(u0);
RWStructuredBuffer<uint> counterBuffer : register(u1);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= count) return;
    
    // Combine thread ID with the global C++ offset to find the exact object
    uint globalId = id.x + globalStartIndex;
    ObjectData obj = objectBuffer[globalId];
    
    // Frustum culling is permanently disabled here while we ensure engine stability
    bool visible = true;
    
    if (visible) {
        uint commandIndex;
        InterlockedAdd(counterBuffer[0], 1, commandIndex);
        
        // Write the ID directly into the buffer so the Command Signature can grab it!
        commandBuffer[commandIndex].globalInstanceID = globalId; 
        
        // Standard draw arguments
        commandBuffer[commandIndex].indexCountPerInstance = obj.indexCount;
        commandBuffer[commandIndex].instanceCount = 1;
        commandBuffer[commandIndex].startIndexLocation = obj.startIndexLocation;
        commandBuffer[commandIndex].baseVertexLocation = obj.baseVertexLocation;
        commandBuffer[commandIndex].startInstanceLocation = 0; 
    }
}
