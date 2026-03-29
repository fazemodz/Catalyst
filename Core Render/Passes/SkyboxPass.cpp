#include "SkyboxPass.h"
#include <d3dcompiler.h>
#include <DirectXTex.h>
#include "../Error Handler/Common.h"

const char* skyboxShader = R"(
cbuffer Constants : register(b0) {
    float4x4 viewProj;
    float4 zenithColor;
    float4 horizonColor;
};

cbuffer SkyTextureConstants : register(b1) {
    uint hdrIndex;
};

Texture2D<float4> bindlessTextures[1024] : register(t0, space1);
SamplerState linearWrapSampler : register(s0);

struct VSInput { 
    float3 pos : POSITION; 
    float4 color : COLOR;
    float2 uv : TEXCOORD;
    float3 norm : NORMAL;
    float3 tangent : TANGENT;
};

struct PSInput { 
    float4 pos : SV_POSITION; 
    float3 localPos : TEXCOORD; 
};

PSInput VSMain(VSInput input) {
    PSInput output;
    output.localPos = input.pos;
    
    // Scale up massively to bypass the near clipping plane
    float3 massivePos = input.pos * 4000.0f; 
    float4 clipPos = mul(float4(massivePos, 1.0f), viewProj);
    
    output.pos = clipPos;
    // Push Z to the absolute limit without triggering hardware clipping cull
    output.pos.z = output.pos.w * 0.99999f; 
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET {
    float3 dir = normalize(input.localPos);
    float t = saturate(dir.y * 0.5f + 0.5f);
    float3 gradientColor = lerp(horizonColor.rgb, zenithColor.rgb, t);

    if (hdrIndex == 0xFFFFFFFFu) {
        return float4(gradientColor, 1.0f);
    }

    static const float INV_TWO_PI = 0.15915494309f;
    static const float INV_PI = 0.31830988618f;
    float2 uv;
    uv.x = atan2(dir.z, dir.x) * INV_TWO_PI + 0.5f;
    uv.y = acos(clamp(dir.y, -1.0f, 1.0f)) * INV_PI;

    float3 hdrColor = bindlessTextures[hdrIndex].Sample(linearWrapSampler, uv).rgb;
    float3 mappedColor = hdrColor / (hdrColor + 1.0f);
    mappedColor = pow(saturate(mappedColor), 1.0f / 2.2f);
    return float4(mappedColor, 1.0f);
}
)";

void SkyboxPass::Initialize(ID3D12Device* device) {
    D3D12_ROOT_PARAMETER rootParams[3] = {};
    
    // Parameter 0: ViewProj Matrix + Zenith/Horizon colors
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.ShaderRegister = 0;
    rootParams[0].Constants.RegisterSpace = 0;
    rootParams[0].Constants.Num32BitValues = 24;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Parameter 1: HDR bindless index
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[1].Constants.ShaderRegister = 1;
    rootParams[1].Constants.RegisterSpace = 0;
    rootParams[1].Constants.Num32BitValues = 1;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_DESCRIPTOR_RANGE bindlessRange = {};
    bindlessRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    bindlessRange.NumDescriptors = 1024;
    bindlessRange.BaseShaderRegister = 0;
    bindlessRange.RegisterSpace = 1;
    bindlessRange.OffsetInDescriptorsFromTableStart = 0;

    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &bindlessRange;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    samplerDesc.ShaderRegister = 0;
    samplerDesc.RegisterSpace = 0;
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 3;
    rsDesc.pParameters = rootParams;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers = &samplerDesc;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> signature, error;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if (FAILED(hr)) ThrowIfFailed(hr, error ? (char*)error->GetBufferPointer() : "Skybox RS Serialize Failed");
    ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));

    Microsoft::WRL::ComPtr<ID3DBlob> vs, ps;
    HRESULT hrVS = D3DCompile(skyboxShader, strlen(skyboxShader), nullptr, nullptr, nullptr, "VSMain", "vs_5_1", 0, 0, &vs, &error);
    if (FAILED(hrVS)) ThrowIfFailed(hrVS, error ? (char*)error->GetBufferPointer() : "Skybox VS compile failed");
    
    HRESULT hrPS = D3DCompile(skyboxShader, strlen(skyboxShader), nullptr, nullptr, nullptr, "PSMain", "ps_5_1", 0, 0, &ps, &error);
    if (FAILED(hrPS)) ThrowIfFailed(hrPS, error ? (char*)error->GetBufferPointer() : "Skybox PS compile failed");

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterDesc.CullMode = D3D12_CULL_MODE_NONE; 
    rasterDesc.FrontCounterClockwise = FALSE;
    rasterDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rasterDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterDesc.DepthClipEnable = TRUE;
    rasterDesc.MultisampleEnable = FALSE;
    rasterDesc.AntialiasedLineEnable = FALSE;
    rasterDesc.ForcedSampleCount = 0;
    rasterDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc = {
        FALSE, FALSE,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_LOGIC_OP_NOOP,
        D3D12_COLOR_WRITE_ENABLE_ALL,
    };
    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
        blendDesc.RenderTarget[i] = defaultRenderTargetBlendDesc;
    }

    D3D12_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; 
    dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; 
    dsDesc.StencilEnable = FALSE;
    dsDesc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    dsDesc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    const D3D12_DEPTH_STENCILOP_DESC defaultStencilOp = { 
        D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS 
    };
    dsDesc.FrontFace = defaultStencilOp;
    dsDesc.BackFace = defaultStencilOp;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.pRootSignature = m_rootSignature.Get();
    if (vs) psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    if (ps) psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.RasterizerState = rasterDesc;
    psoDesc.BlendState = blendDesc;
    psoDesc.DepthStencilState = dsDesc;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso)), "Skybox graphics pipeline creation failed");
}

void SkyboxPass::LoadHDR(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const wchar_t* filename) {
    DirectX::ScratchImage image;
    HRESULT hr = DirectX::LoadFromHDRFile(filename, nullptr, image);
    if (FAILED(hr)) {
        hr = DirectX::LoadFromHDRFile(L"Assets/sky.hdr", nullptr, image);
        if (FAILED(hr)) {
            OutputDebugStringA("WARNING: sky.hdr not found. Skybox will default to a pure white void.\n");
            return;
        }
    }
    
    const DirectX::Image* img = image.GetImage(0, 0, 0);

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = img->width;
    texDesc.Height = static_cast<UINT>(img->height);
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = img->format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_skyTexture));

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
    UINT numRows;
    UINT64 rowSizeInBytes, totalBytes;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, &layout, &numRows, &rowSizeInBytes, &totalBytes);

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = totalBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_skyUpload));

    uint8_t* pData;
    m_skyUpload->Map(0, nullptr, (void**)&pData);
    for (UINT y = 0; y < numRows; ++y) {
        memcpy(pData + layout.Offset + y * layout.Footprint.RowPitch, img->pixels + y * img->rowPitch, rowSizeInBytes);
    }
    m_skyUpload->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = m_skyTexture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = m_skyUpload.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = layout;

    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_skyTexture.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmdList->ResourceBarrier(1, &barrier);
}

void SkyboxPass::Render(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device, Mesh* cubeMesh, Camera& camera, ID3D12DescriptorHeap* bindlessHeap, uint32_t hdrIndex, const DirectX::XMFLOAT4& zenithColor, const DirectX::XMFLOAT4& horizonColor) {
    if (!cubeMesh || !m_pso) return;

    cmdList->SetPipelineState(m_pso.Get());
    cmdList->SetGraphicsRootSignature(m_rootSignature.Get());

    DirectX::XMMATRIX view = camera.GetViewMatrix();
    view.r[3] = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f); 
    DirectX::XMMATRIX proj = camera.GetProjectionMatrix();
    DirectX::XMFLOAT4X4 viewProj;
    DirectX::XMStoreFloat4x4(&viewProj, DirectX::XMMatrixTranspose(view * proj));

    struct SkyboxConstants {
        DirectX::XMFLOAT4X4 viewProj;
        DirectX::XMFLOAT4 zenithColor;
        DirectX::XMFLOAT4 horizonColor;
    } constants = { viewProj, zenithColor, horizonColor };

    cmdList->SetGraphicsRoot32BitConstants(0, 24, &constants, 0);
    cmdList->SetGraphicsRoot32BitConstants(1, 1, &hdrIndex, 0);
    if (bindlessHeap) {
        cmdList->SetGraphicsRootDescriptorTable(2, bindlessHeap->GetGPUDescriptorHandleForHeapStart());
    }
    (void)device;
    cubeMesh->Draw(cmdList);
}
