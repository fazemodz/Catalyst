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

// Constants to prevent typos
const char* PAYLOAD_MODEL = "DRAG_DROP_MODEL";
const char* PAYLOAD_TEXTURE = "DRAG_DROP_TEXTURE";

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
    
    if (FAILED(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap)))) {
        throw std::runtime_error("Failed to create UI Heap");
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    UI::ApplyUnrealTheme(); 

    ImGui_ImplWin32_Init(hwnd);

    ImGui_ImplDX12_InitInfo init_info = {};
    init_info.Device = device;
    init_info.CommandQueue = commandQueue;
    init_info.NumFramesInFlight = frameCount;
    init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    init_info.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    init_info.SrvDescriptorHeap = m_srvHeap.Get();
    init_info.LegacySingleSrvCpuDescriptor = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    init_info.LegacySingleSrvGpuDescriptor = m_srvHeap->GetGPUDescriptorHandleForHeapStart();

    if (!ImGui_ImplDX12_Init(&init_info)) throw std::runtime_error("Failed to init ImGui DX12");
    ImGui_ImplDX12_CreateDeviceObjects();
}

std::string UIManager::OpenFileDialog() {
    OPENFILENAMEA ofn;
    char szFile[260] = { 0 };
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Image Files\0*.png;*.jpg;*.jpeg;*.bmp\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn) == TRUE) return std::string(ofn.lpstrFile);
    return "";
}

// ===========================================================================================
//  MAIN UPDATE LOOP
// ===========================================================================================
void UIManager::Update(std::vector<GameObject>& gameObjects, int& selectedIndex, const XMMATRIX& viewMatrix, const XMMATRIX& projMatrix) {
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

    // 1. MENU BAR (Top)
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit")) PostQuitMessage(0);
            ImGui::EndMenu();
        }

        // --- PLACE ACTORS DROPDOWN ---
        if (ImGui::BeginMenu("Place Actors")) {
            ImGui::SeparatorText("Primitives");
            
            // Spawns using the pre-generated meshes in m_primitives
            if (ImGui::MenuItem("Cube")) {
                gameObjects.push_back({ "Cube", {0,0,0}, {0,0,0}, {1,1,1}, {1,1,1,1}, m_primitives["Cube"], nullptr, nullptr, ObjectType::Mesh });
            }
            if (ImGui::MenuItem("Sphere")) {
                gameObjects.push_back({ "Sphere", {0,0,0}, {0,0,0}, {1,1,1}, {1,1,1,1}, m_primitives["Sphere"], nullptr, nullptr, ObjectType::Mesh });
            }
            if (ImGui::MenuItem("Cylinder")) {
                gameObjects.push_back({ "Cylinder", {0,0,0}, {0,0,0}, {1,1,1}, {1,1,1,1}, m_primitives["Cylinder"], nullptr, nullptr, ObjectType::Mesh });
            }
            if (ImGui::MenuItem("Plane")) {
                gameObjects.push_back({ "Plane", {0,0,0}, {0,0,0}, {1,1,1}, {1,1,1,1}, m_primitives["Plane"], nullptr, nullptr, ObjectType::Mesh });
            }

            ImGui::SeparatorText("Lights");
            if (ImGui::MenuItem("Directional Light")) {
                // Pointing DOWN by default (Rot X = 1.57)
                gameObjects.push_back({ "Sun Light", {0, 10, 0}, {1.57f, 0, 0}, {1,1,1}, {1,1,1,1}, m_primitives["Sphere"], nullptr, nullptr, ObjectType::Light, 1.5f });
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // 2. OUTLINER (Top Right)
    ImGui::SetNextWindowPos(ImVec2(screenW - rightBarW, menuHeight));
    ImGui::SetNextWindowSize(ImVec2(rightBarW, viewportH / 2.0f));
    ImGui::Begin("Outliner", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    
    for (int i = 0; i < gameObjects.size(); i++) {
        ImGui::PushID(i);
        bool isSelected = (selectedIndex == i);
        if (ImGui::Selectable(gameObjects[i].name.c_str(), isSelected)) {
            selectedIndex = i;
        }

        // Context Menu (Right Click -> Delete)
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Delete")) {
                gameObjects.erase(gameObjects.begin() + i);
                if (selectedIndex == i) selectedIndex = -1;
                else if (selectedIndex > i) selectedIndex--;
                i--; // Adjust index
                ImGui::EndPopup();
                ImGui::PopID();
                continue; 
            }
            ImGui::EndPopup();
        }

        // Drag Drop Texture ONTO the name
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(PAYLOAD_TEXTURE)) {
                std::string path = (const char*)payload->Data;
                try {
                    Texture* t = new Texture();
                    t->Load(path, m_device, m_cmdQueue);
                    gameObjects[i].texture = t;
                } catch(...) {}
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::PopID();
    }
    
    // Empty Space Drop Target (Drag Model to create new object)
    ImVec2 available = ImGui::GetContentRegionAvail();
    if (available.y < 50) available.y = 50; 
    ImGui::InvisibleButton("##HierarchyDropZone", available);

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(PAYLOAD_MODEL)) {
            std::string path = (const char*)payload->Data;
            try {
                Mesh* m = ModelLoader::Load(path, m_device);
                std::string name = fs::path(path).stem().string();
                // Note: nullptr for both texture slots
                gameObjects.push_back({ name, {0,0,0}, {0,0,0}, {1,1,1}, {1,1,1,1}, m, nullptr, nullptr, ObjectType::Mesh });
            } catch(...) {}
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::End();

    // 3. DETAILS PANEL (Bottom Right)
    ImGui::SetNextWindowPos(ImVec2(screenW - rightBarW, menuHeight + (viewportH / 2.0f)));
    ImGui::SetNextWindowSize(ImVec2(rightBarW, viewportH / 2.0f));
    ImGui::Begin("Details", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    if (selectedIndex >= 0 && selectedIndex < gameObjects.size()) {
        GameObject& obj = gameObjects[selectedIndex];
        
        // Name Input
        char nameBuf[128]; strcpy_s(nameBuf, obj.name.c_str());
        if (ImGui::InputText("Name", nameBuf, 128)) obj.name = nameBuf;
        
        ImGui::Separator();
        ImGui::Text("TRANSFORM");
        ImGui::DragFloat3("Loc", &obj.position.x, 0.1f);
        ImGui::DragFloat3("Rot", &obj.rotation.x, 0.1f);
        ImGui::DragFloat3("Scl", &obj.scale.x,    0.1f);
        ImGui::Separator();

        // --- TYPE SPECIFIC UI ---
        if (obj.type == ObjectType::Light) {
            ImGui::TextColored(ImVec4(1,1,0,1), "LIGHT SETTINGS");
            ImGui::DragFloat("Intensity", &obj.lightIntensity, 0.1f, 0.0f, 10.0f);
            ImGui::ColorEdit3("Light Color", &obj.color.x);
        }
        else {
            ImGui::Text("MATERIAL");
            ImGui::ColorEdit4("Tint", &obj.color.x);
            
            // --- A. ALBEDO TEXTURE ---
            std::string btnText = obj.texture ? "Tex: " + fs::path(obj.name).filename().string() : "Texture: Empty";
            if (obj.texture) btnText = "Texture Assigned";
            
            if (ImGui::Button(btnText.c_str(), ImVec2(-1, 30))) {
                std::string path = OpenFileDialog();
                if (!path.empty()) {
                    try { Texture* t = new Texture(); t->Load(path, m_device, m_cmdQueue); obj.texture = t; } catch(...) {}
                }
            }
            // Drop Logic (Albedo)
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(PAYLOAD_TEXTURE)) {
                    std::string path = (const char*)payload->Data;
                    try { Texture* t = new Texture(); t->Load(path, m_device, m_cmdQueue); obj.texture = t; } catch(...) {}
                }
                ImGui::EndDragDropTarget();
            }

            // --- B. NORMAL MAP (NEW) ---
            ImGui::Spacing();
            std::string normText = obj.normalMap ? "Norm: " + fs::path(obj.name).filename().string() : "Normal: Empty";
            if (obj.normalMap) normText = "Normal Map Assigned";

            if (ImGui::Button(normText.c_str(), ImVec2(-1, 30))) {
                std::string path = OpenFileDialog();
                if (!path.empty()) {
                    try { Texture* t = new Texture(); t->Load(path, m_device, m_cmdQueue); obj.normalMap = t; } catch(...) {}
                }
            }
            // Drop Logic (Normal)
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(PAYLOAD_TEXTURE)) {
                    std::string path = (const char*)payload->Data;
                    try { Texture* t = new Texture(); t->Load(path, m_device, m_cmdQueue); obj.normalMap = t; } catch(...) {}
                }
                ImGui::EndDragDropTarget();
            }
        }

        // --- GIZMO ---
        ImGuizmo::SetRect(0, menuHeight, viewportW, viewportH);
        XMFLOAT4X4 view, proj;
        XMStoreFloat4x4(&view, viewMatrix);
        XMStoreFloat4x4(&proj, projMatrix);
        float transform[16];
        float translation[3] = { obj.position.x, obj.position.y, obj.position.z };
        float rotation[3]    = { XMConvertToDegrees(obj.rotation.x), XMConvertToDegrees(obj.rotation.y), XMConvertToDegrees(obj.rotation.z) };
        float scale[3]       = { obj.scale.x, obj.scale.y, obj.scale.z };

        ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, transform);
        ImGuizmo::Manipulate(&view.m[0][0], &proj.m[0][0], ImGuizmo::OPERATION::TRANSLATE, ImGuizmo::MODE::WORLD, transform);

        if (ImGuizmo::IsUsing()) {
            float newT[3], newR[3], newS[3];
            ImGuizmo::DecomposeMatrixToComponents(transform, newT, newR, newS);
            obj.position = { newT[0], newT[1], newT[2] };
            obj.rotation = { XMConvertToRadians(newR[0]), XMConvertToRadians(newR[1]), XMConvertToRadians(newR[2]) };
            obj.scale    = { newS[0], newS[1], newS[2] };
        }
    }
    ImGui::End();

    // 4. CONTENT BROWSER (Bottom)
    ImGui::SetNextWindowPos(ImVec2(0, menuHeight + viewportH));
    ImGui::SetNextWindowSize(ImVec2(screenW, bottomH));
    DrawContentBrowser(gameObjects);

    // 5. SHORTCUTS (Delete)
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedIndex >= 0 && selectedIndex < gameObjects.size()) {
        if (!ImGui::GetIO().WantCaptureKeyboard) {
            gameObjects.erase(gameObjects.begin() + selectedIndex);
            selectedIndex = -1;
        }
    }

    ImGui::Render();
}

void UIManager::DrawContentBrowser(std::vector<GameObject>& gameObjects) {
    ImGui::Begin("Content Browser", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    
    ImGui::Text("Assets");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::SliderFloat("Scale", &m_thumbnailSize, 32.0f, 128.0f);
    ImGui::Separator();

    std::string path = "./Assets";
    if (!fs::exists(path)) {
        ImGui::TextColored(ImVec4(1,0,0,1), "Error: 'Assets' folder not found.");
        ImGui::End();
        return;
    }

    float cellSize = m_thumbnailSize + m_padding;
    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = (int)(panelWidth / cellSize);
    if (columnCount < 1) columnCount = 1;

    ImGui::Columns(columnCount, 0, false);

    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.is_directory()) continue;

        auto filename = entry.path().filename().string();
        std::string fullPath = entry.path().string();
        std::string ext = ToLower(entry.path().extension().string());

        ImGui::PushID(fullPath.c_str());
        ImGui::BeginGroup();
        
        ImVec4 btnColor = ImVec4(0.3f, 0.3f, 0.3f, 1.0f); 
        if (ext == ".obj" || ext == ".fbx") btnColor = ImVec4(0.2f, 0.4f, 0.8f, 1.0f);
        if (ext == ".png" || ext == ".jpg") btnColor = ImVec4(0.2f, 0.6f, 0.2f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, btnColor);
        ImGui::Button("##Icon", ImVec2(m_thumbnailSize, m_thumbnailSize));
        ImGui::PopStyleColor();

        std::string displayName = filename;
        if (displayName.length() > 10) displayName = displayName.substr(0, 8) + "...";
        ImGui::Text("%s", displayName.c_str());
        ImGui::EndGroup();

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") {
                ImGui::SetDragDropPayload(PAYLOAD_TEXTURE, fullPath.c_str(), fullPath.length() + 1);
            } 
            else if (ext == ".obj" || ext == ".fbx") {
                ImGui::SetDragDropPayload(PAYLOAD_MODEL, fullPath.c_str(), fullPath.length() + 1);
            }
            ImGui::EndDragDropSource();
        }

        ImGui::PopID();
        ImGui::NextColumn();
    }
    ImGui::Columns(1);
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