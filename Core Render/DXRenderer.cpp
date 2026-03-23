#include "DXRenderer.h"
#include <d3dcompiler.h>
#include <iostream>
#include <DirectXCollision.h> 
#include <cmath>
#include <string>
#include <algorithm>
#include <vector>
#include <filesystem>
#include <windows.h>
#include "PrimitiveGenerator.h"

using namespace DirectX;
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

extern InputManager* g_InputManager;

// Global ID cache for the Skybox SRV in the Bindless Heap
static uint32_t g_skyboxSrvIndex = 0; 

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
    m_skyboxPass.Initialize(m_device.Get());

    m_uiRenderer.Initialize(m_device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);

    m_fontManager.Initialize(m_device.Get(), m_commandQueue.Get(), "C:\\Windows\\Fonts\\arial.ttf", 20.0f);
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

    // LOAD THE HDR FILE!
    m_skyboxPass.LoadHDR(m_device.Get(), m_commandList.Get(), L"sky.hdr");
    
    m_commandList->Close();
    ID3D12CommandList* uploadLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, uploadLists);
    FlushGPU();
    
    // Bind the successfully loaded HDR texture to the Bindless Heap
    if (m_skyboxPass.GetHDRResource()) {
        g_skyboxSrvIndex = m_bindlessManager.AddTexture(m_device.Get(), m_skyboxPass.GetHDRResource());
    }

    auto cubeAsset = std::make_shared<Asset>(); cubeAsset->id = 0; cubeAsset->name = "Basic Cube"; cubeAsset->mesh = m_primitives["Cube"]; m_assets.push_back(cubeAsset);
    auto sphereAsset = std::make_shared<Asset>(); sphereAsset->id = 1; sphereAsset->name = "Basic Sphere"; sphereAsset->mesh = m_primitives["Sphere"]; m_assets.push_back(sphereAsset);
    auto planeAsset = std::make_shared<Asset>(); planeAsset->id = 2; planeAsset->name = "Basic Plane"; planeAsset->mesh = m_primitives["Plane"]; m_assets.push_back(planeAsset);
    auto cylinderAsset = std::make_shared<Asset>(); cylinderAsset->id = 3; cylinderAsset->name = "Basic Cylinder"; cylinderAsset->mesh = m_primitives["Cylinder"]; m_assets.push_back(cylinderAsset);

    m_camera.SetProjection(45.0f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 5000.0f);

    GameObject floor; floor.name = "Floor"; floor.position = {0, -3.0f, 15.0f}; floor.scale = {10,1,10}; floor.color = {0.3f, 0.3f, 0.3f, 1}; floor.asset = m_assets[2].get(); m_gameObjects.push_back(floor);

    // Default Sky Actor
    GameObject skybox; skybox.name = "Sky Atmosphere"; skybox.position = {0, -9999.0f, 0}; skybox.scale = {1,1,1}; skybox.color = {1,1,1,1}; skybox.asset = m_assets[0].get(); m_gameObjects.push_back(skybox);
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
}

void DXRenderer::Render() {
    float topMenuH = 25.0f;
    float toolbarH = 40.0f;
    float topH = topMenuH + toolbarH;
    float rightW = 350.0f;
    float bottomH = 250.0f;

    static int selectedObj = -1; 
    static bool showPlaceActorsMenu = false;
    static bool deleteWasDown = false;
    static std::wstring currentProjectFolder = L"";
    static std::vector<std::string> discoveredAssets;
    static DWORD lastScanTime = 0;
    static int selectedContentAsset = -1;

    static bool showImportPopup = false;
    static std::wstring pendingImportPath = L"";
    static std::string pendingImportName = "";
    static bool isImportNameActive = false;
    static bool wasImportJustOpened = false;

    static bool triggerEditorSwap = false;
    static std::wstring editorSwapProjName = L"";

    if (m_engineState == EngineState::Launcher) {
        float w = (float)m_width;
        float h = (float)m_height;
        float l_leftW = 260.0f;
        float l_rightW = 320.0f;
        float l_botH = 80.0f;
        uint32_t unrealBlue = 0xFFD77800; 

        m_uiDrawList.AddRectFilled(0, 0, l_leftW, h - l_botH, 0xFF111111);
        m_uiDrawList.AddRectFilled(l_leftW, 0, w - l_leftW - l_rightW, h - l_botH, 0xFF1E1E1E);
        m_uiDrawList.AddRectFilled(w - l_rightW, 0, l_rightW, h - l_botH, 0xFF151515);
        m_uiDrawList.AddRectFilled(0, h - l_botH, w, l_botH, 0xFF222222);

        static int activeCategory = 0; 
        static std::string inputProjectName = "MyCatalystProject";
        static bool isInputActive = false;
        
        static std::vector<ProjectInfo> recentProjects;
        static bool recentsLoaded = false;

        if (!recentsLoaded) {
            recentProjects = GetRecentProjectsInfo();
            recentsLoaded = true;
        }

        m_uiDrawList.AddText(m_fontManager, "PROJECT CATEGORIES", 20.0f, 20.0f, 0xFF888888);
        
        if (activeCategory == 0) {
            m_uiDrawList.AddRectFilled(0, 60, l_leftW, 50, 0xFF2A2A2A);
            m_uiDrawList.AddRectFilled(0, 60, 4, 50, unrealBlue);
        }
        if (m_uiContext.Button("New Project", 0, 60, l_leftW, 50, 0x00000000, 0x22FFFFFF, 0x44FFFFFF)) activeCategory = 0;

        if (activeCategory == 1) {
            m_uiDrawList.AddRectFilled(0, 110, l_leftW, 50, 0xFF2A2A2A);
            m_uiDrawList.AddRectFilled(0, 110, 4, 50, unrealBlue);
        }
        if (m_uiContext.Button("Load Project", 0, 110, l_leftW, 50, 0x00000000, 0x22FFFFFF, 0x44FFFFFF)) activeCategory = 1;

        if (activeCategory == 0) {
            m_uiDrawList.AddText(m_fontManager, "New Project Templates", l_leftW + 30.0f, 20.0f, 0xFFFFFFFF);
            
            float tX = l_leftW + 30.0f; float tY = 80.0f;
            m_uiDrawList.AddRectFilled(tX - 4, tY - 4, 128, 148, unrealBlue);
            m_uiDrawList.AddRectFilled(tX, tY, 120, 140, 0xFF2A2A2A);
            m_uiDrawList.AddText(m_fontManager, "Blank", tX + 25, tY + 110, 0xFFFFFFFF);
            
            m_uiDrawList.AddText(m_fontManager, "Blank", w - l_rightW + 20.0f, 20.0f, 0xFFFFFFFF);
            m_uiDrawList.AddText(m_fontManager, "A clean empty project with no code. Start from scratch.", w - l_rightW + 20.0f, 60.0f, 0xFFAAAAAA, l_rightW - 40.0f);

            m_uiDrawList.AddText(m_fontManager, "Project Name:", 30.0f, h - l_botH + 25.0f, 0xFFFFFFFF);
            m_uiContext.TextInput("ProjNameInput", inputProjectName, 210.0f, h - l_botH + 15.0f, 300.0f, 50.0f, isInputActive);

            if (m_uiContext.Button("Cancel", w - 240, h - l_botH + 20, 100, 40, 0xFF333333, 0xFF444444, 0xFF222222)) PostQuitMessage(0);
            
            if (m_uiContext.Button("Create", w - 130, h - l_botH + 20, 100, 40, unrealBlue, 0xFFFF9020, 0xFFB05000)) {
                std::wstring folder = BrowseForProjectFolder(m_hwnd);
                if (!folder.empty() && !inputProjectName.empty()) { 
                    CreateNewProject(folder, inputProjectName);
                    std::wstring wProjName(inputProjectName.begin(), inputProjectName.end());
                    currentProjectFolder = (std::filesystem::path(folder) / wProjName).wstring();
                    
                    triggerEditorSwap = true;
                    editorSwapProjName = wProjName;
                    recentsLoaded = false;
                }
            }
        } else {
            m_uiDrawList.AddText(m_fontManager, "Recent Projects", l_leftW + 30.0f, 20.0f, 0xFFFFFFFF);
            float py = 80.0f;
            
            for (const auto& projInfo : recentProjects) {
                std::string nameStr; for (wchar_t c : projInfo.ProjectName) nameStr += static_cast<char>(c);
                std::string verStr;  for (wchar_t c : projInfo.EngineVersion) verStr += static_cast<char>(c);
                
                m_uiDrawList.AddRectFilled(l_leftW + 30.0f, py, w - l_leftW - l_rightW - 60.0f, 60.0f, 0xFF2A2A2A);
                m_uiDrawList.AddText(m_fontManager, nameStr, l_leftW + 50.0f, py + 20.0f, 0xFFFFFFFF);
                m_uiDrawList.AddText(m_fontManager, "Catalyst Engine " + verStr, l_leftW + 50.0f, py + 45.0f, 0xFF888888);
                
                if (m_uiContext.Button("", l_leftW + 30.0f, py, w - l_leftW - l_rightW - 60.0f, 60.0f, 0x00000000, 0x22FFFFFF, 0x44FFFFFF)) {
                    AddRecentProject(projInfo.Path);
                    currentProjectFolder = std::filesystem::path(projInfo.Path).parent_path().wstring();
                    
                    triggerEditorSwap = true;
                    editorSwapProjName = projInfo.ProjectName;
                    recentsLoaded = false;
                }
                py += 70.0f;
            }

            if (m_uiContext.Button("Cancel", w - 240, h - l_botH + 20, 100, 40, 0xFF333333, 0xFF444444, 0xFF222222)) PostQuitMessage(0);
            
            if (m_uiContext.Button("Browse...", w - 130, h - l_botH + 20, 100, 40, unrealBlue, 0xFFFF9020, 0xFFB05000)) {
                std::wstring file = BrowseForProjectFile(m_hwnd);
                if (!file.empty()) {
                    AddRecentProject(file);
                    currentProjectFolder = std::filesystem::path(file).parent_path().wstring();
                    std::wstring pName = std::filesystem::path(file).stem().wstring();
                    
                    triggerEditorSwap = true;
                    editorSwapProjName = pName;
                    recentsLoaded = false;
                }
            }
        }
    } else {
        m_camera.Update(0.016f); 
        
        float w = (float)m_width;
        float h = (float)m_height;
        float outlinerH = (h - topH) * 0.45f;
        
        bool deleteIsDown = (GetAsyncKeyState(VK_DELETE) & 0x8000) != 0;
        if (deleteIsDown && !deleteWasDown && selectedObj >= 0 && selectedObj < m_gameObjects.size()) {
            m_gameObjects.erase(m_gameObjects.begin() + selectedObj);
            selectedObj = -1;
        }
        deleteWasDown = deleteIsDown;

        m_uiDrawList.AddRectFilled(0, 0, w, topMenuH, 0xFF000000); 
        
        std::vector<std::string> topMenus = {"File", "Edit", "Window", "Tools", "Build", "Help"};
        float menuX = 10.0f;
        for (const auto& menuStr : topMenus) {
            m_uiContext.Button(menuStr, menuX, 0.0f, 65.0f, topMenuH, 0x00000000, 0xFF333333, 0xFF555555);
            menuX += 65.0f;
        }
        
        m_uiDrawList.AddRectFilled(0, topMenuH, w, toolbarH, 0xFF151515); 
        m_uiContext.Button("Save All", 15.0f, topMenuH + 5.0f, 80.0f, 30.0f, 0x00000000, 0xFF333333, 0xFF444444);
        
        static bool isPlaying = false;
        uint32_t playBtnColor = isPlaying ? 0xFF222288 : 0xFF225522; 
        std::string playText = isPlaying ? "Stop" : "Play";
        
        if (m_uiContext.Button(playText, 105.0f, topMenuH + 5.0f, 60.0f, 30.0f, playBtnColor, 0xFF337733, 0xFF114411)) {
            isPlaying = !isPlaying;
        }

        if (m_uiContext.Button("Place Actors +", 175.0f, topMenuH + 5.0f, 130.0f, 30.0f, 0x00000000, 0xFF333333, 0xFF555555)) {
            showPlaceActorsMenu = !showPlaceActorsMenu;
        }

        if (isPlaying) {
            for (auto& obj : m_gameObjects) {
                if (obj.name.find("Sky") == std::string::npos && obj.name != "Floor") {
                    obj.position.y += sinf(GetTickCount() * 0.005f) * 0.005f;
                }
            }
        }

        float rightX = w - rightW;
        m_uiDrawList.AddRectFilled(rightX, topH, rightW, outlinerH, 0xFF1A1A1A); 
        m_uiDrawList.AddRectFilled(rightX, topH, rightW, 30.0f, 0xFF0E0E0E);     
        m_uiDrawList.AddText(m_fontManager, "Outliner", rightX + 15.0f, topH + 20.0f, 0xFFFFFFFF);

        if (selectedObj >= static_cast<int>(m_gameObjects.size())) selectedObj = -1;
        
        float listY = topH + 35.0f;
        for (int i = 0; i < static_cast<int>(m_gameObjects.size()); ++i) {
            uint32_t btnColor = (selectedObj == i) ? 0xFFD77800 : 0x00000000;
            if (m_uiContext.Button(m_gameObjects[i].name, rightX, listY, rightW, 25.0f, btnColor, 0xFF333333, 0xFF555555)) {
                selectedObj = i;
            }
            listY += 25.0f;
        }

        float detailsY = topH + outlinerH;
        float detailsH = h - detailsY;
        m_uiDrawList.AddRectFilled(rightX, detailsY, rightW, detailsH, 0xFF1A1A1A); 
        m_uiDrawList.AddRectFilled(rightX, detailsY, rightW, 30.0f, 0xFF0E0E0E);    
        m_uiDrawList.AddText(m_fontManager, "Details", rightX + 15.0f, detailsY + 20.0f, 0xFFFFFFFF);

        if (selectedObj >= 0 && selectedObj < static_cast<int>(m_gameObjects.size())) {
            auto& obj = m_gameObjects[selectedObj];
            float insX = rightX + 15.0f;
            float insY = detailsY + 45.0f;
            float sW = rightW - 30.0f;
            
            m_uiDrawList.AddRectFilled(insX, insY, sW, 30.0f, 0xFF222222);
            m_uiDrawList.AddText(m_fontManager, obj.name, insX + 5.0f, insY + 22.0f, 0xFFFFFFFF);
            insY += 45.0f;
            
            // Cleaned up the details panel to remove procedural colors since we are using HDR!
            m_uiDrawList.AddText(m_fontManager, "Transform", insX, insY + 15.0f, 0xFFCCCCCC);
            insY += 25.0f;
            m_uiContext.DragFloat("Loc X", obj.position.x, 0.05f, insX, insY, sW, 24.0f); insY += 28.0f;
            m_uiContext.DragFloat("Loc Y", obj.position.y, 0.05f, insX, insY, sW, 24.0f); insY += 28.0f;
            m_uiContext.DragFloat("Loc Z", obj.position.z, 0.05f, insX, insY, sW, 24.0f); insY += 35.0f;

            m_uiDrawList.AddText(m_fontManager, "Material", insX, insY + 15.0f, 0xFFCCCCCC);
            insY += 25.0f;
            m_uiContext.DragFloat("Col R", obj.color.x, 0.01f, insX, insY, sW, 24.0f); insY += 28.0f;
            m_uiContext.DragFloat("Col G", obj.color.y, 0.01f, insX, insY, sW, 24.0f); insY += 28.0f;
            m_uiContext.DragFloat("Col B", obj.color.z, 0.01f, insX, insY, sW, 24.0f); insY += 28.0f;
        }

        float bottomY = h - bottomH;
        float bottomW = w - rightW;
        m_uiDrawList.AddRectFilled(0, bottomY, bottomW, bottomH, 0xFF151515);
        m_uiDrawList.AddRectFilled(0, bottomY, bottomW, 30.0f, 0xFF0E0E0E); 
        m_uiDrawList.AddText(m_fontManager, "Content Browser", 15.0f, bottomY + 20.0f, 0xFFFFFFFF);
        
        if (m_uiContext.Button("Import", 160.0f, bottomY + 3.0f, 80.0f, 24.0f, 0xFF333333, 0xFF444444, 0xFF555555)) {
            std::wstring sourceFile = BrowseForAssetFile(m_hwnd);
            if (!sourceFile.empty() && !currentProjectFolder.empty()) {
                pendingImportPath = sourceFile;
                pendingImportName = std::filesystem::path(sourceFile).stem().string(); 
                showImportPopup = true;
                wasImportJustOpened = true; 
            }
        }
        
        m_uiDrawList.AddRectFilled(10.0f, bottomY + 40.0f, 180.0f, bottomH - 50.0f, 0xFF1A1A1A); 
        m_uiDrawList.AddText(m_fontManager, "> Project\n  > Assets\n  > Materials\n  > Audio", 20.0f, bottomY + 70.0f, 0xFFAAAAAA);
        
        if (GetTickCount() - lastScanTime > 2000) {
            discoveredAssets.clear();
            if (!currentProjectFolder.empty()) {
                std::filesystem::path assetPath = std::filesystem::path(currentProjectFolder) / L"Assets";
                if (std::filesystem::exists(assetPath)) {
                    for (const auto& entry : std::filesystem::directory_iterator(assetPath)) {
                        if (entry.path().extension() == L".catalystactor") {
                            discoveredAssets.push_back(entry.path().stem().string());
                        }
                    }
                }
            }
            lastScanTime = GetTickCount();
        }

        float assetX = 210.0f;
        float assetY = bottomY + 40.0f;
        
        for(int i = 0; i < static_cast<int>(discoveredAssets.size()); i++) {
            uint32_t boxColor = (selectedContentAsset == i) ? 0xFFD77800 : 0xFF222222;
            
            m_uiDrawList.AddRectFilled(assetX - 2, assetY - 2, 94.0f, 94.0f, boxColor); 
            m_uiDrawList.AddRectFilled(assetX, assetY, 90.0f, 90.0f, 0xFF333333);       
            m_uiDrawList.AddRectFilled(assetX, assetY + 65.0f, 90.0f, 25.0f, 0xFF222222);
            
            std::string shortName = discoveredAssets[i].length() > 10 ? discoveredAssets[i].substr(0, 10) + ".." : discoveredAssets[i];
            m_uiDrawList.AddText(m_fontManager, shortName, assetX + 5.0f, assetY + 82.0f, 0xFFFFFFFF);
            
            if (m_uiContext.Button("", assetX, assetY, 90.0f, 90.0f, 0x00000000, 0x22FFFFFF, 0x44FFFFFF)) {
                selectedContentAsset = i;
            }
            
            assetX += 105.0f;
            if (assetX > bottomW - 100.0f) {
                assetX = 210.0f;
                assetY += 105.0f;
            }
        }

        m_uiDrawList.AddRectFilled(0, topH - 2.0f, w, 2.0f, 0xFF000000);
        m_uiDrawList.AddRectFilled(bottomW - 2.0f, topH, 2.0f, h, 0xFF000000);
        m_uiDrawList.AddRectFilled(0, bottomY - 2.0f, bottomW, 2.0f, 0xFF000000);

        if (showPlaceActorsMenu) {
            float menuX = 175.0f;
            float menuY = topH; 
            float menuW = 150.0f;
            
            m_uiDrawList.AddRectFilled(menuX, menuY, menuW, 175.0f, 0xFF2A2A2A);
            m_uiDrawList.AddRectFilled(menuX, menuY, menuW, 2.0f, 0xFFD77800); 

            std::string primitives[5] = {"Cube", "Sphere", "Plane", "Cylinder", "Sky Atmosphere"};
            for (int i = 0; i < 5; i++) {
                if (m_uiContext.Button(primitives[i], menuX + 5.0f, menuY + 10.0f + (i * 32.0f), menuW - 10.0f, 28.0f, 0xFF222222, 0xFF444444, 0xFF555555)) {
                    GameObject newObj;
                    newObj.name = primitives[i] + "_" + std::to_string(m_gameObjects.size());
                    if (i == 4) {
                        newObj.position = {0, -9999.0f, 0};
                        newObj.scale = {1,1,1};
                        newObj.color = {1,1,1,1};
                        newObj.asset = m_assets[0].get(); 
                    } else {
                        newObj.position = {0, 0, 0};
                        newObj.scale = {1,1,1};
                        newObj.color = {0.8f, 0.8f, 0.8f, 1.0f};
                        newObj.asset = m_assets[i].get(); 
                    }
                    m_gameObjects.push_back(newObj);
                    selectedObj = static_cast<int>(m_gameObjects.size()) - 1;
                    showPlaceActorsMenu = false; 
                }
            }
            
            if (g_InputManager->IsMouseButtonPressed(0)) {
                int mx = g_InputManager->GetMouseX();
                int my = g_InputManager->GetMouseY();
                bool inMenu = (mx >= menuX && mx <= menuX + menuW && my >= menuY && my <= menuY + 175.0f);
                bool inButton = (mx >= 175.0f && mx <= 175.0f + 130.0f && my >= topMenuH + 5.0f && my <= topMenuH + 35.0f);
                if (!inMenu && !inButton) {
                    showPlaceActorsMenu = false;
                }
            }
        }

        if (showImportPopup) {
            float popupW = 350.0f;
            float popupH = 180.0f;
            float popupX = (w - popupW) / 2.0f;
            float popupY = (h - popupH) / 2.0f;

            m_uiDrawList.AddRectFilled(0, 0, w, h, 0xAA000000); 

            m_uiDrawList.AddRectFilled(popupX, popupY, popupW, popupH, 0xFF1A1A1A);
            m_uiDrawList.AddRectFilled(popupX, popupY, popupW, 30.0f, 0xFF0E0E0E);
            m_uiDrawList.AddText(m_fontManager, "Name Imported Asset", popupX + 15.0f, popupY + 20.0f, 0xFFFFFFFF);

            m_uiDrawList.AddText(m_fontManager, "Actor Name:", popupX + 20.0f, popupY + 65.0f, 0xFFCCCCCC);
            
            m_uiContext.TextInput("ImportNameInput", pendingImportName, popupX + 20.0f, popupY + 80.0f, popupW - 40.0f, 40.0f, isImportNameActive);

            if (wasImportJustOpened) {
                isImportNameActive = true;
                wasImportJustOpened = false;
            }

            if (m_uiContext.Button("Cancel", popupX + 20.0f, popupY + 130.0f, 100.0f, 30.0f, 0xFF333333, 0xFF444444, 0xFF222222)) {
                showImportPopup = false;
            }
            if (m_uiContext.Button("Import ", popupX + popupW - 120.0f, popupY + 130.0f, 100.0f, 30.0f, 0xFFD77800, 0xFFFF9020, 0xFFB05000)) {
                if (!pendingImportName.empty()) {
                    std::wstring assetsFolder = (std::filesystem::path(currentProjectFolder) / L"Assets").wstring();
                    std::wstring wNewName(pendingImportName.begin(), pendingImportName.end());
                    ImportAssetToProject(pendingImportPath, assetsFolder, wNewName);
                    lastScanTime = 0; 
                    showImportPopup = false;
                }
            }
        }
    }

    m_commandAllocator->Reset(); 
    m_commandList->Reset(m_commandAllocator.Get(), nullptr);

    D3D12_VIEWPORT fullViewport = { 0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f };
    D3D12_RECT fullScissor = { 0, 0, m_width, m_height };
    m_commandList->RSSetViewports(1, &fullViewport);
    m_commandList->RSSetScissorRects(1, &fullScissor);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += m_frameIndex * m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_commandList->ResourceBarrier(1, &barrier);

    const float clearColor[] = { 0.05f, 0.05f, 0.05f, 1.0f }; 
    m_commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    ID3D12DescriptorHeap* heaps[] = { m_bindlessManager.GetHeap() }; 
    m_commandList->SetDescriptorHeaps(1, heaps);

    if (m_engineState == EngineState::Editor) {
        float viewW = (std::max)(1.0f, m_width - rightW);
        float viewH = (std::max)(1.0f, m_height - topH - bottomH);

        bool isGizmoHovered = false;
        if (selectedObj >= 0 && selectedObj < static_cast<int>(m_gameObjects.size())) {
            if (m_gameObjects[selectedObj].name.find("Sky") == std::string::npos) {
                m_uiContext.TransformGizmo(m_gameObjects[selectedObj].position, m_camera, 0.0f, topH, viewW, viewH, isGizmoHovered);
            }
        }

        if (g_InputManager->IsMouseButtonPressed(0) && !showPlaceActorsMenu && !showImportPopup) {
            int mx = g_InputManager->GetMouseX();
            int my = g_InputManager->GetMouseY();

            if (mx >= 0 && mx <= viewW && my >= topH && my <= topH + viewH) {
                if (!isGizmoHovered) {
                    int closestObj = -1;
                    float closestDist = 50.0f; 

                    DirectX::XMMATRIX viewProj = m_camera.GetViewMatrix() * m_camera.GetProjectionMatrix();

                    for (int i = 0; i < static_cast<int>(m_gameObjects.size()); ++i) {
                        DirectX::XMVECTOR wPos = DirectX::XMLoadFloat3(&m_gameObjects[i].position);
                        DirectX::XMVECTOR ndc = DirectX::XMVector3TransformCoord(wPos, viewProj);
                        DirectX::XMFLOAT3 ndc3;
                        DirectX::XMStoreFloat3(&ndc3, ndc);

                        if (ndc3.z >= 0.0f && ndc3.z <= 1.0f) { 
                            float scrX = 0.0f + (ndc3.x + 1.0f) * 0.5f * viewW;
                            float scrY = topH + (1.0f - ndc3.y) * 0.5f * viewH;

                            float dist = sqrtf((mx - scrX)*(mx - scrX) + (my - scrY)*(my - scrY));
                            if (dist < closestDist) {
                                closestDist = dist;
                                closestObj = i;
                            }
                        }
                    }
                    selectedObj = closestObj; 
                }
            }
        }

        D3D12_VIEWPORT sceneViewport = { 0.0f, topH, viewW, viewH, 0.0f, 1.0f };
        D3D12_RECT sceneScissor = { 0, (LONG)topH, (LONG)viewW, (LONG)(topH + viewH) };
        
        m_commandList->RSSetViewports(1, &sceneViewport);
        m_commandList->RSSetScissorRects(1, &sceneScissor);
        
        m_camera.SetProjection(45.0f, viewW / viewH, 0.1f, 5000.0f);

        XMMATRIX lightSpace = XMMatrixIdentity();
        XMFLOAT3 lightDir = {0, -1, 0};
        
        m_quantaMeshPass.Render(m_commandList.Get(), m_device.Get(), m_gameObjects, m_camera, static_cast<int>(viewW), static_cast<int>(viewH), 
                                lightSpace, lightDir, 1.0f, nullptr, m_bindlessManager.GetHeap(), 
                                m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV), 
                                m_frameHeapOffset, m_texWhite, m_texNormal, m_texBlack, nullptr, m_primitives["Sphere"]);

        // THE HDR FIX: Scan for the Sky Atmosphere Actor. If it exists in the outliner, render the HDR Pass!
        bool skyRendered = false;
        for (auto& o : m_gameObjects) {
            if (o.name.find("Sky") != std::string::npos) {
                skyRendered = true;
                break; 
            }
        }

        if (skyRendered) {
            m_skyboxPass.Render(m_commandList.Get(), m_device.Get(), m_primitives["Cube"], m_camera, m_bindlessManager.GetHeap(), g_skyboxSrvIndex);
        }
    }

    m_commandList->RSSetViewports(1, &fullViewport);
    m_commandList->RSSetScissorRects(1, &fullScissor);

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

    if (triggerEditorSwap) {
        triggerEditorSwap = false;
        
        ShowWindow(m_hwnd, SW_HIDE);
        Sleep(350); 

        m_engineState = EngineState::Editor;
        SetWindowTextW(m_hwnd, (L"Catalyst Editor - " + editorSwapProjName).c_str());

        SetWindowLongPtr(m_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);

        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(m_hwnd, HWND_TOP, 0, 0, screenW, screenH, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);
        
        ShowWindow(m_hwnd, SW_MAXIMIZE);
    }
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