#include "UIManager.h"
#include "Theme.h"
#include <stdexcept>
#include <iostream>
#include <algorithm> 
#include <cctype>    
#include "../Resources/ModelLoader.h"
#include "../Resources/Texture.h"

using namespace DirectX;
namespace fs = std::filesystem;

const char* PAYLOAD_ASSET = "ASSET_DROP"; 

std::string ToLower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c){ return std::tolower(c); });
    return str;
}

void UIManager::Initialize(HWND hwnd, ID3D12Device* device, ID3D12CommandQueue* commandQueue, int frameCount) {
    m_hwnd = hwnd;
    m_device = device;
    m_cmdQueue = commandQueue;

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.NumDescriptors = 64;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap)))) { throw std::runtime_error("Failed to create UI Heap"); }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    UI::ApplyUnrealTheme(); 
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_InitInfo init_info = {}; init_info.Device = device; init_info.CommandQueue = commandQueue; init_info.NumFramesInFlight = frameCount; init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM; init_info.DSVFormat = DXGI_FORMAT_D32_FLOAT; init_info.SrvDescriptorHeap = m_srvHeap.Get(); init_info.LegacySingleSrvCpuDescriptor = m_srvHeap->GetCPUDescriptorHandleForHeapStart(); init_info.LegacySingleSrvGpuDescriptor = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    if (!ImGui_ImplDX12_Init(&init_info)) throw std::runtime_error("Failed to init ImGui DX12");
    ImGui_ImplDX12_CreateDeviceObjects();
}

std::string UIManager::OpenFileDialog() {
    OPENFILENAMEA ofn; char szFile[260] = { 0 }; ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = m_hwnd; ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile); ofn.lpstrFilter = "All Files\0*.*\0"; ofn.nFilterIndex = 1; ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn) == TRUE) return std::string(ofn.lpstrFile);
    return "";
}

// Updated Helper for Smart Pointers
Asset* FindAssetByName(std::vector<std::shared_ptr<Asset>>& assets, const std::string& name) {
    for (auto& a : assets) {
        if (a->name == name) return a.get();
    }
    return nullptr;
}

void UIManager::Update(std::vector<GameObject>& gameObjects, std::vector<std::shared_ptr<Asset>>& assets, int& selectedIndex, const XMMATRIX& viewMatrix, const XMMATRIX& projMatrix) {
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    ImGuiIO& io = ImGui::GetIO();
    float menuHeight = 22.0f;
    float rightBarW = 300.0f;
    float bottomH = 250.0f;
    float screenW = io.DisplaySize.x;
    float screenH = io.DisplaySize.y;
    float viewportW = screenW - rightBarW;
    float viewportH = screenH - bottomH - menuHeight;

    // --- SHARED IMPORT LOGIC ---
    auto ImportAsset = [&](const std::string& path) {
        if (path.empty()) return;
        try {
            static int nextID = 100; 
            auto newAsset = std::make_shared<Asset>();
            newAsset->id = nextID++;
            newAsset->name = fs::path(path).stem().string();
            newAsset->sourcePath = path;
            newAsset->mesh = ModelLoader::Load(path, m_device, m_cmdQueue);
            assets.push_back(newAsset);
            std::cout << "Imported: " << newAsset->name << std::endl;
        } catch(const std::exception& e) { std::cout << "Import Failed: " << e.what() << std::endl; }
    };

    // --- MAIN MENU BAR ---
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Import Asset...")) ImportAsset(OpenFileDialog());
            if (ImGui::MenuItem("Exit")) PostQuitMessage(0);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Place Actors")) {
            ImGui::SeparatorText("Primitives");
            auto SpawnPrimitive = [&](const std::string& assetName, const std::string& objName) {
                Asset* asset = FindAssetByName(assets, assetName);
                if (asset) { GameObject newObj; newObj.name = objName; newObj.asset = asset; newObj.type = ObjectType::Mesh; gameObjects.push_back(newObj); }
            };
            if (ImGui::MenuItem("Cube"))     SpawnPrimitive("Basic Cube", "Cube");
            if (ImGui::MenuItem("Sphere"))   SpawnPrimitive("Basic Sphere", "Sphere");
            if (ImGui::MenuItem("Plane"))    SpawnPrimitive("Basic Plane", "Plane");

            ImGui::SeparatorText("Lights");
            if (ImGui::MenuItem("Directional Light")) {
                Asset* sphereAsset = FindAssetByName(assets, "Basic Sphere");
                GameObject sun; sun.name = "Directional Light"; sun.position = {0, 10, 0}; sun.rotation = {1.57f, 0, 0}; sun.type = ObjectType::Light; sun.lightIntensity = 1.5f; sun.asset = sphereAsset;
                gameObjects.push_back(sun);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // --- OUTLINER ---
    ImGui::SetNextWindowPos(ImVec2(screenW - rightBarW, menuHeight));
    ImGui::SetNextWindowSize(ImVec2(rightBarW, viewportH / 2.0f));
    ImGui::Begin("Outliner", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    
    for (int i = 0; i < gameObjects.size(); i++) {
        ImGui::PushID(i);
        bool isSelected = (selectedIndex == i);
        if (ImGui::Selectable(gameObjects[i].name.c_str(), isSelected)) { selectedIndex = i; }
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Delete")) {
                gameObjects.erase(gameObjects.begin() + i);
                if (selectedIndex == i) selectedIndex = -1; else if (selectedIndex > i) selectedIndex--; i--; 
                ImGui::EndPopup(); ImGui::PopID(); continue; 
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    
    ImVec2 available = ImGui::GetContentRegionAvail(); if (available.y < 50) available.y = 50; 
    ImGui::InvisibleButton("##HierarchyDropZone", available);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(PAYLOAD_ASSET)) {
            int assetID = *(const int*)payload->Data;
            for(auto& a : assets) {
                if(a->id == assetID) { GameObject newObj; newObj.name = a->name + " Instance"; newObj.asset = a.get(); newObj.type = ObjectType::Mesh; gameObjects.push_back(newObj); break; }
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::End();

    // --- DETAILS ---
    ImGui::SetNextWindowPos(ImVec2(screenW - rightBarW, menuHeight + (viewportH / 2.0f)));
    ImGui::SetNextWindowSize(ImVec2(rightBarW, viewportH / 2.0f));
    ImGui::Begin("Details", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    if (selectedIndex >= 0 && selectedIndex < gameObjects.size()) {
        GameObject& obj = gameObjects[selectedIndex];
        char nameBuf[128]; strcpy_s(nameBuf, obj.name.c_str());
        if (ImGui::InputText("Name", nameBuf, 128)) obj.name = nameBuf;
        
        ImGui::Separator();
        ImGui::Text("TRANSFORM");
        ImGui::DragFloat3("Loc", &obj.position.x, 0.1f); ImGui::DragFloat3("Rot", &obj.rotation.x, 0.1f); ImGui::DragFloat3("Scl", &obj.scale.x,    0.1f);
        
        ImGui::Separator();
        if (obj.type == ObjectType::Light) {
             ImGui::TextColored(ImVec4(1,1,0,1), "LIGHT SETTINGS");
             ImGui::DragFloat("Intensity", &obj.lightIntensity, 0.1f, 0.0f, 10.0f);
             ImGui::ColorEdit3("Color", &obj.color.x);
        } else {
             if (obj.asset) {
                ImGui::TextDisabled("Source Asset: %s", obj.asset->name.c_str());
                if(ImGui::Button("Edit Asset Properties", ImVec2(-1, 0))) { m_editingAsset = obj.asset; m_showAssetEditor = true; }
            } else { ImGui::TextDisabled("Source Asset: None"); }
        }

        ImGuizmo::SetRect(0, menuHeight, viewportW, viewportH);
        XMFLOAT4X4 view, proj; XMStoreFloat4x4(&view, viewMatrix); XMStoreFloat4x4(&proj, projMatrix);
        float transform[16];
        float translation[3] = { obj.position.x, obj.position.y, obj.position.z };
        float rotation[3]    = { XMConvertToDegrees(obj.rotation.x), XMConvertToDegrees(obj.rotation.y), XMConvertToDegrees(obj.rotation.z) };
        float scale[3]       = { obj.scale.x, obj.scale.y, obj.scale.z };
        ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, transform);
        ImGuizmo::Manipulate(&view.m[0][0], &proj.m[0][0], ImGuizmo::OPERATION::TRANSLATE, ImGuizmo::MODE::WORLD, transform);
        if (ImGuizmo::IsUsing()) {
            float newT[3], newR[3], newS[3]; ImGuizmo::DecomposeMatrixToComponents(transform, newT, newR, newS);
            obj.position = { newT[0], newT[1], newT[2] }; obj.rotation = { XMConvertToRadians(newR[0]), XMConvertToRadians(newR[1]), XMConvertToRadians(newR[2]) }; obj.scale = { newS[0], newS[1], newS[2] };
        }
    }
    ImGui::End();

    // --- CONTENT BROWSER ---
    ImGui::SetNextWindowPos(ImVec2(0, menuHeight + viewportH));
    ImGui::SetNextWindowSize(ImVec2(screenW, bottomH));
    ImGui::Begin("Content Browser", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    
    if (ImGui::Button("+ Import", ImVec2(100, 30))) { ImportAsset(OpenFileDialog()); }
    ImGui::SameLine(); ImGui::SetNextItemWidth(100); ImGui::SliderFloat("Scale", &m_thumbnailSize, 32.0f, 128.0f);
    ImGui::Separator();
    if (ImGui::BeginPopupContextWindow()) { if (ImGui::MenuItem("Import Asset...")) ImportAsset(OpenFileDialog()); ImGui::EndPopup(); }

    DrawContentBrowser(gameObjects, assets);
    ImGui::End();

    // --- ASSET EDITOR WINDOW ---
    if (m_showAssetEditor && m_editingAsset) { DrawAssetEditorWindow(m_device, m_cmdQueue); }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedIndex >= 0 && selectedIndex < gameObjects.size()) {
        if (!ImGui::GetIO().WantCaptureKeyboard) { gameObjects.erase(gameObjects.begin() + selectedIndex); selectedIndex = -1; }
    }
    ImGui::Render();
}

void UIManager::DrawContentBrowser(std::vector<GameObject>& gameObjects, std::vector<std::shared_ptr<Asset>>& assets) {
    float cellSize = m_thumbnailSize + m_padding;
    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = (int)(panelWidth / cellSize);
    if (columnCount < 1) columnCount = 1;

    ImGui::Columns(columnCount, 0, false);

    for (auto& assetPtr : assets) {
        Asset* asset = assetPtr.get();
        ImGui::PushID(asset->id);
        ImGui::BeginGroup();
        
        ImVec4 iconColor = ImVec4(0.2f, 0.4f, 0.8f, 1.0f);
        if (asset->id < 100) iconColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); 

        ImGui::PushStyleColor(ImGuiCol_Button, iconColor);
        ImGui::Button("##Icon", ImVec2(m_thumbnailSize, m_thumbnailSize));
        ImGui::PopStyleColor();

        std::string displayName = asset->name;
        if (displayName.length() > 10) displayName = displayName.substr(0, 8) + "...";
        ImGui::Text("%s", displayName.c_str());
        
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { m_editingAsset = asset; m_showAssetEditor = true; }
        ImGui::EndGroup();

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload(PAYLOAD_ASSET, &asset->id, sizeof(int));
            ImGui::Text("Asset: %s", asset->name.c_str());
            ImGui::EndDragDropSource();
        }

        ImGui::PopID();
        ImGui::NextColumn();
    }
    ImGui::Columns(1);
}

void UIManager::DrawAssetEditorWindow(ID3D12Device* device, ID3D12CommandQueue* cmdQueue) {
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Asset Editor", &m_showAssetEditor)) { ImGui::End(); return; }

    ImGui::Columns(2); ImGui::SetColumnWidth(0, 250);
    
    ImGui::TextColored(ImVec4(1,1,0,1), "Editing: %s", m_editingAsset->name.c_str());
    ImGui::Separator();
    
    ImGui::Text("MATERIALS");
    std::string texName = m_editingAsset->texture ? "Texture Assigned" : "Empty";
    if(ImGui::Button(("Alb: " + texName).c_str(), ImVec2(-1, 0))) {
        std::string p = OpenFileDialog();
        if(!p.empty()) { auto t = new Texture(); t->Load(p, device, cmdQueue); m_editingAsset->texture = t; }
    }
    std::string normName = m_editingAsset->normalMap ? "Normal Assigned" : "Empty";
    if(ImGui::Button(("Nrm: " + normName).c_str(), ImVec2(-1, 0))) {
        std::string p = OpenFileDialog();
        if(!p.empty()) { auto t = new Texture(); t->Load(p, device, cmdQueue); m_editingAsset->normalMap = t; }
    }

    ImGui::Separator();
    ImGui::Text("VIRTUAL GEOMETRY (NANITE)");
    
    ImGui::Checkbox("Enable Virtualization", &m_editingAsset->useVirtualGeometry);
    if (m_editingAsset->useVirtualGeometry) {
        ImGui::Indent();
        ImGui::TextDisabled("Meshlets: %d", (int)m_editingAsset->mesh->GetMeshlets().size());
        ImGui::Checkbox("Debug Visualizer", &m_editingAsset->debugVisualizer);
        ImGui::Unindent();
    }

    ImGui::NextColumn();
    ImGui::TextDisabled("[ 3D Preview Would Go Here ]");
    ImGui::TextWrapped("Changes applied here affect ALL instances of this asset in the scene immediately.");

    ImGui::End();
}

void UIManager::Draw(ID3D12GraphicsCommandList* cmdList) {
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
}

void UIManager::Shutdown() {
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}