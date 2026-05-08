#include "ShadowPass.h"
#include <d3dcompiler.h>
#include <stdexcept>
#include "Common.h"
#include "Mesh.h" 

void ShadowPass::Initialize(ID3D12Device* device) {
    CreateResources(device);
    CreatePipeline(device);
}

void ShadowPass::CreateResources(ID3D12Device* device) {
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {}; dsvHeapDesc.NumDescriptors = 1; dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; ThrowIfFailed(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_shadowDsvHeap)));
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {}; srvHeapDesc.NumDescriptors = 1; srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; ThrowIfFailed(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_shadowSrvHeap)));

    D3D12_RESOURCE_DESC texDesc = {}; texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; texDesc.Width = 2048; texDesc.Height = 2048; texDesc.DepthOrArraySize = 1; texDesc.MipLevels = 1; texDesc.Format = DXGI_FORMAT_R32_TYPELESS; texDesc.SampleDesc.Count = 1; texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE optClear = {}; optClear.Format = DXGI_FORMAT_D32_FLOAT; optClear.DepthStencil.Depth = 1.0f; optClear.DepthStencil.Stencil = 0;
    D3D12_HEAP_PROPERTIES heapProps = { D3D12_HEAP_TYPE_DEFAULT };
    ThrowIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &optClear, IID_PPV_ARGS(&m_shadowMap)));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {}; dsvDesc.Format = DXGI_FORMAT_D32_FLOAT; dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; device->CreateDepthStencilView(m_shadowMap.Get(), &dsvDesc, m_shadowDsvHeap->GetCPUDescriptorHandleForHeapStart());
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {}; srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srvDesc.Format = DXGI_FORMAT_R32_FLOAT; srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srvDesc.Texture2D.MipLevels = 1; device->CreateShaderResourceView(m_shadowMap.Get(), &srvDesc, m_shadowSrvHeap->GetCPUDescriptorHandleForHeapStart());
}

void ShadowPass::CreatePipeline(ID3D12Device* device) {
    ComPtr<ID3DBlob> vs, err;
    HRESULT hr = D3DCompileFromFile(L"Shaders/shadow.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", D3DCOMPILE_DEBUG, 0, &vs, &err); 
    if(FAILED(hr)) { std::string msg = err ? (char*)err->GetBufferPointer() : "Shadow VS Failed"; ThrowIfFailed(hr, msg); }

    D3D12_INPUT_ELEMENT_DESC layout[] = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 } };
    D3D12_ROOT_PARAMETER rp[1]; rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; rp[0].Descriptor.ShaderRegister = 0; rp[0].Descriptor.RegisterSpace = 0; rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_ROOT_SIGNATURE_DESC rsDesc = {}; rsDesc.NumParameters = 1; rsDesc.pParameters = rp; rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    
    ComPtr<ID3DBlob> sig; D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, nullptr); device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_shadowRootSignature));
    
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {}; pso.InputLayout = { layout, 1 }; pso.pRootSignature = m_shadowRootSignature.Get(); pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() }; pso.PS = { nullptr, 0 }; 
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; pso.RasterizerState.CullMode = D3D12_CULL_MODE_BACK; pso.RasterizerState.DepthBiasClamp = 0.0f; pso.RasterizerState.SlopeScaledDepthBias = 1.0f; pso.RasterizerState.DepthClipEnable = TRUE; 
    pso.DepthStencilState.DepthEnable = TRUE; pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; pso.DSVFormat = DXGI_FORMAT_D32_FLOAT; pso.BlendState.RenderTarget[0].RenderTargetWriteMask = 0; pso.SampleMask = UINT_MAX; pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; pso.NumRenderTargets = 0; pso.SampleDesc.Count = 1;
    ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_shadowPipelineState)));
}

void ShadowPass::Render(ID3D12GraphicsCommandList* commandList, const std::vector<GameObject>& gameObjects, UINT8* pCbvDataBegin, D3D12_GPU_VIRTUAL_ADDRESS cbAddress, UINT objSize, DirectX::XMMATRIX lightViewProj) {
    D3D12_RESOURCE_BARRIER shadowBarrier = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION };
    shadowBarrier.Transition.pResource = m_shadowMap.Get();
    shadowBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    shadowBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    commandList->ResourceBarrier(1, &shadowBarrier);

    D3D12_CPU_DESCRIPTOR_HANDLE shadowDsv = m_shadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
    commandList->OMSetRenderTargets(0, nullptr, FALSE, &shadowDsv);
    commandList->ClearDepthStencilView(shadowDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_VIEWPORT shadowVp = { 0.0f, 0.0f, 2048.0f, 2048.0f, 0.0f, 1.0f };
    D3D12_RECT shadowSc = { 0, 0, 2048, 2048 };
    commandList->RSSetViewports(1, &shadowVp);
    commandList->RSSetScissorRects(1, &shadowSc);

    commandList->SetPipelineState(m_shadowPipelineState.Get());
    commandList->SetGraphicsRootSignature(m_shadowRootSignature.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Mesh* lastBoundMesh = nullptr;

    for (int i = 0; i < gameObjects.size(); i++) {
        const GameObject& obj = gameObjects[i]; 
        if (!obj.enabled) continue;
        if (obj.type == ObjectType::Skybox || obj.type == ObjectType::PostProcessVolume || obj.type == ObjectType::Light) continue; 
        Mesh* meshToDraw = obj.asset ? obj.asset->mesh : nullptr; 
        if (!meshToDraw) continue;

        if (meshToDraw != lastBoundMesh) {
            D3D12_VERTEX_BUFFER_VIEW vbv = meshToDraw->GetVertexView();
            D3D12_INDEX_BUFFER_VIEW ibv = meshToDraw->GetIndexView();
            commandList->IASetVertexBuffers(0, 1, &vbv);
            commandList->IASetIndexBuffer(&ibv);
            lastBoundMesh = meshToDraw;
        }

        DirectX::XMMATRIX mScale = DirectX::XMMatrixScaling(obj.scale.x, obj.scale.y, obj.scale.z); 
        DirectX::XMMATRIX mRot = DirectX::XMMatrixRotationRollPitchYaw(obj.rotation.x, obj.rotation.y, obj.rotation.z); 
        DirectX::XMMATRIX mTrans = DirectX::XMMatrixTranslation(obj.position.x, obj.position.y, obj.position.z);
        DirectX::XMMATRIX mWorld = mScale * mRot * mTrans; 
        
        DirectX::XMMATRIX wvp = DirectX::XMMatrixTranspose(mWorld * lightViewProj); 
        
        ConstantBufferData cbData; cbData.wvpMatrix = wvp; 
        memcpy(pCbvDataBegin + (i * objSize), &cbData, sizeof(cbData)); 
        commandList->SetGraphicsRootConstantBufferView(0, cbAddress + (i * objSize));

        commandList->DrawIndexedInstanced(meshToDraw->GetIndexCount(), 1, 0, 0, 0);
    }

    shadowBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    shadowBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &shadowBarrier);
}
