#include "DXRenderer.h"
#include <d3dcompiler.h>
#include <stdexcept>

using namespace DirectX;

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

void DXRenderer::Initialize(HWND hwnd, int width, int height) {
    m_width = width;
    m_height = height;

#ifdef _DEBUG
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device));
    if (FAILED(hr)) throw std::runtime_error("Failed to create D3D12 Device");

    D3D12_COMMAND_QUEUE_DESC qDesc = {};
    qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    qDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    qDesc.NodeMask = 0;

    ThrowIfFailed(m_device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&m_commandQueue)));

    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.Width = width;
    scDesc.Height = height;
    scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.Stereo = FALSE;
    scDesc.SampleDesc = { 1, 0 };
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = FrameCount;
    scDesc.Scaling = DXGI_SCALING_STRETCH;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    scDesc.Flags = 0;

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

    CreateDepthBuffer();
    CreateGraphicsPipeline();
    CreateCubeMesh(); // Creates the Mesh object
    CreateConstantBuffer();

    m_ui.Initialize(hwnd, m_device.Get(), m_commandQueue.Get(), FrameCount);

    float aspectRatio = (float)width / (float)height;
    m_camera.SetProjection(45.0f, aspectRatio, 0.1f, 100.0f);

    // --- SCENE SETUP WITH MESHES ---
    // Note: We pass m_cubeMesh to the objects now
    m_gameObjects.push_back({ "Floor",   {0, -1.0f, 0}, {0,0,0}, {10, 0.2f, 10}, {0.3f, 0.3f, 0.3f, 1}, m_cubeMesh });
    m_gameObjects.push_back({ "Cube A",  {0, 0, 0},     {0,0,0}, {1,1,1},        {1, 0, 0, 1},       m_cubeMesh });
    m_gameObjects.push_back({ "Cube B",  {2, 0, 0},     {0,0,0}, {1,1,1},        {0, 1, 0, 1},       m_cubeMesh });
}

void DXRenderer::Render() {
    m_camera.Update();

    if (ImGui::IsMouseClicked(0)) {
        if (!ImGui::GetIO().WantCaptureMouse) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(GetActiveWindow(), &pt);
            PickObject(pt.x, pt.y);
        }
    }

    XMMATRIX mView = m_camera.GetViewMatrix();
    XMMATRIX mProj = m_camera.GetProjectionMatrix();

    m_ui.Update(m_gameObjects, m_selectedObjectIndex, mView, mProj);
    
    m_commandAllocator->Reset();
    m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get());
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)m_width, (float)m_height, 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, m_width, m_height };
    m_commandList->RSSetViewports(1, &vp);
    m_commandList->RSSetScissorRects(1, &sc);

    D3D12_RESOURCE_BARRIER barrier = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION };
    barrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_commandList->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += m_frameIndex * m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    
    const float clear[] = { 0.1f, 0.1f, 0.1f, 1.0f };
    m_commandList->ClearRenderTargetView(rtv, clear, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    UINT objSize = (sizeof(ConstantBufferData) + 255) & ~255;
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = m_constantBuffer->GetGPUVirtualAddress();

    for (int i = 0; i < m_gameObjects.size() && i < MAX_OBJECTS; i++) {
        GameObject& obj = m_gameObjects[i];
        
        // Skip if this object has no mesh assigned
        if (!obj.mesh) continue;

        // --- BIND MESH BUFFERS ---
        D3D12_VERTEX_BUFFER_VIEW vbv = obj.mesh->GetVertexView();
        D3D12_INDEX_BUFFER_VIEW ibv = obj.mesh->GetIndexView();
        m_commandList->IASetVertexBuffers(0, 1, &vbv);
        m_commandList->IASetIndexBuffer(&ibv);

        XMMATRIX mScale = XMMatrixScaling(obj.scale.x, obj.scale.y, obj.scale.z);
        XMMATRIX mRot = XMMatrixRotationRollPitchYaw(obj.rotation.x, obj.rotation.y, obj.rotation.z);
        XMMATRIX mTrans = XMMatrixTranslation(obj.position.x, obj.position.y, obj.position.z);
        XMMATRIX mWorld = mScale * mRot * mTrans;
        XMMATRIX wvp = XMMatrixTranspose(mWorld * mView * mProj);

        ConstantBufferData cbData;
        cbData.wvpMatrix = wvp;
        cbData.colorOverride = obj.color;
        memcpy(m_pCbvDataBegin + (i * objSize), &cbData, sizeof(cbData));

        m_commandList->SetGraphicsRootConstantBufferView(0, cbAddress + (i * objSize));
        
        // DRAW USING MESH COUNT
        m_commandList->DrawIndexedInstanced(obj.mesh->GetIndexCount(), 1, 0, 0, 0);
    }

    m_ui.Draw(m_commandList.Get());

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

void DXRenderer::PickObject(int mouseX, int mouseY) {
    XMMATRIX view = m_camera.GetViewMatrix();
    XMMATRIX proj = m_camera.GetProjectionMatrix();
    
    float ndcX = (2.0f * mouseX) / m_width - 1.0f;
    float ndcY = -((2.0f * mouseY) / m_height - 1.0f);

    XMVECTOR nearPoint = XMVectorSet(ndcX, ndcY, 0.0f, 1.0f);
    XMVECTOR farPoint  = XMVectorSet(ndcX, ndcY, 1.0f, 1.0f);

    XMVECTOR rayOrigin = XMVector3Unproject(nearPoint, 0, 0, m_width, m_height, 0.0f, 1.0f, proj, view, XMMatrixIdentity());
    XMVECTOR rayEnd    = XMVector3Unproject(farPoint,  0, 0, m_width, m_height, 0.0f, 1.0f, proj, view, XMMatrixIdentity());
    XMVECTOR rayDir    = XMVector3Normalize(rayEnd - rayOrigin);

    float closestDist = FLT_MAX;
    int hitIndex = -1;

    for (int i = 0; i < m_gameObjects.size(); i++) {
        GameObject& obj = m_gameObjects[i];
        BoundingOrientedBox obb;
        obb.Center = obj.position;
        obb.Extents = { obj.scale.x / 2.0f, obj.scale.y / 2.0f, obj.scale.z / 2.0f };
        XMVECTOR quat = XMQuaternionRotationRollPitchYaw(obj.rotation.x, obj.rotation.y, obj.rotation.z);
        XMStoreFloat4(&obb.Orientation, quat);

        float dist;
        if (obb.Intersects(rayOrigin, rayDir, dist)) {
            if (dist < closestDist) {
                closestDist = dist;
                hitIndex = i;
            }
        }
    }
    m_selectedObjectIndex = hitIndex;
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
    m_ui.Shutdown();
    delete m_cubeMesh; // Clean up the mesh
    if (m_fenceEvent) CloseHandle(m_fenceEvent);
}

void DXRenderer::CreateDepthBuffer() {
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap));
    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = m_width;
    depthDesc.Height = m_height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clearVal = {};
    clearVal.Format = DXGI_FORMAT_D32_FLOAT;
    clearVal.DepthStencil.Depth = 1.0f;
    clearVal.DepthStencil.Stencil = 0;
    D3D12_HEAP_PROPERTIES heapProps = { D3D12_HEAP_TYPE_DEFAULT };
    m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearVal, IID_PPV_ARGS(&m_depthBuffer));
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvViewDesc = {};
    dsvViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvViewDesc.Flags = D3D12_DSV_FLAG_NONE;
    m_device->CreateDepthStencilView(m_depthBuffer.Get(), &dsvViewDesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
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
    rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
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
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = TRUE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;
    m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pipelineState));
}

void DXRenderer::CreateCubeMesh() {
    // 1. Create Mesh Object
    m_cubeMesh = new Mesh();

    // 2. Define Cube Geometry
    std::vector<Vertex> verts = {
        { { -0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { -0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { {  0.5f,  0.5f, -0.5f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        { {  0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f, 0.0f, 1.0f } },
        { { -0.5f, -0.5f,  0.5f }, { 0.0f, 1.0f, 1.0f, 1.0f } },
        { { -0.5f,  0.5f,  0.5f }, { 1.0f, 0.0f, 1.0f, 1.0f } },
        { {  0.5f,  0.5f,  0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { {  0.5f, -0.5f,  0.5f }, { 0.0f, 0.0f, 0.0f, 1.0f } }
    };
    std::vector<uint16_t> indices = {
        0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 4, 5, 1, 4, 1, 0,
        3, 2, 6, 3, 6, 7, 1, 5, 6, 1, 6, 2, 4, 0, 3, 4, 3, 7
    };

    // 3. Upload to GPU
    m_cubeMesh->Initialize(m_device.Get(), verts, indices);
}

void DXRenderer::CreateConstantBuffer() {
    const UINT objSize = (sizeof(ConstantBufferData) + 255) & ~255;
    const UINT bufferSize = objSize * MAX_OBJECTS;
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC rd = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, bufferSize, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1,0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_constantBuffer));
    m_constantBuffer->Map(0, nullptr, (void**)&m_pCbvDataBegin);
}