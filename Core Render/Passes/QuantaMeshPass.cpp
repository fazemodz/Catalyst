#include "QuantaMeshPass.h"
#include <d3dcompiler.h>
#include <map>
#include "../../Error handler/Common.h"

void QuantaMeshPass::Initialize(ID3D12Device* device)
{
    CreatePipelines(device);
    CreateComputeBuffers(device);
}

void QuantaMeshPass::CreateComputeBuffers(ID3D12Device* device)
{
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = sizeof(ObjectData) * 10000;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_objectDataBuffer)));

    m_objectDataBuffer->Map(0, nullptr, (void**)&m_mappedObjectData);

    bufferDesc.Width = (sizeof(GlobalBufferData) + 255) & ~255;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_globalCB)));

    m_globalCB->Map(0, nullptr, (void**)&m_mappedGlobalCB);

    bufferDesc.Width = (sizeof(MaterialConstants) + 255) & ~255;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_materialCB)));

    m_materialCB->Map(0, nullptr, (void**)&m_mappedMaterialCB);

    bufferDesc.Width = sizeof(DrawIndexedArgs) * 10000;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
        nullptr,
        IID_PPV_ARGS(&m_commandBuffer)));

    bufferDesc.Width = sizeof(uint32_t);
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
        nullptr,
        IID_PPV_ARGS(&m_counterBuffer)));

    bufferDesc.Width = sizeof(uint32_t);
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_zeroBuffer)));

    uint32_t* zero;
    m_zeroBuffer->Map(0, nullptr, (void**)&zero);
    *zero = 0;
}

void QuantaMeshPass::CreatePipelines(ID3D12Device* device) {
    ComPtr<ID3DBlob> err;
    ComPtr<ID3DBlob> rsErr;
    HRESULT hr;
    
    UINT standardFlags = D3DCOMPILE_DEBUG;

    D3D12_DEPTH_STENCIL_DESC defaultDepthStencil = {};
    defaultDepthStencil.DepthEnable = TRUE;
    defaultDepthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    defaultDepthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    defaultDepthStencil.StencilEnable = FALSE;
    defaultDepthStencil.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    defaultDepthStencil.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    const D3D12_DEPTH_STENCILOP_DESC defaultStencilOp = { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
    defaultDepthStencil.FrontFace = defaultStencilOp;
    defaultDepthStencil.BackFace = defaultStencilOp;

    D3D12_RENDER_TARGET_BLEND_DESC defaultRTBlend = {};
    defaultRTBlend.BlendEnable = FALSE;
    defaultRTBlend.LogicOpEnable = FALSE;
    defaultRTBlend.SrcBlend = D3D12_BLEND_ONE;
    defaultRTBlend.DestBlend = D3D12_BLEND_ZERO;
    defaultRTBlend.BlendOp = D3D12_BLEND_OP_ADD;
    defaultRTBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    defaultRTBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
    defaultRTBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    defaultRTBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
    defaultRTBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_BLEND_DESC defaultBlend = {};
    defaultBlend.AlphaToCoverageEnable = FALSE;
    defaultBlend.IndependentBlendEnable = FALSE;
    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) defaultBlend.RenderTarget[i] = defaultRTBlend;

    D3D12_RASTERIZER_DESC defaultRasterizer = {};
    defaultRasterizer.FillMode = D3D12_FILL_MODE_SOLID;
    defaultRasterizer.CullMode = D3D12_CULL_MODE_BACK;
    defaultRasterizer.FrontCounterClockwise = FALSE;
    defaultRasterizer.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    defaultRasterizer.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    defaultRasterizer.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    defaultRasterizer.DepthClipEnable = TRUE;
    defaultRasterizer.MultisampleEnable = FALSE;
    defaultRasterizer.AntialiasedLineEnable = FALSE;
    defaultRasterizer.ForcedSampleCount = 0;
    defaultRasterizer.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // ==========================================
    // 1. COMPUTE PIPELINE
    // ==========================================
    ComPtr<ID3DBlob> cs;
    hr = D3DCompileFromFile(L"Shaders/QuantaCull.hlsl", nullptr, nullptr, "CSMain", "cs_5_1", standardFlags, 0, &cs, &err);
    if(FAILED(hr)) ThrowIfFailed(hr, err ? (char*)err->GetBufferPointer() : "Compute Cull Failed");

    D3D12_ROOT_PARAMETER crp[4] = {};
    crp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; 
    crp[0].Constants.ShaderRegister = 0; 
    crp[0].Constants.RegisterSpace = 0; 
    // FIX: Changed from 20 to 21 to safely pass the global offset index!
    crp[0].Constants.Num32BitValues = 21; 
    crp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    crp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; crp[1].Descriptor.ShaderRegister = 0; crp[1].Descriptor.RegisterSpace = 0; crp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    crp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; crp[2].Descriptor.ShaderRegister = 0; crp[2].Descriptor.RegisterSpace = 0; crp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    crp[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; crp[3].Descriptor.ShaderRegister = 1; crp[3].Descriptor.RegisterSpace = 0; crp[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC crsDesc = {}; crsDesc.NumParameters = 4; crsDesc.pParameters = crp;
    ComPtr<ID3DBlob> csig; 
    hr = D3D12SerializeRootSignature(&crsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &csig, &rsErr);
    if(FAILED(hr)) ThrowIfFailed(hr, rsErr ? (char*)rsErr->GetBufferPointer() : "Compute RS Serialize Failed");
    
    hr = device->CreateRootSignature(0, csig->GetBufferPointer(), csig->GetBufferSize(), IID_PPV_ARGS(&m_computeRootSignature));
    if(FAILED(hr)) ThrowIfFailed(hr, "Failed to create Compute Root Signature.");

    D3D12_COMPUTE_PIPELINE_STATE_DESC cpso = {};
    cpso.pRootSignature = m_computeRootSignature.Get(); cpso.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
    hr = device->CreateComputePipelineState(&cpso, IID_PPV_ARGS(&m_computePipelineState));
    if(FAILED(hr)) ThrowIfFailed(hr, "Failed to create Compute Pipeline State.");

    D3D12_INDIRECT_ARGUMENT_DESC argDesc[1] = {}; argDesc[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    D3D12_COMMAND_SIGNATURE_DESC csDesc = {}; csDesc.NumArgumentDescs = 1; csDesc.pArgumentDescs = argDesc; csDesc.ByteStride = sizeof(DrawIndexedArgs);
    device->CreateCommandSignature(&csDesc, nullptr, IID_PPV_ARGS(&m_commandSignature));

    // ==========================================
    // 2. QUANTA PIPELINE
    // ==========================================
    ComPtr<ID3DBlob> vs, ps;
    hr = D3DCompileFromFile(L"Shaders/QuantaMesh.hlsl", nullptr, nullptr, "VSMain", "vs_5_1", standardFlags, 0, &vs, &err); 
    if(FAILED(hr)) ThrowIfFailed(hr, err ? (char*)err->GetBufferPointer() : "Quanta VS Failed");
    hr = D3DCompileFromFile(L"Shaders/QuantaMesh.hlsl", nullptr, nullptr, "PSMain", "ps_5_1", standardFlags, 0, &ps, &err); 
    if(FAILED(hr)) ThrowIfFailed(hr, err ? (char*)err->GetBufferPointer() : "Quanta PS Failed");

    D3D12_INPUT_ELEMENT_DESC layout[] = { 
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, 
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, 
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, 
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, 
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 } 
    };
    
    D3D12_ROOT_PARAMETER rpQ[5] = {}; 
    rpQ[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; rpQ[0].Descriptor.ShaderRegister = 0; rpQ[0].Descriptor.RegisterSpace = 0; rpQ[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rpQ[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; rpQ[1].Descriptor.ShaderRegister = 1; rpQ[1].Descriptor.RegisterSpace = 0; rpQ[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rpQ[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; rpQ[2].Descriptor.ShaderRegister = 0; rpQ[2].Descriptor.RegisterSpace = 0; rpQ[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    
    D3D12_DESCRIPTOR_RANGE bindlessRange = {}; 
    bindlessRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; 
    bindlessRange.NumDescriptors = 1024;
    bindlessRange.BaseShaderRegister = 0; 
    bindlessRange.RegisterSpace = 1; 
    bindlessRange.OffsetInDescriptorsFromTableStart = 0;
    
    rpQ[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rpQ[3].DescriptorTable.NumDescriptorRanges = 1; rpQ[3].DescriptorTable.pDescriptorRanges = &bindlessRange; rpQ[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_DESCRIPTOR_RANGE shadowRange = {};
    shadowRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    shadowRange.NumDescriptors = 1;
    shadowRange.BaseShaderRegister = 7;
    shadowRange.RegisterSpace = 0;
    shadowRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rpQ[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rpQ[4].DescriptorTable.NumDescriptorRanges = 1;
    rpQ[4].DescriptorTable.pDescriptorRanges = &shadowRange;
    rpQ[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {}; 
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP; samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP; samplers[0].ShaderRegister = 0; samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR; samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER; samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER; samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER; samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE; samplers[1].ShaderRegister = 1; samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDescQ = {}; rsDescQ.NumParameters = 5; rsDescQ.pParameters = rpQ; rsDescQ.NumStaticSamplers = 2; rsDescQ.pStaticSamplers = samplers; rsDescQ.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> sigQ; 
    hr = D3D12SerializeRootSignature(&rsDescQ, D3D_ROOT_SIGNATURE_VERSION_1, &sigQ, &rsErr); 
    if(FAILED(hr)) ThrowIfFailed(hr, rsErr ? (char*)rsErr->GetBufferPointer() : "Quanta RS Serialize Failed");
    
    hr = device->CreateRootSignature(0, sigQ->GetBufferPointer(), sigQ->GetBufferSize(), IID_PPV_ARGS(&m_quantaRootSignature));
    if(FAILED(hr)) ThrowIfFailed(hr, "Failed to create Quanta Root Signature.");
    
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoQ = {}; 
    psoQ.InputLayout = { layout, 5 }; 
    psoQ.pRootSignature = m_quantaRootSignature.Get(); 
    psoQ.VS = { vs->GetBufferPointer(), vs->GetBufferSize() }; 
    psoQ.PS = { ps->GetBufferPointer(), ps->GetBufferSize() }; 
    
    D3D12_BLEND_DESC quantaBlend = defaultBlend;
    quantaBlend.RenderTarget[0].BlendEnable = FALSE;
    quantaBlend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoQ.BlendState = quantaBlend;

    psoQ.RasterizerState = defaultRasterizer;
    psoQ.DepthStencilState = defaultDepthStencil;
    psoQ.DSVFormat = DXGI_FORMAT_D32_FLOAT; 
    psoQ.SampleMask = UINT_MAX; 
    psoQ.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; 
    psoQ.NumRenderTargets = 1; 
    psoQ.SampleDesc.Count = 1; 
    psoQ.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; 
    
    hr = device->CreateGraphicsPipelineState(&psoQ, IID_PPV_ARGS(&m_quantaPipelineState));
    if (FAILED(hr)) ThrowIfFailed(hr, "Failed to create Quanta Graphics Pipeline!");

    // ==========================================
    // 3. PREVIEW PIPELINE 
    // ==========================================
    ComPtr<ID3DBlob> pvs, pps;

    hr = D3DCompileFromFile(L"Shaders/shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_1", standardFlags, 0, &pvs, &err);
    if(FAILED(hr)) ThrowIfFailed(hr, err ? (char*)err->GetBufferPointer() : "Preview VS Failed");

    hr = D3DCompileFromFile(L"Shaders/shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_1", standardFlags, 0, &pps, &err);
    if(FAILED(hr)) ThrowIfFailed(hr, err ? (char*)err->GetBufferPointer() : "Preview PS Failed");
    
    D3D12_ROOT_PARAMETER rp[2] = {}; 
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; rp[0].Descriptor.ShaderRegister = 0; rp[0].Descriptor.RegisterSpace = 0; rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; 
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[1].DescriptorTable.NumDescriptorRanges = 1; 
    
    D3D12_DESCRIPTOR_RANGE range = {}; 
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; range.NumDescriptors = 8; range.BaseShaderRegister = 0; range.RegisterSpace = 0; range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; 
    rp[1].DescriptorTable.pDescriptorRanges = &range; rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    
    D3D12_ROOT_SIGNATURE_DESC rsDesc = {}; rsDesc.NumParameters = 2; rsDesc.pParameters = rp; rsDesc.NumStaticSamplers = 2; rsDesc.pStaticSamplers = samplers; rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> sig; 
    hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &rsErr); 
    if(FAILED(hr)) ThrowIfFailed(hr, rsErr ? (char*)rsErr->GetBufferPointer() : "Preview RS Serialize Failed");
    
    hr = device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_previewRootSignature));
    if(FAILED(hr)) ThrowIfFailed(hr, "Failed to create Preview Root Signature.");
    
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.InputLayout = { layout, 5 };
    pso.pRootSignature = m_previewRootSignature.Get();
    pso.VS = { pvs->GetBufferPointer(), pvs->GetBufferSize() };
    pso.PS = { pps->GetBufferPointer(), pps->GetBufferSize() };
    pso.RasterizerState = defaultRasterizer;
    pso.BlendState = defaultBlend;
    pso.DepthStencilState = defaultDepthStencil;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; 
    pso.SampleDesc.Count = 1;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    
    hr = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_previewPipelineState));
    if (FAILED(hr)) ThrowIfFailed(hr, "Failed to create Preview Graphics Pipeline.");

    // ==========================================
    // 4. SKYBOX PIPELINE 
    // ==========================================
    ComPtr<ID3DBlob> svs, sps;
    
    hr = D3DCompileFromFile(L"Shaders/skybox.hlsl", nullptr, nullptr, "VSMain", "vs_5_1", standardFlags, 0, &svs, &err);
    if(FAILED(hr)) ThrowIfFailed(hr, err ? (char*)err->GetBufferPointer() : "Skybox VS Failed");

    hr = D3DCompileFromFile(L"Shaders/skybox.hlsl", nullptr, nullptr, "PSMain", "ps_5_1", standardFlags, 0, &sps, &err);
    if(FAILED(hr)) ThrowIfFailed(hr, err ? (char*)err->GetBufferPointer() : "Skybox PS Failed");
    
    D3D12_INPUT_ELEMENT_DESC skyLayout[] = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 } };
    
    D3D12_ROOT_PARAMETER srp[2] = {}; 
    srp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; srp[0].Constants.ShaderRegister = 0; srp[0].Constants.RegisterSpace = 0; srp[0].Constants.Num32BitValues = 16; srp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; 
    
    D3D12_DESCRIPTOR_RANGE srange = {}; 
    srange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; srange.NumDescriptors = 1; srange.BaseShaderRegister = 0; srange.RegisterSpace = 0; srange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; 
    srp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; srp[1].DescriptorTable.NumDescriptorRanges = 1; srp[1].DescriptorTable.pDescriptorRanges = &srange; srp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; 
    
    D3D12_STATIC_SAMPLER_DESC ssampler = {}; 
    ssampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; ssampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; ssampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP; ssampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP; ssampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    
    D3D12_ROOT_SIGNATURE_DESC srsDesc = {}; srsDesc.NumParameters = 2; srsDesc.pParameters = srp; srsDesc.NumStaticSamplers = 1; srsDesc.pStaticSamplers = &ssampler; srsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> ssig; 
    hr = D3D12SerializeRootSignature(&srsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &ssig, &rsErr); 
    if(FAILED(hr)) ThrowIfFailed(hr, rsErr ? (char*)rsErr->GetBufferPointer() : "Skybox RS Serialize Failed");
    
    hr = device->CreateRootSignature(0, ssig->GetBufferPointer(), ssig->GetBufferSize(), IID_PPV_ARGS(&m_skyboxRootSignature));
    if(FAILED(hr)) ThrowIfFailed(hr, "Failed to create Skybox Root Signature.");
    
    D3D12_GRAPHICS_PIPELINE_STATE_DESC spso = {};
    spso.InputLayout = { skyLayout, 1 };
    spso.pRootSignature = m_skyboxRootSignature.Get();
    spso.VS = { svs->GetBufferPointer(), svs->GetBufferSize() };
    spso.PS = { sps->GetBufferPointer(), sps->GetBufferSize() };
    
    D3D12_RASTERIZER_DESC skyRaster = defaultRasterizer;
    skyRaster.CullMode = D3D12_CULL_MODE_FRONT; 
    spso.RasterizerState = skyRaster;
    
    spso.BlendState = defaultBlend;
    
    D3D12_DEPTH_STENCIL_DESC skyDepth = defaultDepthStencil;
    skyDepth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; 
    skyDepth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; 
    spso.DepthStencilState = skyDepth;
    
    spso.SampleMask = UINT_MAX;
    spso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    spso.NumRenderTargets = 1;
    spso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; 
    spso.SampleDesc.Count = 1;
    spso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    
    hr = device->CreateGraphicsPipelineState(&spso, IID_PPV_ARGS(&m_skyboxPipelineState));
    if (FAILED(hr)) ThrowIfFailed(hr, "Failed to create Skybox Graphics Pipeline.");
}

void QuantaMeshPass::Render(
    ID3D12GraphicsCommandList* commandList,
    ID3D12Device* device,
    const std::vector<GameObject>& gameObjects,
    const Camera& camera,
    int width,
    int height,
    DirectX::XMMATRIX lightSpaceMatrix,
    DirectX::XMFLOAT3 activeLightDir,
    float activeIntensity,
    Texture* activeSkybox,
    ID3D12DescriptorHeap* bindlessHeap,
    UINT srvDescriptorSize,
    UINT& frameHeapOffset,
    Texture* texWhite,
    Texture* texNormal,
    Texture* texBlack,
    ID3D12DescriptorHeap* shadowSrvHeap,
    Mesh* defaultSphereMesh)
{
    using namespace DirectX;

    XMMATRIX mView = camera.GetViewMatrix();
    XMMATRIX mProj = camera.GetProjectionMatrix();

    GlobalBufferData globalData = {};
    globalData.viewProj = XMMatrixTranspose(mView * mProj);
    globalData.lightSpaceMatrix = lightSpaceMatrix;
    globalData.lightDir = activeLightDir;
    globalData.lightIntensity = activeIntensity;
    globalData.cameraPos = camera.GetPosition();

    memcpy(m_mappedGlobalCB, &globalData, sizeof(GlobalBufferData));

    MaterialConstants mat = {};
    mat.materialColor = XMFLOAT4(1, 1, 1, 1);
    mat.metallic = 0.5f;
    mat.roughness = 0.5f;
    mat.albedoIndex = texWhite->GetBindlessIndex();
    mat.normalIndex = texNormal->GetBindlessIndex();
    mat.metallicIndex = texBlack->GetBindlessIndex();
    mat.roughnessIndex = texBlack->GetBindlessIndex();
    memcpy(m_mappedMaterialCB, &mat, sizeof(MaterialConstants));

    std::map<Mesh*, std::vector<const GameObject*>> meshGroups;

    for (const auto& obj : gameObjects)
    {
        if (obj.type == ObjectType::Skybox ||
            obj.type == ObjectType::PostProcessVolume)
            continue;

        Mesh* mesh = (obj.asset && obj.asset->mesh)
            ? obj.asset->mesh
            : defaultSphereMesh;

        if (mesh)
            meshGroups[mesh].push_back(&obj);
    }

    D3D12_VIEWPORT vp = { 0,0,(float)width,(float)height,0,1 };
    D3D12_RECT sc = { 0,0,width,height };

    commandList->RSSetViewports(1, &vp);
    commandList->RSSetScissorRects(1, &sc);

    uint32_t globalObjectIndex = 0;

    for (auto& pair : meshGroups)
    {
        Mesh* mesh = pair.first;
        auto& instances = pair.second;

        for (size_t i = 0; i < instances.size(); i++)
        {
            const GameObject* obj = instances[i];
            uint32_t index = globalObjectIndex + (uint32_t)i;

            XMMATRIX scale =
                XMMatrixScaling(obj->scale.x, obj->scale.y, obj->scale.z);

            XMMATRIX rot =
                XMMatrixRotationRollPitchYaw(
                    obj->rotation.x,
                    obj->rotation.y,
                    obj->rotation.z);

            XMMATRIX trans =
                XMMatrixTranslation(
                    obj->position.x,
                    obj->position.y,
                    obj->position.z);

            m_mappedObjectData[index].worldMatrix =
                XMMatrixTranspose(scale * rot * trans);

            m_mappedObjectData[index].colorOverride = obj->color;
            m_mappedObjectData[index].center = obj->position;

            m_mappedObjectData[index].radius =
                max(obj->scale.x,
                max(obj->scale.y, obj->scale.z));

            m_mappedObjectData[index].indexCount =
                mesh->GetIndexCount();

            m_mappedObjectData[index].startIndexLocation = 0;
            m_mappedObjectData[index].baseVertexLocation = 0;
        }

        uint32_t instanceCount = (uint32_t)instances.size();

        D3D12_RESOURCE_BARRIER barriers[2] = {};

        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource = m_counterBuffer.Get();
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Transition.pResource = m_commandBuffer.Get();
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        commandList->ResourceBarrier(2, barriers);

        commandList->CopyBufferRegion(m_counterBuffer.Get(), 0, m_zeroBuffer.Get(), 0, sizeof(uint32_t));

        D3D12_RESOURCE_BARRIER uavBarrier = {};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        uavBarrier.Transition.pResource = m_counterBuffer.Get();
        uavBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        uavBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        uavBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        commandList->ResourceBarrier(1, &uavBarrier);

        commandList->SetPipelineState(m_computePipelineState.Get());
        commandList->SetComputeRootSignature(m_computeRootSignature.Get());
        struct CullConstants
        {
            XMMATRIX vp;
            XMFLOAT3 camPos;
            uint32_t count;
            uint32_t globalStartIndex; 
        };

        CullConstants cull = {};
        cull.vp = XMMatrixTranspose(mView * mProj);
        cull.camPos = camera.GetPosition();
        cull.count = instanceCount;
        cull.globalStartIndex = globalObjectIndex; 

        // Pass all 21 values safely into the shader constants
        commandList->SetComputeRoot32BitConstants(0, 21, &cull, 0);

        // FIX: Reverted pointer math. Always pass the clean base address of the buffer.
        commandList->SetComputeRootShaderResourceView(1, m_objectDataBuffer->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(2, m_commandBuffer->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(3, m_counterBuffer->GetGPUVirtualAddress());

        commandList->Dispatch((instanceCount + 63) / 64, 1, 1);

        D3D12_RESOURCE_BARRIER endBarriers[2] = {};

        endBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        endBarriers[0].Transition.pResource = m_counterBuffer.Get();
        endBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        endBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        endBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        endBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        endBarriers[1].Transition.pResource = m_commandBuffer.Get();
        endBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        endBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        endBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        commandList->ResourceBarrier(2, endBarriers);

        commandList->SetPipelineState(m_quantaPipelineState.Get());
        commandList->SetGraphicsRootSignature(m_quantaRootSignature.Get());

        commandList->SetGraphicsRootConstantBufferView(0, m_globalCB->GetGPUVirtualAddress());
        commandList->SetGraphicsRootConstantBufferView(1, m_materialCB->GetGPUVirtualAddress());

        // FIX: Use the clean base address for Graphics as well. The compute shader handled the offset.
        commandList->SetGraphicsRootShaderResourceView(2, m_objectDataBuffer->GetGPUVirtualAddress());
        commandList->SetGraphicsRootDescriptorTable(3, bindlessHeap->GetGPUDescriptorHandleForHeapStart());

        if (shadowSrvHeap)
        {
            commandList->SetGraphicsRootDescriptorTable(4, shadowSrvHeap->GetGPUDescriptorHandleForHeapStart());
        }

        D3D12_VERTEX_BUFFER_VIEW vbv = mesh->GetVertexView();
        D3D12_INDEX_BUFFER_VIEW ibv = mesh->GetIndexView();

        commandList->IASetVertexBuffers(0, 1, &vbv);
        commandList->IASetIndexBuffer(&ibv);

        commandList->ExecuteIndirect(
            m_commandSignature.Get(),
            instanceCount,
            m_commandBuffer.Get(),
            0,
            m_counterBuffer.Get(),
            0);

        globalObjectIndex += instanceCount;
    }

    // ==========================================
    // RENDER SKYBOX
    // ==========================================
    if (activeSkybox && defaultSphereMesh)
    {
        commandList->SetPipelineState(m_skyboxPipelineState.Get());
        commandList->SetGraphicsRootSignature(m_skyboxRootSignature.Get());

        // Calculate View-Projection without camera translation
        XMMATRIX skyView = mView;
        skyView.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f); 
        XMMATRIX skyWVP = XMMatrixTranspose(skyView * mProj);

        commandList->SetGraphicsRoot32BitConstants(0, 16, &skyWVP, 0);

        // Bind the Skybox Texture
        D3D12_GPU_DESCRIPTOR_HANDLE skyboxHandle = bindlessHeap->GetGPUDescriptorHandleForHeapStart();
        skyboxHandle.ptr += activeSkybox->GetBindlessIndex() * srvDescriptorSize;
        commandList->SetGraphicsRootDescriptorTable(1, skyboxHandle);

        // Draw the Skybox
        D3D12_VERTEX_BUFFER_VIEW vbv = defaultSphereMesh->GetVertexView();
        D3D12_INDEX_BUFFER_VIEW ibv = defaultSphereMesh->GetIndexView();
        
        commandList->IASetVertexBuffers(0, 1, &vbv);
        commandList->IASetIndexBuffer(&ibv);

        commandList->DrawIndexedInstanced(defaultSphereMesh->GetIndexCount(), 1, 0, 0, 0);
    }
}