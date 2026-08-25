#pragma once
#include <directxmath.h>
#include <cstdint>

// Used by the Shadow & Preview Passes
struct ConstantBufferData {
    DirectX::XMMATRIX wvpMatrix;
    DirectX::XMMATRIX worldMatrix;
    DirectX::XMMATRIX lightSpaceMatrix; 
    DirectX::XMFLOAT4 colorOverride;
    DirectX::XMFLOAT3 lightDir;
    float lightIntensity;
    DirectX::XMFLOAT3 cameraPos;
    float visualizationMode;
    float metallicFactor;
    float roughnessFactor;
    float enableReflections;
    float padding; 
};

// --- QUANTA MESH DATA ---

// The massive array sent to the GPU Compute Shader
struct ObjectData {
    DirectX::XMMATRIX worldMatrix;
    DirectX::XMFLOAT4 colorOverride;
    DirectX::XMFLOAT3 center;
    float radius;
    uint32_t indexCount;
    uint32_t startIndexLocation;
    int32_t baseVertexLocation;
    uint32_t albedoIndex;
    uint32_t normalIndex;
    uint32_t metallicIndex;
    uint32_t roughnessIndex;
    float roughnessScale;   // multiplies the sampled roughness; 1 = fully rough
    uint32_t debugMeshletSize;  // triangles per meshlet, or 0 for normal shading
    uint32_t padding[3];
};

// Material-specific data (The "Unreal-style" property block)
struct MaterialConstants {
    DirectX::XMFLOAT4 materialColor;
    float metallic;
    float roughness;
    uint32_t albedoIndex;
    uint32_t normalIndex;
    uint32_t metallicIndex;
    uint32_t roughnessIndex;
    DirectX::XMFLOAT4 padding;
};

// Global environment data for the Quanta graphics pass
struct GlobalBufferData {
    DirectX::XMMATRIX viewProj;
    DirectX::XMMATRIX lightSpaceMatrix;
    DirectX::XMFLOAT3 lightDir;
    float lightIntensity;
    DirectX::XMFLOAT3 cameraPos;
    float padding;
};

// The exact structure DirectX 12 expects inside an ExecuteIndirect buffer
struct DrawIndexedArgs {
    uint32_t IndexCountPerInstance;
    uint32_t InstanceCount;
    uint32_t StartIndexLocation;
    int32_t BaseVertexLocation;
    uint32_t StartInstanceLocation;
};
