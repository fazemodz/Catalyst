#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>
#include <map>
#include <vector>

#include "Camera.h"
#include "GameObject.h"
#include "Mesh.h"

using Microsoft::WRL::ComPtr;

// Hardware raytraced shadows and reflections using DXR 1.1 inline RayQuery.
//
// The pass is entirely opt-in: if the adapter does not report raytracing tier
// 1.1, or the precompiled shader is missing, IsAvailable() stays false and the
// renderer keeps using the pure raster path.
class RaytracePass {
public:
    static constexpr DXGI_FORMAT kGBufferFormat    = DXGI_FORMAT_R8G8B8A8_UNORM;
    static constexpr DXGI_FORMAT kPositionFormat   = DXGI_FORMAT_R32G32B32A32_FLOAT;
    static constexpr DXGI_FORMAT kShadowFormat     = DXGI_FORMAT_R8_UNORM;
    static constexpr DXGI_FORMAT kReflectionFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    bool Initialize(ID3D12Device* device);
    void OnResize(ID3D12Device* device, int width, int height);
    void Shutdown();

    // Timestamp ticks are queue-specific, so the renderer hands the frequency in.
    void SetTimestampFrequency(UINT64 frequency) { m_timestampFrequency = frequency; }

    bool IsAvailable() const { return m_available; }
    const std::wstring& GetUnavailableReason() const { return m_unavailableReason; }

    // --- G-buffer the raster pass writes alongside the HDR colour target -----
    D3D12_CPU_DESCRIPTOR_HANDLE GetGBufferRTV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetPositionRTV() const;
    ID3D12Resource* GetGBufferResource() const { return m_gbuffer.Get(); }
    void BeginGBuffer(ID3D12GraphicsCommandList* commandList);
    void EndGBuffer(ID3D12GraphicsCommandList* commandList);

    // --- acceleration structures -------------------------------------------
    // BLAS are cached per Mesh and reused; the TLAS is rebuilt every frame from
    // the current object transforms.
    void BuildAccelerationStructures(ID3D12GraphicsCommandList* commandList, ID3D12Device* device,
                                     const std::vector<GameObject>& gameObjects, UINT frameIndex);
    void ReleaseMeshGeometry(Mesh* mesh);
    void ReleaseAllGeometry();

    struct TraceParams {
        DirectX::XMFLOAT3 lightDirection = {0.0f, -1.0f, 0.0f};
        DirectX::XMFLOAT3 skyZenith      = {0.55f, 0.72f, 0.95f};
        DirectX::XMFLOAT3 skyHorizon     = {0.85f, 0.89f, 0.95f};
        float surfaceBias          = 0.02f;
        float reflectionDistance   = 250.0f;
        float reflectionIntensity  = 0.6f;
        float aoRadius             = 2.5f;
        float aoSamples            = 8.0f;
        float aoStrength           = 0.65f;
        float shadowSoftness       = 0.035f;
        float shadowSamples        = 8.0f;
        bool  shadowsEnabled       = true;
        bool  reflectionsEnabled   = true;
        bool  aoEnabled            = true;
    };

    // depthBuffer must already be readable by a shader; the caller owns its
    // state transitions because the depth target belongs to the renderer.
    void Render(ID3D12GraphicsCommandList* commandList, ID3D12Device* device, const Camera& camera,
                ID3D12Resource* depthBuffer, int width, int height, const TraceParams& params, UINT frameIndex);

    ID3D12Resource* GetShadowMask() const { return m_shadowMask.Get(); }
    ID3D12Resource* GetReflections() const { return m_reflections.Get(); }
    bool HasTracedThisFrame() const { return m_tracedThisFrame; }

    struct Stats {
        bool available = false;
        bool tracedThisFrame = false;
        UINT instanceCount = 0;
        UINT blasCount = 0;
        UINT traceWidth = 0;
        UINT traceHeight = 0;
        UINT aoSamples = 0;
        UINT shadowSamples = 0;
        // Measured on the GPU with timestamp queries, not inferred from CPU time.
        float gpuMilliseconds = 0.0f;
    };
    Stats GetStats() const {
        Stats stats;
        stats.available       = m_available;
        stats.tracedThisFrame = m_tracedThisFrame;
        stats.instanceCount   = m_instanceCount;
        stats.blasCount       = static_cast<UINT>(m_blasCache.size());
        stats.traceWidth      = static_cast<UINT>(m_lastTraceWidth);
        stats.traceHeight     = static_cast<UINT>(m_lastTraceHeight);
        stats.aoSamples       = m_lastAoSamples;
        stats.shadowSamples   = m_lastShadowSamples;
        stats.gpuMilliseconds = m_gpuMilliseconds;
        return stats;
    }

private:
    struct BlasEntry {
        ComPtr<ID3D12Resource> blas;
        ComPtr<ID3D12Resource> scratch;
        // Slots in the bindless geometry range, so a reflection hit can read the
        // triangle it landed on instead of guessing at its shading.
        uint32_t vertexBufferIndex = 0;
        uint32_t indexBufferIndex = 0;
    };

    struct InstanceShading {
        DirectX::XMFLOAT3 baseColor;
        float reflectivity;
        uint32_t vertexBufferIndex;   // into the pass's bindless geometry range
        uint32_t indexBufferIndex;
        uint32_t padding[2];
    };

    bool CreatePipeline(ID3D12Device* device);
    void CreateDescriptorHeaps(ID3D12Device* device);
    ID3D12Resource* EnsureBlas(ID3D12GraphicsCommandList4* commandList, ID3D12Device* device, Mesh* mesh);
    uint32_t RegisterGeometryBuffer(ID3D12Device* device, ID3D12Resource* buffer, UINT byteSize);
    void EnsureBufferCapacity(ID3D12Device* device, ComPtr<ID3D12Resource>& buffer, UINT64 requiredBytes,
                              D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES initialState,
                              D3D12_RESOURCE_FLAGS flags);

    ComPtr<ID3D12Device5> m_device5;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;

    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;      // G-buffer RTV
    ComPtr<ID3D12DescriptorHeap> m_viewHeap;     // depth SRV, gbuffer SRV, shadow UAV, reflection UAV
    UINT m_viewDescriptorSize = 0;
    UINT m_rtvDescriptorSize = 0;

    ComPtr<ID3D12Resource> m_gbuffer;
    ComPtr<ID3D12Resource> m_positions;
    ComPtr<ID3D12Resource> m_shadowMask;
    ComPtr<ID3D12Resource> m_reflections;

    std::map<Mesh*, BlasEntry> m_blasCache;
    // The TLAS is rebuilt every frame and the instance buffers are rewritten by
    // the CPU, so with frames in flight each needs its own copy: otherwise a
    // build overwrites what the previous frame is still tracing against.
    static constexpr UINT kFrameCount = 2;
    ComPtr<ID3D12Resource> m_tlas[kFrameCount];
    ComPtr<ID3D12Resource> m_tlasScratch[kFrameCount];
    ComPtr<ID3D12Resource> m_instanceDescs[kFrameCount];   // upload, D3D12_RAYTRACING_INSTANCE_DESC[]
    ComPtr<ID3D12Resource> m_instanceShading[kFrameCount]; // upload, InstanceShading[]
    UINT m_instanceCount = 0;
    UINT m_activeFrame = 0;
    UINT m_geometrySlotsUsed = 0;
    UINT m_lastAoSamples = 0;
    UINT m_lastShadowSamples = 0;

    // GPU timestamps around the dispatch. Read back two frames later so the
    // resolve has definitely completed without stalling the pipeline.
    ComPtr<ID3D12QueryHeap> m_timestampHeap;
    ComPtr<ID3D12Resource> m_timestampReadback;
    UINT64 m_timestampFrequency = 0;
    float m_gpuMilliseconds = 0.0f;

    int  m_width = 0;
    int  m_height = 0;
    int  m_lastTraceWidth = 0;
    int  m_lastTraceHeight = 0;
    bool m_available = false;
    bool m_outputsAreSrv = false;
    bool m_gbufferIsSrv = false;
    bool m_tracedThisFrame = false;
    std::wstring m_unavailableReason;
};
