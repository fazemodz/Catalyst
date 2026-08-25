#include "RaytracePass.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <filesystem>

#include "Common.h"
#include "../ShaderCompiler.h"

using namespace DirectX;
namespace fs = std::filesystem;

namespace {

constexpr UINT kDescriptorDepthSrv    = 0;
constexpr UINT kDescriptorGBufferSrv  = 1;
constexpr UINT kDescriptorPositionSrv = 2;
constexpr UINT kDescriptorShadowUav   = 3;
constexpr UINT kDescriptorReflectUav  = 4;
constexpr UINT kDescriptorFixedCount  = 5;

// Bindless geometry follows the fixed descriptors: two raw-buffer SRVs (vertex
// and index) per unique mesh, so a reflection hit can fetch its triangle.
constexpr UINT kMaxGeometryMeshes = 256;
constexpr UINT kDescriptorCount   = kDescriptorFixedCount + kMaxGeometryMeshes * 2;

// invViewProj (16) + cameraPos (4) + lightDir/bias (4) + outputSize/params (4)
// + skyZenith (4) + skyHorizon (4) + aoParams (4)
constexpr UINT kRootConstantCount = 44;   // + aoParams + shadowParams float4s

D3D12_RESOURCE_BARRIER TransitionBarrier(ID3D12Resource* resource,
                                         D3D12_RESOURCE_STATES before,
                                         D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter  = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}

D3D12_RESOURCE_BARRIER UavBarrier(ID3D12Resource* resource) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    return barrier;
}

XMMATRIX ComposeWorldMatrix(const GameObject& object) {
    return XMMatrixScaling(object.scale.x, object.scale.y, object.scale.z) *
           XMMatrixRotationRollPitchYaw(object.rotation.x, object.rotation.y, object.rotation.z) *
           XMMatrixTranslation(object.position.x, object.position.y, object.position.z);
}

}

bool RaytracePass::Initialize(ID3D12Device* device) {
    m_available = false;
    m_unavailableReason.clear();

    if (device == nullptr) {
        m_unavailableReason = L"No D3D12 device";
        return false;
    }

    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&m_device5)))) {
        m_unavailableReason = L"ID3D12Device5 not available (Windows 10 1809 or newer required)";
        return false;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options = {};
    if (FAILED(m_device5->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options))) ||
        options.RaytracingTier < D3D12_RAYTRACING_TIER_1_1) {
        m_unavailableReason = L"Adapter does not support DXR 1.1 inline raytracing";
        m_device5.Reset();
        return false;
    }

    if (!CreatePipeline(device)) {
        m_device5.Reset();
        return false;
    }

    CreateDescriptorHeaps(device);
    m_available = true;
    return true;
}

bool RaytracePass::CreatePipeline(ID3D12Device* device) {
    // Inline raytracing needs shader model 6.5, which FXC cannot produce, so
    // this shader is compiled ahead of time with DXC and loaded as bytecode.
    const std::wstring shaderPath = CatalystRender::ResolveShaderPath(L"Shaders/Raytrace.cso");
    ComPtr<ID3DBlob> computeShader;
    if (FAILED(D3DReadFileToBlob(shaderPath.c_str(), &computeShader))) {
        m_unavailableReason = L"Shaders/Raytrace.cso not found - rebuild it with dxc (see Raytrace.hlsl)";
        return false;
    }

    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;   // t1 depth, t2 gbuffer
    ranges[0].NumDescriptors     = 3;   // t1 depth, t2 gbuffer, t3 position
    ranges[0].BaseShaderRegister = 1;
    ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    ranges[1].RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;   // u0 shadow, u1 reflections
    ranges[1].NumDescriptors     = 2;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE geometryRange = {};
    geometryRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    geometryRange.NumDescriptors     = kMaxGeometryMeshes * 2;
    geometryRange.BaseShaderRegister = 0;
    geometryRange.RegisterSpace      = 1;   // MeshBuffers[] lives in space1
    geometryRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[6] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.ShaderRegister = 0;
    rootParams[0].Constants.Num32BitValues = kRootConstantCount;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;      // t0: TLAS
    rootParams[1].Descriptor.ShaderRegister = 0;

    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;      // t4: instance shading
    rootParams[2].Descriptor.ShaderRegister = 4;

    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[3].DescriptorTable.pDescriptorRanges   = &ranges[0];

    rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[4].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[4].DescriptorTable.pDescriptorRanges   = &ranges[1];

    rootParams[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[5].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[5].DescriptorTable.pDescriptorRanges   = &geometryRange;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = _countof(rootParams);
    rootDesc.pParameters   = rootParams;

    ComPtr<ID3DBlob> signature, error;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error))) {
        m_unavailableReason = L"Raytrace root signature could not be serialised";
        return false;
    }
    if (FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                           IID_PPV_ARGS(&m_rootSignature)))) {
        m_unavailableReason = L"Raytrace root signature could not be created";
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.CS = { computeShader->GetBufferPointer(), computeShader->GetBufferSize() };
    if (FAILED(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)))) {
        m_unavailableReason = L"Raytrace pipeline state could not be created";
        return false;
    }

    return true;
}

void RaytracePass::CreateDescriptorHeaps(ID3D12Device* device) {
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.NumDescriptors = 2; // gbuffer + world position
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC viewDesc = {};
    viewDesc.NumDescriptors = kDescriptorCount;
    viewDesc.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    viewDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&viewDesc, IID_PPV_ARGS(&m_viewHeap)));

    m_viewDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_rtvDescriptorSize  = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_QUERY_HEAP_DESC timestampDesc = {};
    timestampDesc.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    timestampDesc.Count = 2; // begin / end of the trace dispatch
    if (FAILED(device->CreateQueryHeap(&timestampDesc, IID_PPV_ARGS(&m_timestampHeap)))) {
        m_timestampHeap.Reset();
        return;
    }

    D3D12_HEAP_PROPERTIES readbackHeap = {};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readbackDesc = {};
    readbackDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width            = sizeof(UINT64) * 2;
    readbackDesc.Height           = 1;
    readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels        = 1;
    readbackDesc.Format           = DXGI_FORMAT_UNKNOWN;
    readbackDesc.SampleDesc.Count = 1;
    readbackDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&m_timestampReadback)))) {
        m_timestampReadback.Reset();
    }
}

void RaytracePass::OnResize(ID3D12Device* device, int width, int height) {
    if (!m_available || device == nullptr || width <= 0 || height <= 0) {
        return;
    }

    m_width  = width;
    m_height = height;
    m_gbuffer.Reset();
    m_positions.Reset();
    m_shadowMask.Reset();
    m_reflections.Reset();
    m_outputsAreSrv = false;
    m_gbufferIsSrv  = false;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    auto makeTexture = [&](DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state,
                           const D3D12_CLEAR_VALUE* clear, ComPtr<ID3D12Resource>& out) {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width            = static_cast<UINT64>(width);
        desc.Height           = static_cast<UINT>(height);
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.Format           = format;
        desc.SampleDesc.Count = 1;
        desc.Flags            = flags;
        ThrowIfFailed(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                                      state, clear, IID_PPV_ARGS(&out)));
    };

    D3D12_CLEAR_VALUE gbufferClear = {};
    gbufferClear.Format = kGBufferFormat;
    gbufferClear.Color[0] = 0.5f; gbufferClear.Color[1] = 0.5f;
    gbufferClear.Color[2] = 1.0f; gbufferClear.Color[3] = 1.0f;

    makeTexture(kGBufferFormat, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &gbufferClear, m_gbuffer);
    D3D12_CLEAR_VALUE positionClear = {};
    positionClear.Format = kPositionFormat;
    makeTexture(kPositionFormat, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &positionClear, m_positions);
    makeTexture(kShadowFormat, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, m_shadowMask);
    makeTexture(kReflectionFormat, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, m_reflections);

    device->CreateRenderTargetView(m_gbuffer.Get(), nullptr, m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    {
        D3D12_CPU_DESCRIPTOR_HANDLE positionRtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        positionRtv.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        device->CreateRenderTargetView(m_positions.Get(), nullptr, positionRtv);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_viewHeap->GetCPUDescriptorHandleForHeapStart();

    // t1: depth is written per-frame in Render() because the renderer owns it.
    handle.ptr += m_viewDescriptorSize; // skip to gbuffer slot

    D3D12_SHADER_RESOURCE_VIEW_DESC gbufferSrv = {};
    gbufferSrv.Format                  = kGBufferFormat;
    gbufferSrv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    gbufferSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    gbufferSrv.Texture2D.MipLevels     = 1;
    device->CreateShaderResourceView(m_gbuffer.Get(), &gbufferSrv, handle);

    handle.ptr += m_viewDescriptorSize;
    D3D12_SHADER_RESOURCE_VIEW_DESC positionSrv = gbufferSrv;
    positionSrv.Format = kPositionFormat;
    device->CreateShaderResourceView(m_positions.Get(), &positionSrv, handle);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    handle.ptr += m_viewDescriptorSize;
    uavDesc.Format = kShadowFormat;
    device->CreateUnorderedAccessView(m_shadowMask.Get(), nullptr, &uavDesc, handle);

    handle.ptr += m_viewDescriptorSize;
    uavDesc.Format = kReflectionFormat;
    device->CreateUnorderedAccessView(m_reflections.Get(), nullptr, &uavDesc, handle);
}

D3D12_CPU_DESCRIPTOR_HANDLE RaytracePass::GetGBufferRTV() const {
    return m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_CPU_DESCRIPTOR_HANDLE RaytracePass::GetPositionRTV() const {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += m_rtvDescriptorSize;
    return handle;
}

void RaytracePass::BeginGBuffer(ID3D12GraphicsCommandList* commandList) {
    if (!m_available || !m_gbuffer) {
        return;
    }

    const D3D12_RESOURCE_BARRIER gbufferToRtv = TransitionBarrier(
        m_gbuffer.Get(),
        m_gbufferIsSrv ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ResourceBarrier(1, &gbufferToRtv);

    const D3D12_RESOURCE_BARRIER positionToRtv = TransitionBarrier(
        m_positions.Get(),
        m_gbufferIsSrv ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ResourceBarrier(1, &positionToRtv);

    // Both targets are now render targets; EndGBuffer flips the flag back.
    m_gbufferIsSrv = false;

    const float clearColor[] = {0.5f, 0.5f, 1.0f, 1.0f};
    commandList->ClearRenderTargetView(GetGBufferRTV(), clearColor, 0, nullptr);
    const float positionClearColor[] = {0.0f, 0.0f, 0.0f, 0.0f};
    commandList->ClearRenderTargetView(GetPositionRTV(), positionClearColor, 0, nullptr);
}

void RaytracePass::EndGBuffer(ID3D12GraphicsCommandList* commandList) {
    if (!m_available || !m_gbuffer || m_gbufferIsSrv) {
        return;
    }


    const D3D12_RESOURCE_BARRIER barriers[2] = {
        TransitionBarrier(m_gbuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        TransitionBarrier(m_positions.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
    };
    commandList->ResourceBarrier(2, barriers);
    m_gbufferIsSrv = true;
}

void RaytracePass::EnsureBufferCapacity(ID3D12Device* device, ComPtr<ID3D12Resource>& buffer, UINT64 requiredBytes,
                                        D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES initialState,
                                        D3D12_RESOURCE_FLAGS flags) {
    if (requiredBytes == 0) {
        return;
    }
    if (buffer && buffer->GetDesc().Width >= requiredBytes) {
        return;
    }

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = heapType;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width            = requiredBytes;
    desc.Height           = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.Format           = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags            = flags;

    buffer.Reset();
    ThrowIfFailed(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, initialState,
                                                  nullptr, IID_PPV_ARGS(&buffer)));
}

ID3D12Resource* RaytracePass::EnsureBlas(ID3D12GraphicsCommandList4* commandList, ID3D12Device* device, Mesh* mesh) {
    if (mesh == nullptr) {
        return nullptr;
    }

    const auto cached = m_blasCache.find(mesh);
    if (cached != m_blasCache.end()) {
        return cached->second.blas.Get();
    }

    const D3D12_VERTEX_BUFFER_VIEW vbv = mesh->GetVertexView();
    const D3D12_INDEX_BUFFER_VIEW  ibv = mesh->GetIndexView();
    if (vbv.BufferLocation == 0 || ibv.BufferLocation == 0 || vbv.StrideInBytes == 0) {
        return nullptr;
    }


    // Acceleration-structure builds read the geometry as a shader resource, but
    // the mesh leaves its buffers in the states the rasteriser wants. Move them
    // across for the build and hand them straight back.
    ID3D12Resource* vertexResource = mesh->GetVertexBufferResource();
    ID3D12Resource* indexResource  = mesh->GetIndexBufferResource();
    if (vertexResource == nullptr || indexResource == nullptr) {
        return nullptr;
    }

    // Moved into a combined read state once and left there: the acceleration
    // build and the reflection shading both read this geometry, while the
    // rasteriser keeps using it as a vertex/index buffer. D3D12 allows a
    // resource to sit in several read states at once, which avoids a
    // transition pair per mesh per frame.
    const D3D12_RESOURCE_BARRIER toReadState[2] = {
        TransitionBarrier(vertexResource, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                          D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        TransitionBarrier(indexResource, D3D12_RESOURCE_STATE_INDEX_BUFFER,
                          D3D12_RESOURCE_STATE_INDEX_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
    };
    commandList->ResourceBarrier(2, toReadState);

    D3D12_RAYTRACING_GEOMETRY_DESC geometry = {};
    geometry.Type  = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.VertexBuffer.StartAddress  = vbv.BufferLocation;
    geometry.Triangles.VertexBuffer.StrideInBytes = vbv.StrideInBytes;
    geometry.Triangles.VertexCount  = vbv.SizeInBytes / vbv.StrideInBytes;
    geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT; // position is first in Vertex
    geometry.Triangles.IndexBuffer  = ibv.BufferLocation;
    geometry.Triangles.IndexCount   = ibv.SizeInBytes / sizeof(uint32_t);
    geometry.Triangles.IndexFormat  = DXGI_FORMAT_R32_UINT;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs       = 1;
    inputs.pGeometryDescs = &geometry;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild = {};
    m_device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);
    if (prebuild.ResultDataMaxSizeInBytes == 0) {
        return nullptr;
    }

    BlasEntry entry;
    entry.vertexBufferIndex = RegisterGeometryBuffer(device, vertexResource, vbv.SizeInBytes);
    entry.indexBufferIndex  = RegisterGeometryBuffer(device, indexResource, ibv.SizeInBytes);
    EnsureBufferCapacity(device, entry.scratch, prebuild.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    EnsureBufferCapacity(device, entry.blas, prebuild.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                         D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build = {};
    build.Inputs                          = inputs;
    build.ScratchAccelerationStructureData = entry.scratch->GetGPUVirtualAddress();
    build.DestAccelerationStructureData    = entry.blas->GetGPUVirtualAddress();
    commandList->BuildRaytracingAccelerationStructure(&build, 0, nullptr);

    const D3D12_RESOURCE_BARRIER barrier = UavBarrier(entry.blas.Get());
    commandList->ResourceBarrier(1, &barrier);

    ID3D12Resource* result = entry.blas.Get();
    m_blasCache.emplace(mesh, std::move(entry));
    return result;
}

void RaytracePass::BuildAccelerationStructures(ID3D12GraphicsCommandList* commandList, ID3D12Device* device,
                                               const std::vector<GameObject>& gameObjects, UINT frameIndex) {
    m_instanceCount = 0;
    m_activeFrame = frameIndex % kFrameCount;
    if (!m_available) {
        return;
    }

    // Acceleration-structure builds live on ID3D12GraphicsCommandList4, which the
    // renderer's list supports whenever the device reports DXR.
    ComPtr<ID3D12GraphicsCommandList4> dxrCommandList;
    if (FAILED(commandList->QueryInterface(IID_PPV_ARGS(&dxrCommandList)))) {
        return;
    }

    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances;
    std::vector<InstanceShading> shading;
    instances.reserve(gameObjects.size());
    shading.reserve(gameObjects.size());

    for (const GameObject& object : gameObjects) {
        if (!object.enabled || object.type != ObjectType::Mesh) {
            continue;
        }
        Mesh* mesh = (object.asset != nullptr) ? object.asset->mesh : nullptr;
        ID3D12Resource* blas = EnsureBlas(dxrCommandList.Get(), device, mesh);
        if (blas == nullptr) {
            continue;
        }

        XMFLOAT4X4 transform;
        XMStoreFloat4x4(&transform, XMMatrixTranspose(ComposeWorldMatrix(object)));

        D3D12_RAYTRACING_INSTANCE_DESC desc = {};
        memcpy(desc.Transform, transform.m, sizeof(desc.Transform)); // 3x4 row-major
        desc.InstanceID                          = static_cast<UINT>(instances.size());
        desc.InstanceMask                        = 0xFF;
        desc.InstanceContributionToHitGroupIndex = 0;
        desc.Flags                               = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        desc.AccelerationStructure               = blas->GetGPUVirtualAddress();
        instances.push_back(desc);

        const auto cachedBlas = m_blasCache.find(mesh);
        InstanceShading entry = {};
        entry.baseColor    = {object.color.x, object.color.y, object.color.z};
        entry.reflectivity = 1.0f;
        entry.vertexBufferIndex = (cachedBlas != m_blasCache.end()) ? cachedBlas->second.vertexBufferIndex : 0;
        entry.indexBufferIndex  = (cachedBlas != m_blasCache.end()) ? cachedBlas->second.indexBufferIndex : 0;
        shading.push_back(entry);
    }

    if (instances.empty()) {
        return;
    }

    const UINT64 instanceBytes = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instances.size();
    const UINT64 shadingBytes  = sizeof(InstanceShading) * shading.size();
    EnsureBufferCapacity(device, m_instanceDescs[m_activeFrame], instanceBytes, D3D12_HEAP_TYPE_UPLOAD,
                         D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
    EnsureBufferCapacity(device, m_instanceShading[m_activeFrame], shadingBytes, D3D12_HEAP_TYPE_UPLOAD,
                         D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);

    void* mapped = nullptr;
    if (SUCCEEDED(m_instanceDescs[m_activeFrame]->Map(0, nullptr, &mapped)) && mapped != nullptr) {
        memcpy(mapped, instances.data(), static_cast<size_t>(instanceBytes));
        m_instanceDescs[m_activeFrame]->Unmap(0, nullptr);
    }
    if (SUCCEEDED(m_instanceShading[m_activeFrame]->Map(0, nullptr, &mapped)) && mapped != nullptr) {
        memcpy(mapped, shading.data(), static_cast<size_t>(shadingBytes));
        m_instanceShading[m_activeFrame]->Unmap(0, nullptr);
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout   = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags         = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs      = static_cast<UINT>(instances.size());
    inputs.InstanceDescs = m_instanceDescs[m_activeFrame]->GetGPUVirtualAddress();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild = {};
    m_device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);
    if (prebuild.ResultDataMaxSizeInBytes == 0) {
        return;
    }

    EnsureBufferCapacity(device, m_tlasScratch[m_activeFrame], prebuild.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    EnsureBufferCapacity(device, m_tlas[m_activeFrame], prebuild.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                         D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build = {};
    build.Inputs                           = inputs;
    build.ScratchAccelerationStructureData = m_tlasScratch[m_activeFrame]->GetGPUVirtualAddress();
    build.DestAccelerationStructureData    = m_tlas[m_activeFrame]->GetGPUVirtualAddress();
    dxrCommandList->BuildRaytracingAccelerationStructure(&build, 0, nullptr);

    const D3D12_RESOURCE_BARRIER barrier = UavBarrier(m_tlas[m_activeFrame].Get());
    commandList->ResourceBarrier(1, &barrier);

    m_instanceCount = static_cast<UINT>(instances.size());
}

void RaytracePass::Render(ID3D12GraphicsCommandList* commandList, ID3D12Device* device, const Camera& camera,
                          ID3D12Resource* depthBuffer, int width, int height, const TraceParams& params, UINT frameIndex) {
    m_tracedThisFrame = false;
    m_activeFrame = frameIndex % kFrameCount;
    if (!m_available || m_instanceCount == 0 || !m_tlas[m_activeFrame] || depthBuffer == nullptr ||
        width <= 0 || height <= 0 || !m_shadowMask || !m_reflections) {
        return;
    }

    // The depth SRV is refreshed every frame: the renderer recreates the depth
    // buffer on resize, so the descriptor cannot be cached across resizes.
    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv = {};
    depthSrv.Format                  = DXGI_FORMAT_R32_FLOAT;
    depthSrv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrv.Texture2D.MipLevels     = 1;
    device->CreateShaderResourceView(depthBuffer, &depthSrv, m_viewHeap->GetCPUDescriptorHandleForHeapStart());

    if (m_outputsAreSrv) {
        const D3D12_RESOURCE_BARRIER toUav[2] = {
            TransitionBarrier(m_shadowMask.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            TransitionBarrier(m_reflections.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        };
        commandList->ResourceBarrier(2, toUav);
        m_outputsAreSrv = false;
    }

    ID3D12DescriptorHeap* heaps[] = { m_viewHeap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetPipelineState(m_pipelineState.Get());
    commandList->SetComputeRootSignature(m_rootSignature.Get());

    const XMMATRIX viewProj = camera.GetViewMatrix() * camera.GetProjectionMatrix();
    XMVECTOR determinant;
    const XMMATRIX invViewProj = XMMatrixInverse(&determinant, viewProj);

    float constants[kRootConstantCount] = {};
    XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(constants), XMMatrixTranspose(invViewProj));

    const XMFLOAT3 cameraPosition = camera.GetPosition();
    constants[16] = cameraPosition.x;
    constants[17] = cameraPosition.y;
    constants[18] = cameraPosition.z;
    constants[19] = 0.0f;

    XMFLOAT3 lightDirection = params.lightDirection;
    const float lightLength = sqrtf(lightDirection.x * lightDirection.x +
                                    lightDirection.y * lightDirection.y +
                                    lightDirection.z * lightDirection.z);
    if (lightLength > 1e-5f) {
        lightDirection = {lightDirection.x / lightLength, lightDirection.y / lightLength, lightDirection.z / lightLength};
    } else {
        lightDirection = {0.0f, -1.0f, 0.0f};
    }
    constants[20] = lightDirection.x;
    constants[21] = lightDirection.y;
    constants[22] = lightDirection.z;
    constants[23] = params.surfaceBias;

    constants[24] = static_cast<float>(width);
    constants[25] = static_cast<float>(height);
    constants[26] = (std::max)(1.0f, params.reflectionDistance);
    constants[27] = params.reflectionIntensity;

    constants[28] = params.skyZenith.x;
    constants[29] = params.skyZenith.y;
    constants[30] = params.skyZenith.z;
    constants[31] = 0.0f;

    constants[32] = params.skyHorizon.x;
    constants[33] = params.skyHorizon.y;
    constants[34] = params.skyHorizon.z;
    constants[35] = 0.0f;

    constants[36] = (std::max)(0.01f, params.aoRadius);
    constants[37] = (std::max)(1.0f, params.aoSamples);
    constants[38] = params.aoStrength;
    constants[39] = params.aoEnabled ? 1.0f : 0.0f;
    m_lastAoSamples = static_cast<UINT>(constants[37]);

    constants[40] = (std::max)(0.0f, params.shadowSoftness);
    constants[41] = (std::max)(1.0f, params.shadowSamples);
    constants[42] = params.shadowsEnabled ? 1.0f : 0.0f;
    constants[43] = params.reflectionsEnabled ? 1.0f : 0.0f;
    m_lastShadowSamples = static_cast<UINT>(constants[41]);

    commandList->SetComputeRoot32BitConstants(0, kRootConstantCount, constants, 0);
    commandList->SetComputeRootShaderResourceView(1, m_tlas[m_activeFrame]->GetGPUVirtualAddress());
    commandList->SetComputeRootShaderResourceView(2, m_instanceShading[m_activeFrame]->GetGPUVirtualAddress());

    D3D12_GPU_DESCRIPTOR_HANDLE srvTable = m_viewHeap->GetGPUDescriptorHandleForHeapStart();
    commandList->SetComputeRootDescriptorTable(3, srvTable);

    D3D12_GPU_DESCRIPTOR_HANDLE uavTable = srvTable;
    uavTable.ptr += static_cast<UINT64>(m_viewDescriptorSize) * kDescriptorShadowUav;
    commandList->SetComputeRootDescriptorTable(4, uavTable);

    D3D12_GPU_DESCRIPTOR_HANDLE geometryTable = srvTable;
    geometryTable.ptr += static_cast<UINT64>(m_viewDescriptorSize) * kDescriptorFixedCount;
    commandList->SetComputeRootDescriptorTable(5, geometryTable);

    m_lastTraceWidth  = width;
    m_lastTraceHeight = height;

    // Pick up the timings the GPU wrote a couple of frames ago before issuing
    // this frame's pair; nothing here blocks on the GPU.
    if (m_timestampReadback && m_timestampFrequency != 0) {
        D3D12_RANGE readRange = {0, sizeof(UINT64) * 2};
        void* mapped = nullptr;
        if (SUCCEEDED(m_timestampReadback->Map(0, &readRange, &mapped)) && mapped != nullptr) {
            const UINT64* stamps = static_cast<const UINT64*>(mapped);
            if (stamps[1] > stamps[0]) {
                const double elapsed = static_cast<double>(stamps[1] - stamps[0]) /
                                       static_cast<double>(m_timestampFrequency);
                const float milliseconds = static_cast<float>(elapsed * 1000.0);
                m_gpuMilliseconds = (m_gpuMilliseconds <= 0.0f)
                    ? milliseconds
                    : (m_gpuMilliseconds * 0.9f + milliseconds * 0.1f);
            }
            const D3D12_RANGE writeRange = {0, 0};
            m_timestampReadback->Unmap(0, &writeRange);
        }
    }

    if (m_timestampHeap) {
        commandList->EndQuery(m_timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    }

    commandList->Dispatch((width + 7) / 8, (height + 7) / 8, 1);

    if (m_timestampHeap && m_timestampReadback) {
        commandList->EndQuery(m_timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        commandList->ResolveQueryData(m_timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
                                      m_timestampReadback.Get(), 0);
    }

    const D3D12_RESOURCE_BARRIER toSrv[2] = {
        TransitionBarrier(m_shadowMask.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        TransitionBarrier(m_reflections.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
    };
    commandList->ResourceBarrier(2, toSrv);
    m_outputsAreSrv   = true;
    m_tracedThisFrame = true;
}

uint32_t RaytracePass::RegisterGeometryBuffer(ID3D12Device* device, ID3D12Resource* buffer, UINT byteSize) {
    if (buffer == nullptr || byteSize < 4 || m_geometrySlotsUsed >= kMaxGeometryMeshes * 2) {
        return 0;
    }

    const UINT slot = m_geometrySlotsUsed++;
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_viewHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(m_viewDescriptorSize) * (kDescriptorFixedCount + slot);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format                     = DXGI_FORMAT_R32_TYPELESS;
    srv.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.FirstElement        = 0;
    srv.Buffer.NumElements         = byteSize / 4;
    srv.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_RAW;
    device->CreateShaderResourceView(buffer, &srv, handle);
    return slot;
}

void RaytracePass::ReleaseMeshGeometry(Mesh* mesh) {
    m_blasCache.erase(mesh);
}

void RaytracePass::ReleaseAllGeometry() {
    m_blasCache.clear();
    m_instanceCount = 0;
}

void RaytracePass::Shutdown() {
    ReleaseAllGeometry();
    for (UINT frame = 0; frame < kFrameCount; ++frame) {
        m_tlas[frame].Reset();
        m_tlasScratch[frame].Reset();
        m_instanceDescs[frame].Reset();
        m_instanceShading[frame].Reset();
    }
    m_gbuffer.Reset();
    m_positions.Reset();
    m_shadowMask.Reset();
    m_reflections.Reset();
    m_available = false;
}
