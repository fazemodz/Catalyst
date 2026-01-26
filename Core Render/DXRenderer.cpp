#include "DXRenderer.h"
#include <d3dcompiler.h>

using namespace DirectX;

void DXRenderer::Initialize(HWND hwnd) {
#ifdef _DEBUG
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));

    D3D12_COMMAND_QUEUE_DESC qDesc = { D3D12_COMMAND_LIST_TYPE_DIRECT };
    ThrowIfFailed(m_device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&m_commandQueue)));

    DXGI_SWAP_CHAIN_DESC1 scDesc = { 1280, 720, DXGI_FORMAT_R8G8B8A8_UNORM, FALSE, {1,0}, DXGI_USAGE_RENDER_TARGET_OUTPUT, FrameCount, DXGI_SCALING_STRETCH, DXGI_SWAP_EFFECT_FLIP_DISCARD };
    ComPtr<IDXGISwapChain1> sc1;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &scDesc, nullptr, nullptr, &sc1));
    sc1.As(&m_swapChain);
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FrameCount };
    ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap)));

    uint32_t rtvSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < FrameCount; i++) {
        ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])));
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += rtvSize;
    }

    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
    m_commandList->Close();

    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    CreateGraphicsPipeline();
    CreateCubeMesh(); // Build the Cube
    CreateConstantBuffer();
}

void DXRenderer::CreateGraphicsPipeline() {
    ComPtr<ID3DBlob> vs, ps, err;
    D3DCompileFromFile(L"Shaders/shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", D3DCOMPILE_DEBUG, 0, &vs, &err);
    if (err) OutputDebugStringA((char*)err->GetBufferPointer());
    D3DCompileFromFile(L"Shaders/shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", D3DCOMPILE_DEBUG, 0, &ps, nullptr);

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_ROOT_PARAMETER rp = {};
    rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp.Descriptor.ShaderRegister = 0;
    rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = { 1, &rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };
    ComPtr<ID3DBlob> sig;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, nullptr);
    m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.InputLayout = { layout, 2 };
    pso.pRootSignature = m_rootSignature.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_BACK; // Culling back faces
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;
    m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pipelineState));
}

void DXRenderer::CreateCubeMesh() {
    // 1. Define 8 Vertices (Cube Corners)
    Vertex verts[] = {
        { { -0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f, 1.0f } }, // 0: Red
        { { -0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f, 1.0f } }, // 1: Green
        { {  0.5f,  0.5f, -0.5f }, { 0.0f, 0.0f, 1.0f, 1.0f } }, // 2: Blue
        { {  0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f, 0.0f, 1.0f } }, // 3: Yellow
        { { -0.5f, -0.5f,  0.5f }, { 0.0f, 1.0f, 1.0f, 1.0f } }, // 4: Cyan
        { { -0.5f,  0.5f,  0.5f }, { 1.0f, 0.0f, 1.0f, 1.0f } }, // 5: Magenta
        { {  0.5f,  0.5f,  0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f } }, // 6: White
        { {  0.5f, -0.5f,  0.5f }, { 0.0f, 0.0f, 0.0f, 1.0f } }  // 7: Black
    };

    // 2. Define 36 Indices (12 Triangles)
    // Note: Clockwise winding order for Front faces
    uint16_t indices[] = {
        // Front Face
        0, 1, 2,  0, 2, 3,
        // Back Face
        4, 6, 5,  4, 7, 6,
        // Left Face
        4, 5, 1,  4, 1, 0,
        // Right Face
        3, 2, 6,  3, 6, 7,
        // Top Face
        1, 5, 6,  1, 6, 2,
        // Bottom Face
        4, 0, 3,  4, 3, 7
    };

    const UINT vBufferSize = sizeof(verts);
    const UINT iBufferSize = sizeof(indices);

    // 3. Create Vertex Buffer
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC vDesc = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, vBufferSize, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1,0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &vDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_vertexBuffer)));
    
    UINT8* pVertexData;
    m_vertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pVertexData));
    memcpy(pVertexData, verts, vBufferSize);
    m_vertexBuffer->Unmap(0, nullptr);
    m_vertexBufferView = { m_vertexBuffer->GetGPUVirtualAddress(), vBufferSize, sizeof(Vertex) };

    // 4. Create Index Buffer (Same process, separate resource)
    D3D12_RESOURCE_DESC iDesc = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, iBufferSize, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1,0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &iDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_indexBuffer)));

    UINT8* pIndexData;
    m_indexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pIndexData));
    memcpy(pIndexData, indices, iBufferSize);
    m_indexBuffer->Unmap(0, nullptr);

    // Initialize Index Buffer View
    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R16_UINT; // We used uint16_t
    m_indexBufferView.SizeInBytes = iBufferSize;
}

void DXRenderer::CreateConstantBuffer() {
    UINT size = (sizeof(ConstantBufferData) + 255) & ~255;
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC rd = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, size, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1,0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_constantBuffer));
    m_constantBuffer->Map(0, nullptr, (void**)&m_pCbvDataBegin);
}

void DXRenderer::Render() {
    // 1. INPUT & CAMERA
    if (g_RightMouseDown) {
        float sensitivity = 0.002f;
        m_yaw += (float)g_MouseDeltaX * sensitivity;
        m_pitch += (float)g_MouseDeltaY * sensitivity;
        m_pitch = XMMax(-XM_PIDIV2 + 0.1f, XMMin(XM_PIDIV2 - 0.1f, m_pitch));
        g_MouseDeltaX = 0; g_MouseDeltaY = 0;
    }

    XMMATRIX rotMat = XMMatrixRotationRollPitchYaw(m_pitch, m_yaw, 0.0f);
    XMVECTOR forward = XMVector3TransformNormal(XMVectorSet(0, 0, 1, 0), rotMat);
    XMVECTOR right = XMVector3TransformNormal(XMVectorSet(1, 0, 0, 0), rotMat);
    XMVECTOR camPos = XMLoadFloat3(&m_cameraPos);

    float speed = 0.02f;
    if (g_RightMouseDown) {
        if (g_Keys['W']) camPos += forward * speed;
        if (g_Keys['S']) camPos -= forward * speed;
        if (g_Keys['A']) camPos -= right * speed;
        if (g_Keys['D']) camPos += right * speed;
        XMStoreFloat3(&m_cameraPos, camPos);
    }

    XMMATRIX mView = XMMatrixLookToLH(camPos, forward, XMVectorSet(0, 1, 0, 0));
    XMMATRIX mProj = XMMatrixPerspectiveFovLH(XMConvertToRadians(45.0f), 1280.0f / 720.0f, 0.1f, 100.0f);
    m_cbData.wvpMatrix = XMMatrixTranspose(XMMatrixIdentity() * mView * mProj);
    memcpy(m_pCbvDataBegin, &m_cbData, sizeof(m_cbData));

    // 2. RENDER COMMANDS
    m_commandAllocator->Reset();
    m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get());
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, 1280, 720 };
    m_commandList->RSSetViewports(1, &vp);
    m_commandList->RSSetScissorRects(1, &sc);

    D3D12_RESOURCE_BARRIER barrier = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION };
    barrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_commandList->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += m_frameIndex * m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    
    const float clear[] = { 0.1f, 0.1f, 0.1f, 1.0f };
    m_commandList->ClearRenderTargetView(rtv, clear, 0, nullptr);

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    // Bind Index Buffer
    m_commandList->IASetIndexBuffer(&m_indexBufferView);
    // Draw using Indices (36 indices = 12 triangles)
    m_commandList->DrawIndexedInstanced(36, 1, 0, 0, 0);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    m_commandList->ResourceBarrier(1, &barrier);
    m_commandList->Close();

    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);
    m_swapChain->Present(1, 0);
    FlushGPU();
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void DXRenderer::FlushGPU() {
    m_fenceValue++;
    m_commandQueue->Signal(m_fence.Get(), m_fenceValue);
    if (m_fence->GetCompletedValue() < m_fenceValue) {
        m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void DXRenderer::Shutdown() { 
    FlushGPU(); 
    if (m_fenceEvent) CloseHandle(m_fenceEvent); 
}