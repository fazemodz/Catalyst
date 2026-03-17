#include "DXRenderer.h"
#include <d3dcompiler.h>
#include <iostream>
#include <DirectXCollision.h> 
#include <cmath>
#include "PrimitiveGenerator.h"

using namespace DirectX;
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

extern InputManager* g_InputManager;

void DXRenderer::Initialize(HWND hwnd, int width, int height) {
    m_hwnd = hwnd;
    m_width = width; 
    m_height = height;

    ComPtr<IDXGIFactory4> factory; ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
    }
#endif

    ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));

    D3D12_COMMAND_QUEUE_DESC qDesc = {}; qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(m_device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&m_commandQueue)));

    DXGI_SWAP_CHAIN_DESC1 scDesc = {}; 
    scDesc.Width = width; 
    scDesc.Height = height; 
    scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; 
    scDesc.SampleDesc = { 1, 0 }; 
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; 
    scDesc.BufferCount = FrameCount; 
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> sc1; 
    ThrowIfFailed(factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &scDesc, nullptr, nullptr, &sc1));
    sc1.As(&m_swapChain); m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

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
    CreateConstantBuffer(); 
    
    m_bindlessManager.Initialize(m_device.Get(), 1024);
    CreateDefaultTextures(); 

    m_shadowPass.Initialize(m_device.Get()); 
    m_postProcessPass.Initialize(m_device.Get(), width, height);
    m_quantaMeshPass.Initialize(m_device.Get());

    m_uiRenderer.Initialize(m_device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);

    m_fontManager.Initialize(m_device.Get(), m_commandQueue.Get(), "C:\\Windows\\Fonts\\arial.ttf", 24.0f);
    m_fontManager.SetBindlessIndex(m_bindlessManager.AddTexture(m_device.Get(), m_fontManager.GetTextureAtlas()));

    m_uiContext.Initialize(&m_uiDrawList, &m_fontManager, g_InputManager);

    m_commandAllocator->Reset();
    m_commandList->Reset(m_commandAllocator.Get(), nullptr);
    
    MeshData cubeData = PrimitiveGenerator::CreateCube(1.0f, 1.0f, 1.0f);
    m_primitives["Cube"] = new Mesh(m_device.Get(), m_commandList.Get(), cubeData.Vertices.data(), cubeData.Vertices.size(), cubeData.Indices.data(), cubeData.Indices.size());

    MeshData sphereData = PrimitiveGenerator::CreateSphere(0.5f, 32, 32);
    m_primitives["Sphere"] = new Mesh(m_device.Get(), m_commandList.Get(), sphereData.Vertices.data(), sphereData.Vertices.size(), sphereData.Indices.data(), sphereData.Indices.size());

    MeshData planeData = PrimitiveGenerator::CreatePlane(10.0f, 10.0f);
    m_primitives["Plane"] = new Mesh(m_device.Get(), m_commandList.Get(), planeData.Vertices.data(), planeData.Vertices.size(), planeData.Indices.data(), planeData.Indices.size());

    MeshData cylinderData = PrimitiveGenerator::CreateCylinder(0.5f, 1.0f, 32);
    m_primitives["Cylinder"] = new Mesh(m_device.Get(), m_commandList.Get(), cylinderData.Vertices.data(), cylinderData.Vertices.size(), cylinderData.Indices.data(), cylinderData.Indices.size());

    m_commandList->Close();
    ID3D12CommandList* uploadLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, uploadLists);
    FlushGPU();

    auto cubeAsset = std::make_shared<Asset>(); cubeAsset->id = 0; cubeAsset->name = "Basic Cube"; cubeAsset->mesh = m_primitives["Cube"]; m_assets.push_back(cubeAsset);
    auto sphereAsset = std::make_shared<Asset>(); sphereAsset->id = 1; sphereAsset->name = "Basic Sphere"; sphereAsset->mesh = m_primitives["Sphere"]; m_assets.push_back(sphereAsset);
    auto planeAsset = std::make_shared<Asset>(); planeAsset->id = 2; planeAsset->name = "Basic Plane"; planeAsset->mesh = m_primitives["Plane"]; m_assets.push_back(planeAsset);
    auto cylinderAsset = std::make_shared<Asset>(); cylinderAsset->id = 3; cylinderAsset->name = "Basic Cylinder"; cylinderAsset->mesh = m_primitives["Cylinder"]; m_assets.push_back(cylinderAsset);

    m_camera.SetProjection(45.0f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 5000.0f);

    GameObject floor; floor.name = "Floor"; floor.position = {0, -3.0f, 15.0f}; floor.scale = {10,1,10}; floor.color = {0.3f, 0.3f, 0.3f, 1}; floor.asset = m_assets[2].get(); m_gameObjects.push_back(floor);
}

void DXRenderer::OnResize(int width, int height) {
    if (width == 0 || height == 0) return;
    FlushGPU();
    m_width = width;
    m_height = height;

    for (int i = 0; i < FrameCount; ++i) m_renderTargets[i].Reset();
    m_depthBuffer.Reset();

    ThrowIfFailed(m_swapChain->ResizeBuffers(FrameCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0));
    m_frameIndex = 0;

    uint32_t rtvSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < FrameCount; i++) {
        ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])));
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += rtvSize;
    }

    CreateDepthBuffer();
    m_postProcessPass.OnResize(m_device.Get(), width, height);
    m_camera.SetProjection(45.0f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 5000.0f);
}

void DXRenderer::Render() {
    if (m_engineState == EngineState::Launcher) {
        float w = (float)m_width;
        float h = (float)m_height;
        float leftW = 260.0f;
        float rightW = 320.0f;
        float botH = 80.0f;
        uint32_t unrealBlue = 0xFFD77800; 

        m_uiDrawList.AddRectFilled(0, 0, leftW, h - botH, 0xFF111111);
        m_uiDrawList.AddRectFilled(leftW, 0, w - leftW - rightW, h - botH, 0xFF1E1E1E);
        m_uiDrawList.AddRectFilled(w - rightW, 0, rightW, h - botH, 0xFF151515);
        m_uiDrawList.AddRectFilled(0, h - botH, w, botH, 0xFF222222);

        static int activeCategory = 0; // 0 = New Project, 1 = Load
        static std::string inputProjectName = "MyCatalystProject";
        static bool isInputActive = false;
        static std::vector<std::wstring> recentProjects;
        static bool recentsLoaded = false;

        if (!recentsLoaded) {
            recentProjects = GetRecentProjects();
            recentsLoaded = true;
        }

        m_uiDrawList.AddText(m_fontManager, "PROJECT CATEGORIES", 20.0f, 20.0f, 0xFF888888);
        
        // New Project Tab
        if (activeCategory == 0) {
            m_uiDrawList.AddRectFilled(0, 60, leftW, 50, 0xFF2A2A2A);
            m_uiDrawList.AddRectFilled(0, 60, 4, 50, unrealBlue);
        }
        if (m_uiContext.Button("New Project", 0, 60, leftW, 50, 0x00000000, 0x22FFFFFF, 0x44FFFFFF)) activeCategory = 0;

        // Load Tab
        if (activeCategory == 1) {
            m_uiDrawList.AddRectFilled(0, 110, leftW, 50, 0xFF2A2A2A);
            m_uiDrawList.AddRectFilled(0, 110, 4, 50, unrealBlue);
        }
        if (m_uiContext.Button("Load Project", 0, 110, leftW, 50, 0x00000000, 0x22FFFFFF, 0x44FFFFFF)) activeCategory = 1;

        if (activeCategory == 0) {
            // NEW PROJECT VIEW
            m_uiDrawList.AddText(m_fontManager, "New Project Templates", leftW + 30.0f, 20.0f, 0xFFFFFFFF);
            
            float tX = leftW + 30.0f; float tY = 80.0f;
            m_uiDrawList.AddRectFilled(tX - 4, tY - 4, 128, 148, unrealBlue);
            m_uiDrawList.AddRectFilled(tX, tY, 120, 140, 0xFF2A2A2A);
            m_uiDrawList.AddText(m_fontManager, "Blank", tX + 25, tY + 110, 0xFFFFFFFF);
            
            m_uiDrawList.AddText(m_fontManager, "Blank", w - rightW + 20.0f, 20.0f, 0xFFFFFFFF);
            m_uiDrawList.AddText(m_fontManager, "A clean empty project with no code. Start from scratch.", w - rightW + 20.0f, 60.0f, 0xFFAAAAAA, rightW - 40.0f);

            // TEXT INPUT WIDGET
            m_uiDrawList.AddText(m_fontManager, "Project Name:", 30.0f, h - botH + 25.0f, 0xFFFFFFFF);
            m_uiContext.TextInput("ProjNameInput", inputProjectName, 210.0f, h - botH + 15.0f, 300.0f, 50.0f, isInputActive);

            if (m_uiContext.Button("Cancel", w - 240, h - botH + 20, 100, 40, 0xFF333333, 0xFF444444, 0xFF222222)) PostQuitMessage(0);
            
            if (m_uiContext.Button("Create", w - 130, h - botH + 20, 100, 40, unrealBlue, 0xFFFF9020, 0xFFB05000)) {
                std::wstring folder = BrowseForProjectFolder(m_hwnd);
                if (!folder.empty() && !inputProjectName.empty()) { 
                    CreateNewProject(folder, inputProjectName); 
                    m_engineState = EngineState::Editor; 
                    SetWindowText(m_hwnd, L"Catalyst Engine - Editor");
                    recentsLoaded = false;
                }
            }
        } else {
            // LOAD PROJECT VIEW
            m_uiDrawList.AddText(m_fontManager, "Recent Projects", leftW + 30.0f, 20.0f, 0xFFFFFFFF);
            float py = 80.0f;
            for (const auto& proj : recentProjects) {
                std::string projStr(proj.begin(), proj.end()); // simple wstring to string
                m_uiDrawList.AddRectFilled(leftW + 30.0f, py, w - leftW - rightW - 60.0f, 40.0f, 0xFF2A2A2A);
                m_uiDrawList.AddText(m_fontManager, projStr, leftW + 40.0f, py + 25.0f, 0xFFFFFFFF);
                
                if (m_uiContext.Button("Load_" + projStr, leftW + 30.0f, py, w - leftW - rightW - 60.0f, 40.0f, 0x00000000, 0x22FFFFFF, 0x44FFFFFF)) {
                    AddRecentProject(proj); // Move to top
                    recentsLoaded = false;
                    m_engineState = EngineState::Editor;
                    SetWindowText(m_hwnd, L"Catalyst Engine - Editor");
                }
                py += 50.0f;
            }

            if (m_uiContext.Button("Cancel", w - 240, h - botH + 20, 100, 40, 0xFF333333, 0xFF444444, 0xFF222222)) PostQuitMessage(0);
            
            if (m_uiContext.Button("Browse...", w - 130, h - botH + 20, 100, 40, unrealBlue, 0xFFFF9020, 0xFFB05000)) {
                std::wstring file = BrowseForProjectFile(m_hwnd);
                if (!file.empty()) {
                    AddRecentProject(file);
                    recentsLoaded = false;
                    m_engineState = EngineState::Editor; 
                    SetWindowText(m_hwnd, L"Catalyst Engine - Editor");
                }
            }
        }
    } else {
        // --- EDITOR DRAWING CODE (Unchanged) ---
        m_camera.Update(0.016f); 
        m_uiDrawList.AddRectFilled(10.0f, 10.0f, 280.0f, 200.0f, 0xAA000000);
        m_uiDrawList.AddRectFilled(10.0f, 10.0f, 280.0f, 30.0f, 0xFF444444);
        m_uiDrawList.AddText(m_fontManager, "Catalyst Control Panel", 20.0f, 25.0f, 0xFFFFFFFF);

        static float colorValue = 0.3f;
        if (m_uiContext.Button("Spawn Cube", 20.0f, 60.0f, 260.0f, 40.0f)) {
            GameObject newCube;
            newCube.name = "UI Spawned Cube";
            newCube.position = {0, 5.0f, 10.0f};
            newCube.scale = {1,1,1};
            newCube.color = {1.0f, 0.0f, 0.0f, 1.0f};
            newCube.asset = m_assets[0].get();
            m_gameObjects.push_back(newCube);
        }
        if (m_uiContext.Button("Change Floor Color", 20.0f, 110.0f, 260.0f, 40.0f)) {
            colorValue += 0.2f;
            if (colorValue > 1.0f) colorValue = 0.1f;
            if (!m_gameObjects.empty()) m_gameObjects[0].color = {colorValue, colorValue, colorValue, 1.0f};
        }
    }

    // --- RENDER EXECUTION (Unchanged) ---
    m_commandAllocator->Reset(); 
    m_commandList->Reset(m_commandAllocator.Get(), nullptr);

    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f };
    D3D12_RECT scissorRect = { 0, 0, m_width, m_height };
    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissorRect);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += m_frameIndex * m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_commandList->ResourceBarrier(1, &barrier);

    const float clearColor[] = { 0.1f, 0.1f, 0.1f, 1.0f };
    m_commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    ID3D12DescriptorHeap* heaps[] = { m_bindlessManager.GetHeap() }; 
    m_commandList->SetDescriptorHeaps(1, heaps);

    if (m_engineState == EngineState::Editor) {
        XMMATRIX lightSpace = XMMatrixIdentity();
        XMFLOAT3 lightDir = {0, -1, 0};
        m_quantaMeshPass.Render(m_commandList.Get(), m_device.Get(), m_gameObjects, m_camera, m_width, m_height, 
                                lightSpace, lightDir, 1.0f, nullptr, m_bindlessManager.GetHeap(), 
                                m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV), 
                                m_frameHeapOffset, m_texWhite, m_texNormal, m_texBlack, nullptr, m_primitives["Sphere"]);
    }

    m_uiRenderer.Render(m_commandList.Get(), m_uiDrawList, (float)m_width, (float)m_height, m_bindlessManager.GetHeap());
    m_uiDrawList.Clear();

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

void DXRenderer::CreateDefaultTextures() { 
    m_texWhite = new Texture(); m_texWhite->Create1x1Color(m_device.Get(), m_commandQueue.Get(), 0xFFFFFFFF); 
    m_texWhite->SetBindlessIndex(m_bindlessManager.AddTexture(m_device.Get(), m_texWhite->GetResource()));
    m_texBlack = new Texture(); m_texBlack->Create1x1Color(m_device.Get(), m_commandQueue.Get(), 0xFF000000); 
    m_texBlack->SetBindlessIndex(m_bindlessManager.AddTexture(m_device.Get(), m_texBlack->GetResource()));
    m_texNormal = new Texture(); m_texNormal->Create1x1Color(m_device.Get(), m_commandQueue.Get(), 0xFFFF7F7F); 
    m_texNormal->SetBindlessIndex(m_bindlessManager.AddTexture(m_device.Get(), m_texNormal->GetResource()));
}

void DXRenderer::CreateDepthBuffer() { 
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = { D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE }; 
    ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap))); 
    D3D12_RESOURCE_DESC depthDesc = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, (UINT64)m_width, (UINT)m_height, 1, 1, DXGI_FORMAT_D32_FLOAT, {1,0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL }; 
    D3D12_CLEAR_VALUE clearVal = { DXGI_FORMAT_D32_FLOAT }; clearVal.DepthStencil.Depth = 1.0f; 
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT }; 
    ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearVal, IID_PPV_ARGS(&m_depthBuffer))); 
    m_device->CreateDepthStencilView(m_depthBuffer.Get(), nullptr, m_dsvHeap->GetCPUDescriptorHandleForHeapStart()); 
}

void DXRenderer::CreateConstantBuffer() { 
    const UINT bufferSize = (sizeof(ConstantBufferData) + 255) & ~255; 
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_UPLOAD }; 
    D3D12_RESOURCE_DESC rd = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, (UINT64)bufferSize * 1000, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1,0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE }; 
    ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_constantBuffer))); 
    m_constantBuffer->Map(0, nullptr, (void**)&m_pCbvDataBegin); 
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
    m_uiRenderer.Shutdown();
    for (auto& pair : m_primitives) delete pair.second; 
    delete m_texWhite; delete m_texBlack; delete m_texNormal; 
}