#include "UIRenderer.h"
#include <d3dcompiler.h>
#include <stdexcept>
#include <windows.h>
#include "../../Core Render/ShaderCompiler.h"

void UIRenderer::CreatePipelineState(ID3D12Device* device, DXGI_FORMAT rtvFormat) {
    D3D12_DESCRIPTOR_RANGE ranges[1];
    ranges[0].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors                    = (UINT)-1;
    ranges[0].BaseShaderRegister                = 0;
    ranges[0].RegisterSpace                     = 1;
    ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rp[2] = {};

    rp[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rp[0].Constants.ShaderRegister = 0;
    rp[0].Constants.RegisterSpace  = 0;
    rp[0].Constants.Num32BitValues = 17;
    rp[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;

    rp[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[1].DescriptorTable.NumDescriptorRanges = 1;
    rp[1].DescriptorTable.pDescriptorRanges   = ranges;
    rp[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter         = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU       = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV       = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW       = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor    = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MaxLOD         = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters     = 2;
    rsDesc.pParameters       = rp;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers   = &sampler;
    rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sigBlob, errBlob;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));

    ComPtr<ID3DBlob> vs, ps;
    HRESULT hrVS = CatalystRender::CompileShaderFromFile(L"Shaders/UI.hlsl", nullptr, nullptr, "VSMain", "vs_5_1", 0, 0, &vs, &errBlob);
    if (FAILED(hrVS)) {
        if (errBlob) OutputDebugStringA((char*)errBlob->GetBufferPointer());
        throw std::runtime_error("Failed to compile UI.hlsl Vertex Shader.");
    }

    HRESULT hrPS = CatalystRender::CompileShaderFromFile(L"Shaders/UI.hlsl", nullptr, nullptr, "PSMain", "ps_5_1", 0, 0, &ps, &errBlob);
    if (FAILED(hrPS)) {
        if (errBlob) OutputDebugStringA((char*)errBlob->GetBufferPointer());
        throw std::runtime_error("Failed to compile UI.hlsl Pixel Shader.");
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0,  8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable           = TRUE;
    blendDesc.RenderTarget[0].SrcBlend              = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable   = FALSE;
    depthDesc.StencilEnable = FALSE;

    D3D12_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterDesc.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout        = { layout, 3 };
    psoDesc.pRootSignature     = m_rootSignature.Get();
    psoDesc.VS                 = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS                 = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.RasterizerState    = rasterDesc;
    psoDesc.BlendState         = blendDesc;
    psoDesc.DepthStencilState  = depthDesc;
    psoDesc.SampleMask         = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets   = 1;
    psoDesc.RTVFormats[0]      = rtvFormat;
    psoDesc.SampleDesc.Count   = 1;

    device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState));
}

void UIRenderer::CreateDynamicBuffers(ID3D12Device* device) {
    // Allocate the starting capacity up front; Render() grows a frame's buffers
    // on demand when a draw list needs more than this.
    for (DynamicBuffers& frame : m_frames) {
        EnsureCapacity(frame, kInitialVertices, kInitialIndices);
    }
}
