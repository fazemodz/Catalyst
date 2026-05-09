#include "QuantaMeshPass.h"
#include <d3dcompiler.h>
#include <map>
#include "Common.h"
#include "Material.h"
#include "../ShaderCompiler.h"

namespace {
Texture* ResolveObjectTexture(const GameObject& obj, Texture* GameObject::* overrideSlot,
                              Texture* Asset::* assetSlot, Texture* Material::* materialSlot,
                              Texture* fallback) {
    if (obj.*overrideSlot) {
        return obj.*overrideSlot;
    }

    if (materialSlot && obj.assignedMaterial && obj.assignedMaterial->*materialSlot) {
        return obj.assignedMaterial->*materialSlot;
    }

    if (obj.asset && obj.asset->*assetSlot) {
        return obj.asset->*assetSlot;
    }

    return fallback;
}
}

void QuantaMeshPass::Initialize(ID3D12Device* device)
{
    CreatePipelines(device);
    CreateComputeBuffers(device);
}

void QuantaMeshPass::CreateComputeBuffers(ID3D12Device* device)
{
    D3D12_HEAP_PROPERTIES uploadHeap = {}; uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_HEAP_PROPERTIES defaultHeap = {}; defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bufferDesc.Width = sizeof(ObjectData) * 10000;
    bufferDesc.Height = 1; bufferDesc.DepthOrArraySize = 1; bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN; bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ThrowIfFailed(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_objectDataBuffer)));
    m_objectDataBuffer->Map(0, nullptr, (void**)&m_mappedObjectData);

    bufferDesc.Width = (sizeof(GlobalBufferData) + 255) & ~255;
    ThrowIfFailed(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_globalCB)));
    m_globalCB->Map(0, nullptr, (void**)&m_mappedGlobalCB);

    bufferDesc.Width = (sizeof(MaterialConstants) + 255) & ~255;
    ThrowIfFailed(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_materialCB)));
    m_materialCB->Map(0, nullptr, (void**)&m_mappedMaterialCB);

    bufferDesc.Width = 24 * 10000;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_commandBuffer)));

    bufferDesc.Width = sizeof(uint32_t);
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_counterBuffer)));

    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    ThrowIfFailed(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_zeroBuffer)));

    uint32_t* zero; m_zeroBuffer->Map(0, nullptr, (void**)&zero); *zero = 0;
}

void QuantaMeshPass::CreatePipelines(ID3D12Device* device) {
    ComPtr<ID3DBlob> err; ComPtr<ID3DBlob> rsErr; HRESULT hr;
    UINT standardFlags = D3DCOMPILE_DEBUG;

    D3D12_DEPTH_STENCIL_DESC defaultDepthStencil = {}; defaultDepthStencil.DepthEnable = TRUE; defaultDepthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; defaultDepthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS; defaultDepthStencil.StencilEnable = FALSE; defaultDepthStencil.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK; defaultDepthStencil.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    const D3D12_DEPTH_STENCILOP_DESC defaultStencilOp = { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
    defaultDepthStencil.FrontFace = defaultStencilOp; defaultDepthStencil.BackFace = defaultStencilOp;

    D3D12_RENDER_TARGET_BLEND_DESC defaultRTBlend = {}; defaultRTBlend.BlendEnable = FALSE; defaultRTBlend.LogicOpEnable = FALSE; defaultRTBlend.SrcBlend = D3D12_BLEND_ONE; defaultRTBlend.DestBlend = D3D12_BLEND_ZERO; defaultRTBlend.BlendOp = D3D12_BLEND_OP_ADD; defaultRTBlend.SrcBlendAlpha = D3D12_BLEND_ONE; defaultRTBlend.DestBlendAlpha = D3D12_BLEND_ZERO; defaultRTBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD; defaultRTBlend.LogicOp = D3D12_LOGIC_OP_NOOP; defaultRTBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_BLEND_DESC defaultBlend = {}; defaultBlend.AlphaToCoverageEnable = FALSE; defaultBlend.IndependentBlendEnable = FALSE;
    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) defaultBlend.RenderTarget[i] = defaultRTBlend;

    D3D12_RASTERIZER_DESC defaultRasterizer = {}; defaultRasterizer.FillMode = D3D12_FILL_MODE_SOLID; defaultRasterizer.CullMode = D3D12_CULL_MODE_BACK; defaultRasterizer.FrontCounterClockwise = FALSE; defaultRasterizer.DepthBias = D3D12_DEFAULT_DEPTH_BIAS; defaultRasterizer.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP; defaultRasterizer.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS; defaultRasterizer.DepthClipEnable = TRUE; defaultRasterizer.MultisampleEnable = FALSE; defaultRasterizer.AntialiasedLineEnable = FALSE; defaultRasterizer.ForcedSampleCount = 0; defaultRasterizer.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // ==========================================
    // 1. COMPUTE PIPELINE
    // ==========================================
    ComPtr<ID3DBlob> cs; hr = CatalystRender::CompileShaderFromFile(L"Shaders/QuantaCull.hlsl", nullptr, nullptr, "CSMain", "cs_5_1", standardFlags, 0, &cs, &err);
    if(FAILED(hr)) ThrowIfFailed(hr, err ? (char*)err->GetBufferPointer() : "Compute Cull Failed");

    D3D12_ROOT_PARAMETER crp[4] = {};
    crp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; crp[0].Constants.ShaderRegister = 0; crp[0].Constants.RegisterSpace = 0; crp[0].Constants.Num32BitValues = 21; crp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    crp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; crp[1].Descriptor.ShaderRegister = 0; crp[1].Descriptor.RegisterSpace = 0; crp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    crp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; crp[2].Descriptor.ShaderRegister = 0; crp[2].Descriptor.RegisterSpace = 0; crp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    crp[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; crp[3].Descriptor.ShaderRegister = 1; crp[3].Descriptor.RegisterSpace = 0; crp[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC crsDesc = {}; crsDesc.NumParameters = 4; crsDesc.pParameters = crp;
    ComPtr<ID3DBlob> csig; hr = D3D12SerializeRootSignature(&crsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &csig, &rsErr);
    if(FAILED(hr)) ThrowIfFailed(hr, rsErr ? (char*)rsErr->GetBufferPointer() : "Compute RS Serialize Failed");
    hr = device->CreateRootSignature(0, csig->GetBufferPointer(), csig->GetBufferSize(), IID_PPV_ARGS(&m_computeRootSignature));
    ThrowIfFailed(hr, "Compute root signature creation failed");
    
    D3D12_COMPUTE_PIPELINE_STATE_DESC cpso = {}; cpso.pRootSignature = m_computeRootSignature.Get(); cpso.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
    hr = device->CreateComputePipelineState(&cpso, IID_PPV_ARGS(&m_computePipelineState));
    ThrowIfFailed(hr, "Compute pipeline creation failed");

    // ==========================================
    // 2. QUANTA PIPELINE
    // ==========================================
    ComPtr<ID3DBlob> vs, ps;
    hr = CatalystRender::CompileShaderFromFile(L"Shaders/QuantaMesh.hlsl", nullptr, nullptr, "VSMain", "vs_5_1", standardFlags, 0, &vs, &err);
    if(FAILED(hr)) ThrowIfFailed(hr, err ? (char*)err->GetBufferPointer() : "Quanta VS Failed");
    hr = CatalystRender::CompileShaderFromFile(L"Shaders/QuantaMesh.hlsl", nullptr, nullptr, "PSMain", "ps_5_1", standardFlags, 0, &ps, &err);
    if(FAILED(hr)) ThrowIfFailed(hr, err ? (char*)err->GetBufferPointer() : "Quanta PS Failed");

    D3D12_INPUT_ELEMENT_DESC layout[] = { 
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, 
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, 
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, 
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, 
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 } 
    };
    
    D3D12_ROOT_PARAMETER rpQ[6] = {}; 
    rpQ[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; rpQ[0].Descriptor.ShaderRegister = 0; rpQ[0].Descriptor.RegisterSpace = 0; rpQ[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rpQ[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; rpQ[1].Descriptor.ShaderRegister = 1; rpQ[1].Descriptor.RegisterSpace = 0; rpQ[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rpQ[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; rpQ[2].Descriptor.ShaderRegister = 0; rpQ[2].Descriptor.RegisterSpace = 0; rpQ[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    
    D3D12_DESCRIPTOR_RANGE bindlessRange = {}; bindlessRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; bindlessRange.NumDescriptors = 1024; bindlessRange.BaseShaderRegister = 0; bindlessRange.RegisterSpace = 1; bindlessRange.OffsetInDescriptorsFromTableStart = 0;
    rpQ[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rpQ[3].DescriptorTable.NumDescriptorRanges = 1; rpQ[3].DescriptorTable.pDescriptorRanges = &bindlessRange; rpQ[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_DESCRIPTOR_RANGE shadowRange = {}; shadowRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; shadowRange.NumDescriptors = 1; shadowRange.BaseShaderRegister = 7; shadowRange.RegisterSpace = 0; shadowRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    rpQ[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rpQ[4].DescriptorTable.NumDescriptorRanges = 1; rpQ[4].DescriptorTable.pDescriptorRanges = &shadowRange; rpQ[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    
    // ⚡ INJECTING THE PER-OBJECT ID HERE
    rpQ[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rpQ[5].Constants.ShaderRegister = 2; // b2
    rpQ[5].Constants.RegisterSpace = 0;
    rpQ[5].Constants.Num32BitValues = 1;
    rpQ[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_STATIC_SAMPLER_DESC samplers[2] = {}; 
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP; samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP; samplers[0].ShaderRegister = 0; samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR; samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER; samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER; samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER; samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE; samplers[1].ShaderRegister = 1; samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDescQ = {}; rsDescQ.NumParameters = 6; rsDescQ.pParameters = rpQ; rsDescQ.NumStaticSamplers = 2; rsDescQ.pStaticSamplers = samplers; rsDescQ.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> sigQ; hr = D3D12SerializeRootSignature(&rsDescQ, D3D_ROOT_SIGNATURE_VERSION_1, &sigQ, &rsErr); 
    if(FAILED(hr)) ThrowIfFailed(hr, rsErr ? (char*)rsErr->GetBufferPointer() : "Quanta RS Serialize Failed");
    hr = device->CreateRootSignature(0, sigQ->GetBufferPointer(), sigQ->GetBufferSize(), IID_PPV_ARGS(&m_quantaRootSignature));
    ThrowIfFailed(hr, "Quanta root signature creation failed");
    
    D3D12_INDIRECT_ARGUMENT_DESC argDesc[2] = {}; 
    argDesc[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
    argDesc[0].Constant.RootParameterIndex = 5;
    argDesc[0].Constant.DestOffsetIn32BitValues = 0;
    argDesc[0].Constant.Num32BitValuesToSet = 1;
    argDesc[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    
    D3D12_COMMAND_SIGNATURE_DESC csDesc = {}; 
    csDesc.NumArgumentDescs = 2; 
    csDesc.pArgumentDescs = argDesc; 
    csDesc.ByteStride = 24; 
    
    // Pass the Quanta Root Sig to bind the constant!
    ThrowIfFailed(device->CreateCommandSignature(&csDesc, m_quantaRootSignature.Get(), IID_PPV_ARGS(&m_commandSignature)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoQ = {}; 
    psoQ.InputLayout = { layout, 5 }; psoQ.pRootSignature = m_quantaRootSignature.Get(); psoQ.VS = { vs->GetBufferPointer(), vs->GetBufferSize() }; psoQ.PS = { ps->GetBufferPointer(), ps->GetBufferSize() }; 
    
    D3D12_BLEND_DESC quantaBlend = defaultBlend; quantaBlend.RenderTarget[0].BlendEnable = FALSE; quantaBlend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoQ.BlendState = quantaBlend; psoQ.RasterizerState = defaultRasterizer; psoQ.DepthStencilState = defaultDepthStencil;
    psoQ.DSVFormat = DXGI_FORMAT_D32_FLOAT; psoQ.SampleMask = UINT_MAX; psoQ.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; 
    psoQ.NumRenderTargets = 1; psoQ.SampleDesc.Count = 1; psoQ.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; 
    hr = device->CreateGraphicsPipelineState(&psoQ, IID_PPV_ARGS(&m_quantaPipelineState));
    ThrowIfFailed(hr, "Quanta graphics pipeline creation failed");
}

void QuantaMeshPass::Render(ID3D12GraphicsCommandList* commandList, ID3D12Device* device, const std::vector<GameObject>& gameObjects, const Camera& camera, int width, int height, DirectX::XMMATRIX lightSpaceMatrix, DirectX::XMFLOAT3 activeLightDir, float activeIntensity, Texture* activeSkybox, ID3D12DescriptorHeap* bindlessHeap, UINT srvDescriptorSize, UINT& frameHeapOffset, Texture* texWhite, Texture* texNormal, Texture* texBlack, ID3D12DescriptorHeap* shadowSrvHeap, Mesh* defaultSphereMesh)
{
    using namespace DirectX;
    XMMATRIX mView = camera.GetViewMatrix(); XMMATRIX mProj = camera.GetProjectionMatrix();

    GlobalBufferData globalData = {}; globalData.viewProj = XMMatrixTranspose(mView * mProj); globalData.lightSpaceMatrix = lightSpaceMatrix; globalData.lightDir = activeLightDir; globalData.lightIntensity = activeIntensity; globalData.cameraPos = camera.GetPosition();
    memcpy(m_mappedGlobalCB, &globalData, sizeof(GlobalBufferData));

    MaterialConstants mat = {}; mat.materialColor = XMFLOAT4(1, 1, 1, 1); mat.metallic = 0.5f; mat.roughness = 0.5f; mat.albedoIndex = texWhite->GetBindlessIndex(); mat.normalIndex = texNormal->GetBindlessIndex(); mat.metallicIndex = texBlack->GetBindlessIndex(); mat.roughnessIndex = texBlack->GetBindlessIndex();
    memcpy(m_mappedMaterialCB, &mat, sizeof(MaterialConstants));

    std::map<Mesh*, std::vector<const GameObject*>> meshGroups;
    for (const auto& obj : gameObjects) {
        if (!obj.enabled) continue;
        if (obj.type == ObjectType::Skybox || obj.type == ObjectType::PostProcessVolume) continue;
        Mesh* mesh = (obj.asset && obj.asset->mesh) ? obj.asset->mesh : defaultSphereMesh;
        if (mesh) meshGroups[mesh].push_back(&obj);
    }

    D3D12_VIEWPORT vp = { 0,0,(float)width,(float)height,0,1 }; D3D12_RECT sc = { 0,0,width,height };
    commandList->RSSetViewports(1, &vp); commandList->RSSetScissorRects(1, &sc);

    uint32_t globalObjectIndex = 0;

    for (auto& pair : meshGroups) {
        Mesh* mesh = pair.first; auto& instances = pair.second;

        for (size_t i = 0; i < instances.size(); i++) {
            const GameObject* obj = instances[i];
            uint32_t index = globalObjectIndex + (uint32_t)i;
            Texture* albedoTexture = ResolveObjectTexture(*obj, &GameObject::overrideAlbedo, &Asset::albedoMap, &Material::albedoTexture, texWhite);
            Texture* normalTexture = ResolveObjectTexture(*obj, &GameObject::overrideNormal, &Asset::normalMap, &Material::normalTexture, texNormal);
            Texture* metallicTexture = ResolveObjectTexture(*obj, &GameObject::overrideMetallic, &Asset::metallicMap, nullptr, texBlack);
            Texture* roughnessTexture = ResolveObjectTexture(*obj, &GameObject::overrideRoughness, &Asset::roughnessMap, &Material::roughnessTexture, texWhite);
            XMFLOAT4 resolvedColor = obj->color;
            if (obj->assignedMaterial) {
                const XMFLOAT4& materialColor = obj->assignedMaterial->baseColor;
                resolvedColor = {
                    resolvedColor.x * materialColor.x,
                    resolvedColor.y * materialColor.y,
                    resolvedColor.z * materialColor.z,
                    resolvedColor.w * materialColor.w
                };
            }

            XMMATRIX scale = XMMatrixScaling(obj->scale.x, obj->scale.y, obj->scale.z); XMMATRIX rot = XMMatrixRotationRollPitchYaw(obj->rotation.x, obj->rotation.y, obj->rotation.z); XMMATRIX trans = XMMatrixTranslation(obj->position.x, obj->position.y, obj->position.z);
            m_mappedObjectData[index].worldMatrix = XMMatrixTranspose(scale * rot * trans);
            m_mappedObjectData[index].colorOverride = resolvedColor;
            m_mappedObjectData[index].center = obj->position;
            m_mappedObjectData[index].radius = max(obj->scale.x, max(obj->scale.y, obj->scale.z));
            m_mappedObjectData[index].indexCount = mesh->GetIndexCount();
            m_mappedObjectData[index].startIndexLocation = 0;
            m_mappedObjectData[index].baseVertexLocation = 0;
            m_mappedObjectData[index].albedoIndex = albedoTexture ? albedoTexture->GetBindlessIndex() : texWhite->GetBindlessIndex();
            m_mappedObjectData[index].normalIndex = normalTexture ? normalTexture->GetBindlessIndex() : texNormal->GetBindlessIndex();
            m_mappedObjectData[index].metallicIndex = metallicTexture ? metallicTexture->GetBindlessIndex() : texBlack->GetBindlessIndex();
            m_mappedObjectData[index].roughnessIndex = roughnessTexture ? roughnessTexture->GetBindlessIndex() : texWhite->GetBindlessIndex();
            for (uint32_t& pad : m_mappedObjectData[index].padding) {
                pad = 0;
            }
        }

        uint32_t instanceCount = (uint32_t)instances.size();

        const D3D12_RESOURCE_STATES indirectState = m_indirectBuffersInitialized ? D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT : D3D12_RESOURCE_STATE_COMMON;

        D3D12_RESOURCE_BARRIER barriers[2] = {};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barriers[0].Transition.pResource = m_counterBuffer.Get(); barriers[0].Transition.StateBefore = indirectState; barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST; barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barriers[1].Transition.pResource = m_commandBuffer.Get(); barriers[1].Transition.StateBefore = indirectState; barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(2, barriers);

        commandList->CopyBufferRegion(m_counterBuffer.Get(), 0, m_zeroBuffer.Get(), 0, sizeof(uint32_t));

        D3D12_RESOURCE_BARRIER uavBarrier = {}; uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; uavBarrier.Transition.pResource = m_counterBuffer.Get(); uavBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; uavBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; uavBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &uavBarrier);

        commandList->SetPipelineState(m_computePipelineState.Get());
        commandList->SetComputeRootSignature(m_computeRootSignature.Get());

        struct CullConstants { XMMATRIX vp; XMFLOAT3 camPos; uint32_t count; uint32_t globalStartIndex; };
        CullConstants cull = {}; cull.vp = XMMatrixTranspose(mView * mProj); cull.camPos = camera.GetPosition(); cull.count = instanceCount; cull.globalStartIndex = globalObjectIndex; 

        commandList->SetComputeRoot32BitConstants(0, 21, &cull, 0);
        commandList->SetComputeRootShaderResourceView(1, m_objectDataBuffer->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(2, m_commandBuffer->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(3, m_counterBuffer->GetGPUVirtualAddress());
        commandList->Dispatch((instanceCount + 63) / 64, 1, 1);

        D3D12_RESOURCE_BARRIER endBarriers[2] = {};
        endBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; endBarriers[0].Transition.pResource = m_counterBuffer.Get(); endBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; endBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT; endBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        endBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; endBarriers[1].Transition.pResource = m_commandBuffer.Get(); endBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; endBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT; endBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(2, endBarriers);
        m_indirectBuffersInitialized = true;

        commandList->SetPipelineState(m_quantaPipelineState.Get());
        commandList->SetGraphicsRootSignature(m_quantaRootSignature.Get());
        commandList->SetGraphicsRootConstantBufferView(0, m_globalCB->GetGPUVirtualAddress());
        commandList->SetGraphicsRootConstantBufferView(1, m_materialCB->GetGPUVirtualAddress());
        commandList->SetGraphicsRootShaderResourceView(2, m_objectDataBuffer->GetGPUVirtualAddress());
        commandList->SetGraphicsRootDescriptorTable(3, bindlessHeap->GetGPUDescriptorHandleForHeapStart());

        if (shadowSrvHeap) commandList->SetGraphicsRootDescriptorTable(4, shadowSrvHeap->GetGPUDescriptorHandleForHeapStart());

        D3D12_VERTEX_BUFFER_VIEW vbv = mesh->GetVertexView(); D3D12_INDEX_BUFFER_VIEW ibv = mesh->GetIndexView();
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->IASetVertexBuffers(0, 1, &vbv); commandList->IASetIndexBuffer(&ibv);

        commandList->ExecuteIndirect(m_commandSignature.Get(), instanceCount, m_commandBuffer.Get(), 0, m_counterBuffer.Get(), 0);
        globalObjectIndex += instanceCount;
    }

    (void)activeSkybox;
    (void)srvDescriptorSize;
    (void)frameHeapOffset;
    (void)defaultSphereMesh;
}
