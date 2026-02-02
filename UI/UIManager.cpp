#include "UIManager.h"
#include <stdexcept>

using namespace DirectX;

void UIManager::Initialize(HWND hwnd, ID3D12Device* device, ID3D12CommandQueue* commandQueue, int frameCount) {
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.NumDescriptors = 64;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    
    if (FAILED(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap)))) {
        throw std::runtime_error("Failed to create UI Heap");
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

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

    if (!ImGui_ImplDX12_Init(&init_info)) {
        throw std::runtime_error("Failed to init ImGui DX12");
    }

    ImGui_ImplDX12_CreateDeviceObjects();
}

void UIManager::Update(std::vector<GameObject>& gameObjects, int& selectedIndex, const XMMATRIX& viewMatrix, const XMMATRIX& projMatrix) {
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    ImGuiIO& io = ImGui::GetIO();
    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

    ImGui::Begin("Hierarchy");
    if (ImGui::Button("Create Cube")) {
        gameObjects.push_back({ "New Cube", {0,2,0}, {0,0,0}, {1,1,1}, {1,1,1,1} });
    }
    ImGui::Separator();
    for (int i = 0; i < gameObjects.size(); i++) {
        ImGui::PushID(i);
        if (ImGui::Selectable(gameObjects[i].name.c_str(), selectedIndex == i)) {
            selectedIndex = i;
        }
        ImGui::PopID();
    }
    ImGui::End();

    ImGui::Begin("Inspector");
    if (selectedIndex >= 0 && selectedIndex < gameObjects.size()) {
        GameObject& obj = gameObjects[selectedIndex];
        ImGui::PushID("Inspector");
        
        char nameBuf[128];
        strcpy_s(nameBuf, obj.name.c_str());
        if (ImGui::InputText("Name", nameBuf, 128)) obj.name = nameBuf;

        ImGui::DragFloat3("Position", &obj.position.x, 0.05f);
        ImGui::DragFloat3("Rotation", &obj.rotation.x, 0.05f);
        ImGui::DragFloat3("Scale",    &obj.scale.x,    0.05f);
        ImGui::ColorEdit4("Color",    &obj.color.x);

        ImGui::PopID();

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
            float newTranslation[3], newRotation[3], newScale[3];
            ImGuizmo::DecomposeMatrixToComponents(transform, newTranslation, newRotation, newScale);

            obj.position = { newTranslation[0], newTranslation[1], newTranslation[2] };
            obj.rotation = { XMConvertToRadians(newRotation[0]), XMConvertToRadians(newRotation[1]), XMConvertToRadians(newRotation[2]) };
            obj.scale    = { newScale[0], newScale[1], newScale[2] };
        }
    }
    ImGui::End();

    ImGui::Begin("Performance");
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::End();

    ImGui::Render();
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