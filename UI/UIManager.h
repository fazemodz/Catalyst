#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <directxmath.h>
#include "../Scene/GameObject.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"
#include "ImGuizmo.h"

using Microsoft::WRL::ComPtr;

class UIManager {
public:
    void Initialize(HWND hwnd, ID3D12Device* device, ID3D12CommandQueue* commandQueue, int frameCount);
    void Update(std::vector<GameObject>& gameObjects, int& selectedIndex, const DirectX::XMMATRIX& viewMatrix, const DirectX::XMMATRIX& projMatrix);
    void Draw(ID3D12GraphicsCommandList* cmdList);
    void Shutdown();

private:
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
};