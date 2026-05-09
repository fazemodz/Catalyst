#include "PostProcessPass.h"
#include <d3dcompiler.h>
#include <stdexcept>
#include <cmath>
#include <DirectXMath.h>
#include "Common.h"
#include "../ShaderCompiler.h"

using namespace DirectX;

void PostProcessPass::Initialize(ID3D12Device* device, int width, int height) {
    CreateOffscreenRenderTarget(device, width, height);
    CreatePipeline(device);
}

void PostProcessPass::OnResize(ID3D12Device* device, int width, int height) {
    CreateOffscreenRenderTarget(device, width, height);
}

void PostProcessPass::CreateOffscreenRenderTarget(ID3D12Device* device, int width, int height) {
    if (!m_hdrRtvHeap) { D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {}; rtvDesc.NumDescriptors = 1; rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; ThrowIfFailed(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_hdrRtvHeap))); }
    if (!m_hdrSrvHeap) { D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {}; srvDesc.NumDescriptors = 1; srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; ThrowIfFailed(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_hdrSrvHeap))); }
    
    m_hdrRenderTarget.Reset();
    D3D12_RESOURCE_DESC desc = {}; desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width = width; desc.Height = height; desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; desc.SampleDesc.Count = 1; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clearVal = {}; clearVal.Format = desc.Format; clearVal.Color[0] = 0.0f; clearVal.Color[1] = 0.0f; clearVal.Color[2] = 0.0f; clearVal.Color[3] = 1.0f;
    D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_DEFAULT};
    ThrowIfFailed(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearVal, IID_PPV_ARGS(&m_hdrRenderTarget)));
    device->CreateRenderTargetView(m_hdrRenderTarget.Get(), nullptr, m_hdrRtvHeap->GetCPUDescriptorHandleForHeapStart());
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {}; srvDesc.Format = desc.Format; srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(m_hdrRenderTarget.Get(), &srvDesc, m_hdrSrvHeap->GetCPUDescriptorHandleForHeapStart());
}

void PostProcessPass::CreatePipeline(ID3D12Device* device) {
    ComPtr<ID3DBlob> vs, ps, err;
    HRESULT hr = CatalystRender::CompileShaderFromFile(L"Shaders/postprocess.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", D3DCOMPILE_DEBUG, 0, &vs, &err);
    if(FAILED(hr)) ThrowIfFailed(hr, err ? (char*)err->GetBufferPointer() : "PP VS Failed");
    hr = CatalystRender::CompileShaderFromFile(L"Shaders/postprocess.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", D3DCOMPILE_DEBUG, 0, &ps, &err);
    if(FAILED(hr)) ThrowIfFailed(hr, err ? (char*)err->GetBufferPointer() : "PP PS Failed");

    D3D12_ROOT_PARAMETER rp[2] = {};
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; range.NumDescriptors = 1; range.BaseShaderRegister = 0; range.RegisterSpace = 0; range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[0].DescriptorTable.NumDescriptorRanges = 1; rp[0].DescriptorTable.pDescriptorRanges = &range; rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rp[1].Constants.ShaderRegister = 0; rp[1].Constants.RegisterSpace = 0; rp[1].Constants.Num32BitValues = 8; rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler = {}; sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT; sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP; sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP; sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP; sampler.ShaderRegister = 0; sampler.RegisterSpace = 0; sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rsDesc = {}; rsDesc.NumParameters = 2; rsDesc.pParameters = rp; rsDesc.NumStaticSamplers = 1; rsDesc.pStaticSamplers = &sampler; rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> sig, rsErr;
    hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &rsErr);
    if(FAILED(hr)) ThrowIfFailed(hr, rsErr ? (char*)rsErr->GetBufferPointer() : "PP RS Serialize Failed");
    ThrowIfFailed(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_postProcessRootSignature)), "Post-process root signature creation failed");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {}; pso.InputLayout = { nullptr, 0 }; pso.pRootSignature = m_postProcessRootSignature.Get(); pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() }; pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() }; pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; pso.DepthStencilState.DepthEnable = FALSE; pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; pso.SampleMask = UINT_MAX; pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; pso.NumRenderTargets = 1; pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; pso.SampleDesc.Count = 1;
    ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_postProcessPipelineState)));
}

void PostProcessPass::TransitionToRTV(ID3D12GraphicsCommandList* commandList) {
    D3D12_RESOURCE_BARRIER b = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION }; b.Transition.pResource = m_hdrRenderTarget.Get(); b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE; b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET; commandList->ResourceBarrier(1, &b);
}

void PostProcessPass::TransitionToSRV(ID3D12GraphicsCommandList* commandList) {
    D3D12_RESOURCE_BARRIER b = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION }; b.Transition.pResource = m_hdrRenderTarget.Get(); b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE; commandList->ResourceBarrier(1, &b);
}

void PostProcessPass::Clear(ID3D12GraphicsCommandList* commandList) {
    const float clearHDR[] = { 0.0f, 0.0f, 0.0f, 1.0f }; commandList->ClearRenderTargetView(GetRTV(), clearHDR, 0, nullptr);
}

void PostProcessPass::Render(ID3D12GraphicsCommandList* commandList, const Camera& camera, const std::vector<GameObject>& gameObjects, const PostProcessSettings& globalPP, int width, int height) {
    PostProcessSettings activePP = globalPP;
    XMFLOAT3 camPosFloat3 = camera.GetPosition(); XMVECTOR camPosVec = XMLoadFloat3(&camPosFloat3);
    auto lerp = [](float a, float b, float t) { return a + t * (b - a); };

    for (const auto& obj : gameObjects) {
        if (obj.type == ObjectType::PostProcessVolume) {
            XMMATRIX mScale = XMMatrixScaling(obj.scale.x, obj.scale.y, obj.scale.z); XMMATRIX mRot = XMMatrixRotationRollPitchYaw(obj.rotation.x, obj.rotation.y, obj.rotation.z); XMMATRIX mTrans = XMMatrixTranslation(obj.position.x, obj.position.y, obj.position.z);
            XMMATRIX world = mScale * mRot * mTrans; XMVECTOR det; XMMATRIX invWorld = XMMatrixInverse(&det, world);
            XMVECTOR localCamPos = XMVector3TransformCoord(camPosVec, invWorld); XMFLOAT3 localCam; XMStoreFloat3(&localCam, localCamPos);
            float dx = max(0.0f, abs(localCam.x) - 0.5f); float dy = max(0.0f, abs(localCam.y) - 0.5f); float dz = max(0.0f, abs(localCam.z) - 0.5f); float localDist = sqrt(dx*dx + dy*dy + dz*dz); float maxScale = max(obj.scale.x, max(obj.scale.y, obj.scale.z)); float worldDist = localDist * maxScale;
            
            if (worldDist <= obj.ppSettings.blendRadius) {
                float weight = 1.0f; if (obj.ppSettings.blendRadius > 0.0f) { weight = 1.0f - (worldDist / obj.ppSettings.blendRadius); weight = weight * weight * (3.0f - 2.0f * weight); }
                activePP.exposure = lerp(activePP.exposure, obj.ppSettings.exposure, weight); activePP.colorTint.x = lerp(activePP.colorTint.x, obj.ppSettings.colorTint.x, weight); activePP.colorTint.y = lerp(activePP.colorTint.y, obj.ppSettings.colorTint.y, weight); activePP.colorTint.z = lerp(activePP.colorTint.z, obj.ppSettings.colorTint.z, weight); activePP.bloomThreshold = lerp(activePP.bloomThreshold, obj.ppSettings.bloomThreshold, weight); activePP.bloomIntensity = lerp(activePP.bloomIntensity, obj.ppSettings.bloomIntensity, weight);
            }
        }
    }

    commandList->SetPipelineState(m_postProcessPipelineState.Get());
    commandList->SetGraphicsRootSignature(m_postProcessRootSignature.Get());
    ID3D12DescriptorHeap* ppHeaps[] = { m_hdrSrvHeap.Get() }; commandList->SetDescriptorHeaps(1, ppHeaps);
    commandList->SetGraphicsRootDescriptorTable(0, m_hdrSrvHeap->GetGPUDescriptorHandleForHeapStart());
    float ppData[8] = { activePP.exposure, activePP.colorTint.x, activePP.colorTint.y, activePP.colorTint.z, activePP.bloomThreshold, activePP.bloomIntensity, static_cast<float>(width), static_cast<float>(height) };
    commandList->SetGraphicsRoot32BitConstants(1, 8, ppData, 0); commandList->DrawInstanced(3, 1, 0, 0);
}
