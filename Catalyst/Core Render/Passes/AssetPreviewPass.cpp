#include "AssetPreviewPass.h"
#include <directxmath.h>
#include "../Error Handler/Common.h"
#include "../Resources/Mesh.h"

using namespace DirectX;

void AssetPreviewPass::Initialize(ID3D12Device* device, ID3D12DescriptorHeap* uiHeap) {
}

void AssetPreviewPass::Render(ID3D12GraphicsCommandList* commandList, Asset* asset, ID3D12RootSignature* rs, ID3D12PipelineState* pso, ID3D12Resource* cb, UINT8* cbBegin, D3D12_CPU_DESCRIPTOR_HANDLE dsv, Texture* defaultTex, float yaw, float pitch, float dist) {
    if (!asset || !asset->mesh) return;

    commandList->SetPipelineState(pso);
    commandList->SetGraphicsRootSignature(rs);

    XMMATRIX mRotation = XMMatrixRotationRollPitchYaw(pitch, yaw, 0);
    XMVECTOR vPos = XMVectorSet(0, 0, -dist, 1);
    vPos = XMVector3TransformCoord(vPos, mRotation);
    XMVECTOR vTarget = XMVectorSet(0, 0, 0, 1);
    XMVECTOR vUp = XMVectorSet(0, 1, 0, 0);
    
    XMMATRIX mView = XMMatrixLookAtLH(vPos, vTarget, vUp);
    XMMATRIX mProj = XMMatrixPerspectiveFovLH(0.8f, 1.0f, 0.1f, 100.0f);
    XMMATRIX mWVP = XMMatrixTranspose(XMMatrixIdentity() * mView * mProj);

    ConstantBufferData data = {};
    data.wvpMatrix = mWVP;
    data.worldMatrix = XMMatrixIdentity();
    data.colorOverride = { 1, 1, 1, 1 };
    memcpy(cbBegin, &data, sizeof(ConstantBufferData));

    commandList->SetGraphicsRootConstantBufferView(0, cb->GetGPUVirtualAddress());

    Mesh* mesh = asset->mesh;
    D3D12_VERTEX_BUFFER_VIEW vbv = mesh->GetVertexView();
    D3D12_INDEX_BUFFER_VIEW ibv = mesh->GetIndexView();

    commandList->IASetVertexBuffers(0, 1, &vbv);
    commandList->IASetIndexBuffer(&ibv);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    commandList->DrawIndexedInstanced(mesh->GetIndexCount(), 1, 0, 0, 0);
}