#include "UIManager.h"
#include "Theme.h"
#include <stdexcept>
#include <iostream>
#include <algorithm> 
#include <cctype>
#include <thread> 
#include <chrono> 
#include "../Resources/ModelLoader.h"
#include "../Resources/Texture.h"

using namespace DirectX;
namespace fs = std::filesystem;

const char* PAYLOAD_ASSET = "ASSET_DROP"; 

std::string ToLower(std::string str) { std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c){ return std::tolower(c); }); return str; }

void UIManager::Initialize(HWND hwnd, ID3D12Device* device, ID3D12CommandQueue* commandQueue, int frameCount) {
    m_hwnd = hwnd; m_device = device; m_cmdQueue = commandQueue;
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {}; srvDesc.NumDescriptors = 64; srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap)))) throw std::runtime_error("Failed to create UI Heap");
    IMGUI_CHECKVERSION(); ImGui::CreateContext(); UI::ApplyUnrealTheme(); 
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_InitInfo init_info = {}; init_info.Device = device; init_info.CommandQueue = commandQueue; init_info.NumFramesInFlight = frameCount; init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM; init_info.DSVFormat = DXGI_FORMAT_D32_FLOAT; init_info.SrvDescriptorHeap = m_srvHeap.Get(); init_info.LegacySingleSrvCpuDescriptor = m_srvHeap->GetCPUDescriptorHandleForHeapStart(); init_info.LegacySingleSrvGpuDescriptor = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    if (!ImGui_ImplDX12_Init(&init_info)) throw std::runtime_error("Failed to init ImGui DX12");
    ImGui_ImplDX12_CreateDeviceObjects();
}

std::string UIManager::OpenFileDialog(const char* filter) {
    OPENFILENAMEA ofn; char szFile[260] = { 0 }; ZeroMemory(&ofn, sizeof(ofn)); 
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = m_hwnd; ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile); 
    ofn.lpstrFilter = filter; ofn.nFilterIndex = 1; ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn) == TRUE) return std::string(ofn.lpstrFile);
    return "";
}

Asset* FindAssetByName(std::vector<std::shared_ptr<Asset>>& assets, const std::string& name) { for (auto& a : assets) { if (a->name == name) return a.get(); } return nullptr; }

void UIManager::StartImport(const std::string& path) {
    if (path.empty()) return; m_pendingImportPath = path; m_pendingImportName = fs::path(path).stem().string(); m_isImporting = true;
    m_importStatus = "Importing Model " + m_pendingImportName + "...";
    ID3D12Device* d = m_device; ID3D12CommandQueue* q = m_cmdQueue;
    m_importFuture = std::async(std::launch::async, [path, d, q]() { return ModelLoader::Load(path, d, q); });
}

void UIManager::StartImportHDR(const std::string& path) {
    if (path.empty()) return; m_pendingImportPath = path; m_pendingImportName = fs::path(path).stem().string(); m_isImportingHDR = true;
    m_importStatus = "Importing HDR Skybox " + m_pendingImportName + "...";
    ID3D12Device* d = m_device; ID3D12CommandQueue* q = m_cmdQueue;
    m_importHDRFuture = std::async(std::launch::async, [path, d, q]() { Texture* t = new Texture(); t->LoadHDR(path, d, q); return t; });
}

void UIManager::Update(std::vector<GameObject>& gameObjects, std::vector<std::shared_ptr<Asset>>& assets, int& selectedIndex, const XMMATRIX& viewMatrix, const XMMATRIX& projMatrix, D3D12_GPU_DESCRIPTOR_HANDLE previewTex, PostProcessSettings& globalPP) {
    ImGui_ImplDX12_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame(); ImGuizmo::BeginFrame();

    if (!ImGui::GetIO().WantCaptureKeyboard) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) m_currentGizmoOp = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) m_currentGizmoOp = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) m_currentGizmoOp = ImGuizmo::SCALE;
    }

    if (m_isImporting || m_isImportingHDR) {
        ImGui::OpenPopup("Processing Asset"); ImVec2 center = ImGui::GetMainViewport()->GetCenter(); ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Processing Asset", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
            ImGui::Text("%s", m_importStatus.c_str()); ImGui::Separator();
            static float time = 0.0f; time += ImGui::GetIO().DeltaTime; float radius = 20.0f; ImVec2 p = ImGui::GetCursorScreenPos(); p.x += 20; p.y += 20;
            for (int i = 0; i < 12; i++) { float a = time * 8.0f + i * 6.28f / 12; ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x + cos(a) * radius, p.y + sin(a) * radius), ImVec2(p.x + cos(a) * (radius - 5), p.y + sin(a) * (radius - 5)), IM_COL32(255, 255, 255, 255 - i * 20), 2.0f); } ImGui::Dummy(ImVec2(50, 50));

            if (m_isImporting && m_importFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) { try { Mesh* mesh = m_importFuture.get(); static int nextID = 100; auto newAsset = std::make_shared<Asset>(); newAsset->id = nextID++; newAsset->name = m_pendingImportName; newAsset->sourcePath = m_pendingImportPath; newAsset->type = AssetType::Mesh; newAsset->mesh = mesh; assets.push_back(newAsset); } catch(...) { } m_isImporting = false; ImGui::CloseCurrentPopup(); }
            else if (m_isImportingHDR && m_importHDRFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) { try { Texture* tex = m_importHDRFuture.get(); static int nextID = 200; auto newAsset = std::make_shared<Asset>(); newAsset->id = nextID++; newAsset->name = m_pendingImportName; newAsset->sourcePath = m_pendingImportPath; newAsset->type = AssetType::TextureHDR; newAsset->hdrTexture = tex; assets.push_back(newAsset); } catch(...) { } m_isImportingHDR = false; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        } ImGui::Render(); return;
    }

    ImGuiIO& io = ImGui::GetIO(); float menuHeight = 22.0f; float rightBarW = 300.0f; float bottomH = 250.0f;
    float screenW = io.DisplaySize.x; float screenH = io.DisplaySize.y; float viewportW = screenW - rightBarW; float viewportH = screenH - bottomH - menuHeight;

    auto HandleAssetDrop = [&](const ImGuiPayload* payload) { int assetID = *(const int*)payload->Data; for(auto& a : assets) { if(a->id == assetID) { GameObject newObj; newObj.name = a->name + (a->type == AssetType::TextureHDR ? " Environment" : " Instance"); newObj.asset = a.get(); newObj.type = (a->type == AssetType::TextureHDR) ? ObjectType::Skybox : ObjectType::Mesh; gameObjects.push_back(newObj); break; } } };

    if (ImGui::GetDragDropPayload() != nullptr) {
        ImGui::SetNextWindowPos(ImVec2(0, menuHeight)); ImGui::SetNextWindowSize(ImVec2(viewportW, viewportH));
        ImGui::Begin("ViewportDrop", nullptr, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::InvisibleButton("##ViewportDropZone", ImGui::GetContentRegionAvail());
        if (ImGui::BeginDragDropTarget()) { if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(PAYLOAD_ASSET)) HandleAssetDrop(payload); ImGui::EndDragDropTarget(); }
        ImGui::End();
    }

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) { if (ImGui::MenuItem("Import 3D Model...")) StartImport(OpenFileDialog("3D Models\0*.obj;*.fbx\0All Files\0*.*\0")); if (ImGui::MenuItem("Import HDR Skybox...")) StartImportHDR(OpenFileDialog("HDR Images\0*.hdr\0All Files\0*.*\0")); ImGui::Separator(); if (ImGui::MenuItem("Exit")) PostQuitMessage(0); ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Place Actors")) {
            auto SpawnPrimitive = [&](const std::string& assetName, const std::string& objName) { Asset* asset = FindAssetByName(assets, assetName); if (asset) { GameObject newObj; newObj.name = objName; newObj.asset = asset; newObj.type = ObjectType::Mesh; gameObjects.push_back(newObj); } };
            if (ImGui::MenuItem("Cube")) SpawnPrimitive("Basic Cube", "Cube"); if (ImGui::MenuItem("Sphere")) SpawnPrimitive("Basic Sphere", "Sphere"); if (ImGui::MenuItem("Plane")) SpawnPrimitive("Basic Plane", "Plane");
            ImGui::SeparatorText("Lights & Environment");
            if (ImGui::MenuItem("Directional Light")) { Asset* sphereAsset = FindAssetByName(assets, "Basic Sphere"); GameObject sun; sun.name = "Directional Light"; sun.position = {0, 10, 0}; sun.rotation = {1.57f, 0, 0}; sun.type = ObjectType::Light; sun.lightIntensity = 1.5f; sun.asset = sphereAsset; gameObjects.push_back(sun); }
            if (ImGui::MenuItem("Post-Process Volume")) { GameObject vol; vol.name = "Post-Process Volume"; vol.type = ObjectType::PostProcessVolume; vol.scale = {5.0f, 5.0f, 5.0f}; gameObjects.push_back(vol); }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    ImGui::SetNextWindowPos(ImVec2(screenW - rightBarW, menuHeight)); ImGui::SetNextWindowSize(ImVec2(rightBarW, viewportH / 2.0f));
    ImGui::Begin("Outliner", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    for (int i = 0; i < gameObjects.size(); i++) { ImGui::PushID(i); if (ImGui::Selectable(gameObjects[i].name.c_str(), selectedIndex == i)) selectedIndex = i; if (ImGui::BeginPopupContextItem()) { if (ImGui::MenuItem("Delete")) { gameObjects.erase(gameObjects.begin() + i); if (selectedIndex == i) selectedIndex = -1; else if (selectedIndex > i) selectedIndex--; i--; ImGui::EndPopup(); ImGui::PopID(); continue; } ImGui::EndPopup(); } ImGui::PopID(); }
    ImVec2 avail = ImGui::GetContentRegionAvail(); if (avail.y < 20) avail.y = 20; ImGui::InvisibleButton("##OutlinerDropZone", avail);
    if (ImGui::BeginDragDropTarget()) { if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(PAYLOAD_ASSET)) HandleAssetDrop(payload); ImGui::EndDragDropTarget(); }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(screenW - rightBarW, menuHeight + (viewportH / 2.0f))); ImGui::SetNextWindowSize(ImVec2(rightBarW, viewportH / 2.0f));
    ImGui::Begin("Details", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    if (selectedIndex < 0 || selectedIndex >= gameObjects.size()) {
        ImGui::SeparatorText("WORLD SETTINGS");
        ImGui::TextDisabled("No object selected.");
        ImGui::SliderFloat("Global Exposure", &globalPP.exposure, 0.1f, 10.0f, "%.2f");
        ImGui::ColorEdit3("Global Tint", &globalPP.colorTint.x);

        // --- NEW: Add Bloom Settings ---
        ImGui::SeparatorText("BLOOM");
        ImGui::SliderFloat("Bloom Threshold", &globalPP.bloomThreshold, 0.1f, 5.0f, "%.2f");
        ImGui::SliderFloat("Bloom Intensity", &globalPP.bloomIntensity, 0.0f, 2.0f, "%.2f");
    }
    else {
        GameObject& obj = gameObjects[selectedIndex];
        char nameBuf[128]; strcpy_s(nameBuf, obj.name.c_str()); if (ImGui::InputText("Name", nameBuf, 128)) obj.name = nameBuf;

        if (obj.type == ObjectType::Skybox) {
            ImGui::Separator(); ImGui::Text("ENVIRONMENT PROPERTIES"); ImGui::TextDisabled("This object provides the scene's skybox.");
            if (obj.asset) { if(ImGui::Button("View Source Image", ImVec2(-1, 0))) { m_editingAsset = obj.asset; m_showAssetEditor = true; ImGui::SetWindowFocus("Asset Editor"); } }
        } else {
            ImGui::Separator(); ImGui::Text("TOOLS");
            if(ImGui::RadioButton("Translate (W)", m_currentGizmoOp == ImGuizmo::TRANSLATE)) m_currentGizmoOp = ImGuizmo::TRANSLATE; ImGui::SameLine(); if(ImGui::RadioButton("Rotate (E)", m_currentGizmoOp == ImGuizmo::ROTATE)) m_currentGizmoOp = ImGuizmo::ROTATE; ImGui::SameLine(); if(ImGui::RadioButton("Scale (R)", m_currentGizmoOp == ImGuizmo::SCALE)) m_currentGizmoOp = ImGuizmo::SCALE;

            ImGui::Separator(); ImGui::Text("TRANSFORM"); ImGui::DragFloat3("Loc", &obj.position.x, 0.1f); ImGui::DragFloat3("Rot", &obj.rotation.x, 0.1f); ImGui::DragFloat3("Scl", &obj.scale.x, 0.1f);

            if (obj.type == ObjectType::PostProcessVolume) {
                ImGui::Separator(); ImGui::Text("VOLUME SETTINGS");
                ImGui::TextDisabled("(Invisible in viewport)");
                ImGui::SliderFloat("Local Exposure", &obj.ppSettings.exposure, 0.1f, 10.0f, "%.2f");
                ImGui::ColorEdit3("Local Tint", &obj.ppSettings.colorTint.x);

                // --- NEW: Add Bloom Settings ---
                ImGui::SliderFloat("Bloom Threshold", &obj.ppSettings.bloomThreshold, 0.1f, 5.0f, "%.2f");
                ImGui::SliderFloat("Bloom Intensity", &obj.ppSettings.bloomIntensity, 0.0f, 2.0f, "%.2f");

                ImGui::SliderFloat("Blend Radius", &obj.ppSettings.blendRadius, 0.0f, 10.0f, "%.2f");
            } else {
                ImGui::Separator(); ImGui::Text("APPEARANCE"); ImGui::ColorEdit4("Base Color", &obj.color.x);
                if (obj.asset && obj.asset->type == AssetType::Mesh) {
                    ImGui::Separator(); if(ImGui::Button("Edit Asset Source", ImVec2(-1, 0))) { m_editingAsset = obj.asset; m_showAssetEditor = true; ImGui::SetWindowFocus("Asset Editor"); }
                    ImGui::Separator(); ImGui::Text("MATERIAL OVERRIDES");
                    auto TextureSlot = [&](const char* label, Texture*& texSlot) { std::string btnText = texSlot ? std::string(label) + ": Set" : std::string(label) + ": Default"; if (ImGui::Button(btnText.c_str(), ImVec2(-1, 0))) { std::string p = OpenFileDialog("Images\0*.png;*.jpg;*.jpeg;*.dds;*.bmp\0All Files\0*.*\0"); if (!p.empty()) { auto t = new Texture(); t->Load(p, m_device, m_cmdQueue); texSlot = t; } } if (texSlot && ImGui::IsItemHovered() && ImGui::IsMouseDown(1)) texSlot = nullptr; };
                    TextureSlot("Albedo", obj.overrideAlbedo); TextureSlot("Normal", obj.overrideNormal); TextureSlot("Metallic", obj.overrideMetallic); TextureSlot("Roughness", obj.overrideRoughness); TextureSlot("AO", obj.overrideAO);
                    ImGui::TextDisabled("(Right-click button to clear)");
                } else if (obj.type == ObjectType::Light) {
                     ImGui::Separator(); ImGui::Text("LIGHT PARAMETERS"); ImGui::DragFloat("Intensity", &obj.lightIntensity, 0.1f);
                }
            }
            
            ImGuizmo::SetRect(0, menuHeight, viewportW, viewportH);
            XMFLOAT4X4 view, proj; XMStoreFloat4x4(&view, viewMatrix); XMStoreFloat4x4(&proj, projMatrix);
            float transform[16]; float t[3]={obj.position.x, obj.position.y, obj.position.z}, r[3]={XMConvertToDegrees(obj.rotation.x), XMConvertToDegrees(obj.rotation.y), XMConvertToDegrees(obj.rotation.z)}, s[3]={obj.scale.x, obj.scale.y, obj.scale.z};
            ImGuizmo::RecomposeMatrixFromComponents(t, r, s, transform);
            ImGuizmo::Manipulate(&view.m[0][0], &proj.m[0][0], m_currentGizmoOp, ImGuizmo::MODE::WORLD, transform);
            if (ImGuizmo::IsUsing()) { float nt[3], nr[3], ns[3]; ImGuizmo::DecomposeMatrixToComponents(transform, nt, nr, ns); obj.position={nt[0],nt[1],nt[2]}; obj.rotation={XMConvertToRadians(nr[0]),XMConvertToRadians(nr[1]),XMConvertToRadians(nr[2])}; obj.scale={ns[0],ns[1],ns[2]}; }
        }
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(0, menuHeight + viewportH)); ImGui::SetNextWindowSize(ImVec2(screenW, bottomH));
    DrawContentBrowser(gameObjects, assets);

    if (m_showAssetEditor && m_editingAsset) { DrawAssetEditorWindow(m_device, m_cmdQueue, previewTex); }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedIndex >= 0 && selectedIndex < gameObjects.size()) { if (!ImGui::GetIO().WantCaptureKeyboard) { gameObjects.erase(gameObjects.begin() + selectedIndex); selectedIndex = -1; } }
    ImGui::Render();
}

void UIManager::DrawContentBrowser(std::vector<GameObject>& gameObjects, std::vector<std::shared_ptr<Asset>>& assets) {
    ImGui::Begin("Content Browser", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::SetNextItemWidth(100); ImGui::SliderFloat("Scale", &m_thumbnailSize, 32.0f, 128.0f); ImGui::Separator();
    
    float cellSize = m_thumbnailSize + m_padding; float panelWidth = ImGui::GetContentRegionAvail().x; int columnCount = (int)(panelWidth / cellSize); if (columnCount < 1) columnCount = 1;
    ImGui::Columns(columnCount, 0, false);
    for (auto& assetPtr : assets) {
        Asset* asset = assetPtr.get(); if (asset->id < 100 && asset->type != AssetType::TextureHDR) continue; 
        ImGui::PushID(asset); ImGui::BeginGroup(); ImVec2 size(m_thumbnailSize, m_thumbnailSize); ImVec2 cursorStart = ImGui::GetCursorPos(); 
        
        if (ImGui::Selectable("##ItemHitbox", (m_editingAsset == asset), ImGuiSelectableFlags_AllowDoubleClick, size)) { if (ImGui::IsMouseDoubleClicked(0)) { m_editingAsset = asset; m_showAssetEditor = true; ImGui::SetWindowFocus("Asset Editor"); } }
        if (ImGui::BeginDragDropSource()) { ImGui::SetDragDropPayload(PAYLOAD_ASSET, &asset->id, sizeof(int)); ImGui::Text("Asset: %s", asset->name.c_str()); ImGui::EndDragDropSource(); }

        ImGui::SetCursorPos(cursorStart); ImVec2 p0 = ImGui::GetItemRectMin(); ImVec2 p1 = ImGui::GetItemRectMax();
        if (asset->type == AssetType::TextureHDR) ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, IM_COL32(200, 100, 50, 255), 4.0f); else ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, IM_COL32(50, 100, 200, 255), 4.0f);
        ImGui::SetCursorPos(ImVec2(cursorStart.x, cursorStart.y + m_thumbnailSize)); std::string displayName = asset->name; if (displayName.length() > 10) displayName = displayName.substr(0, 8) + "..."; ImGui::Text("%s", displayName.c_str());
        ImGui::EndGroup(); ImGui::PopID(); ImGui::NextColumn();
    }
    ImGui::Columns(1); ImGui::End();
}

void UIManager::DrawAssetEditorWindow(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, D3D12_GPU_DESCRIPTOR_HANDLE previewTex) {
    ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver); if (!ImGui::Begin("Asset Editor", &m_showAssetEditor)) { ImGui::End(); return; }
    // if (m_editingAsset->type == AssetType::TextureHDR) {
    //     ImGui::TextColored(ImVec4(1,0.5f,0,1), "HDR Skybox: %s", m_editingAsset->name.c_str()); ImGui::Separator();
    //     if (m_editingAsset->hdrTexture && m_editingAsset->hdrTexture->GetSRVHeap()) { ImVec2 avail = ImGui::GetContentRegionAvail(); float size = avail.x; if(avail.y < size) size = avail.y; ImGui::Image((ImTextureID)m_editingAsset->hdrTexture->GetGPUHandle().ptr, ImVec2(size, size / 2.0f)); } else { ImGui::TextDisabled("No texture loaded."); } ImGui::End(); return;
    // }
    ImGui::Columns(2); ImGui::SetColumnWidth(0, 300); ImGui::TextColored(ImVec4(1,1,0,1), "Editing: %s", m_editingAsset->name.c_str()); ImGui::Separator(); ImGui::Text("MATERIALS");
    std::string texName = m_editingAsset->albedoMap ? "Albedo: Assigned" : "Albedo: Empty"; if(ImGui::Button(texName.c_str(), ImVec2(-1, 0))) { std::string p = OpenFileDialog("Images\0*.png;*.jpg;*.jpeg;*.dds;*.bmp\0All Files\0*.*\0"); if(!p.empty()) { auto t = new Texture(); t->Load(p, device, cmdQueue); m_editingAsset->albedoMap = t; } }
    std::string normName = m_editingAsset->normalMap ? "Normal: Assigned" : "Normal: Empty"; if(ImGui::Button(normName.c_str(), ImVec2(-1, 0))) { std::string p = OpenFileDialog("Images\0*.png;*.jpg;*.jpeg;*.dds;*.bmp\0All Files\0*.*\0"); if(!p.empty()) { auto t = new Texture(); t->Load(p, device, cmdQueue); m_editingAsset->normalMap = t; } }
    ImGui::Text("PBR Properties"); ImGui::SliderFloat("Metallic", &m_editingAsset->metallicFactor, 0.0f, 1.0f); ImGui::SliderFloat("Roughness", &m_editingAsset->roughnessFactor, 0.0f, 1.0f); ImGui::Checkbox("Enable IBL Reflections", &m_editingAsset->enableReflections);
    ImGui::Separator(); ImGui::Text("Quanta Mesh"); ImGui::Checkbox("Enable Virtualization", &m_editingAsset->useVirtualGeometry);
    if (m_editingAsset->useVirtualGeometry) { ImGui::Indent(); ImGui::TextDisabled("Meshlets: %d", (int)m_editingAsset->mesh->GetMeshlets().size()); ImGui::Checkbox("Debug Visualizer", &m_editingAsset->debugVisualizer); ImGui::Unindent(); }
    ImGui::NextColumn(); ImGui::Text("3D Preview (Right-Click Rotate, Scroll Zoom)");
    ImVec2 avail = ImGui::GetContentRegionAvail(); float size = avail.x; if(avail.y < size) size = avail.y; ImGui::Image((ImTextureID)previewTex.ptr, ImVec2(size, size));
    if (ImGui::IsItemHovered()) { float wheel = ImGui::GetIO().MouseWheel; if (wheel != 0.0f) { m_previewDistance -= wheel * 0.1f; if (m_previewDistance < 0.5f) m_previewDistance = 0.5f; if (m_previewDistance > 5.0f) m_previewDistance = 5.0f; } if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) { ImVec2 delta = ImGui::GetIO().MouseDelta; m_previewYaw -= delta.x * 0.01f; m_previewPitch -= delta.y * 0.01f; if (m_previewPitch > 1.5f) m_previewPitch = 1.5f; if (m_previewPitch < -1.5f) m_previewPitch = -1.5f; } }
    ImGui::End();
}

void UIManager::Draw(ID3D12GraphicsCommandList* cmdList) { ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() }; cmdList->SetDescriptorHeaps(1, heaps); ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList); }
void UIManager::Shutdown() { ImGui_ImplDX12_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(); }