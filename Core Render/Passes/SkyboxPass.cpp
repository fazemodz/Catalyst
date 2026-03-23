#include "SkyboxPass.h"
#include <d3dcompiler.h>
#include <DirectXTex.h>

const char* skyboxShader = R"(
Texture2D skyTex : register(t0);
SamplerState skySamp : register(s0);

cbuffer Constants : register(b0) {
    float4x4 viewProj;
};

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
    
    // Scale up to bypass clipping plane
    float3 massivePos = input.pos * 1000.0f;
    float4 clipPos = mul(float4(massivePos, 1.0f), viewProj);
    
    output.pos = clipPos;
    // Push Z to the absolute limit without triggering hardware clipping cull
    output.pos.z = output.pos.w * 0.99999f; 
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET {
    float3 dir = normalize(input.localPos);
    
    // Equirectangular Spherical Mapping math
    float2 uv = float2(atan2(dir.z, dir.x), asin(dir.y));
    uv *= float2(0.15915494309f, 0.31830988618f); // Divide by 2PI and PI
    uv += 0.5f;
    uv.x = 1.0f - uv.x; // Flip horizontally to correct interior perspective mirroring
    
    float3 color = skyTex.SampleLevel(skySamp, uv, 0).rgb;
    return float4(color, 1.0f);
}
)";

void SkyboxPass::Initialize(ID3D12Device* device) {
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[2] = {};
    
    // Parameter 0: ViewProj Matrix
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.ShaderRegister = 0;
    rootParams[0].Constants.RegisterSpace = 0;
    rootParams[0].Constants.Num32BitValues = 16;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Parameter 1: Bindless Texture SRV
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Static Sampler for smooth sky gradient wrapping
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 2;
    rsDesc.pParameters = rootParams;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers = &sampler;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> signature, error;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));

    Microsoft::WRL::ComPtr<ID3DBlob> vs, ps;
    HRESULT hrVS = D3DCompile(skyboxShader, strlen(skyboxShader), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vs, &error);
    if (FAILED(hrVS) && error) OutputDebugStringA((char*)error->GetBufferPointer());
    
    HRESULT hrPS = D3DCompile(skyboxShader, strlen(skyboxShader), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &ps, &error);
    if (FAILED(hrPS) && error) OutputDebugStringA((char*)error->GetBufferPointer());

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterDesc.CullMode = D3D12_CULL_MODE_NONE; // Disable culling so we can be inside the cube
    rasterDesc.FrontCounterClockwise = FALSE;
    rasterDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rasterDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterDesc.DepthClipEnable = TRUE;
    rasterDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_BLEND_DESC blendDesc = {};
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

    device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso));
}

void SkyboxPass::LoadHDR(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const wchar_t* filename) {
    DirectX::ScratchImage image;
    HRESULT hr = DirectX::LoadFromHDRFile(filename, nullptr, image);
    if (FAILED(hr)) {
        // Fallback: Check if it's in the Assets subfolder instead of the executable root
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
    texDesc.Height = img->height;
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

void SkyboxPass::Render(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device, Mesh* cubeMesh, Camera& camera, ID3D12DescriptorHeap* bindlessHeap, uint32_t hdrIndex) {
    if (!cubeMesh || !m_pso) return;

    cmdList->SetPipelineState(m_pso.Get());
    cmdList->SetGraphicsRootSignature(m_rootSignature.Get());

    DirectX::XMMATRIX view = camera.GetViewMatrix();
    view.r[3] = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f); 
    DirectX::XMMATRIX proj = camera.GetProjectionMatrix();
    DirectX::XMMATRIX viewProj = DirectX::XMMatrixTranspose(view * proj);

    cmdList->SetGraphicsRoot32BitConstants(0, 16, &viewProj, 0);

    // Calculate dynamic GPU handle for our specific Texture Index inside the Bindless Heap
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = bindlessHeap->GetGPUDescriptorHandleForHeapStart();
    gpuHandle.ptr += hdrIndex * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    
    cmdList->SetGraphicsRootDescriptorTable(1, gpuHandle);

    cubeMesh->Draw(cmdList);
}