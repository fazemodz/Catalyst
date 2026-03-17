#pragma once
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl.h>
#include <vector>
#include <map>
#include <memory>
#include <string>

#include "Camera.h"
#include "GameObject.h"
#include "Texture.h"
#include "Mesh.h"
#include "Asset.h"
#include "BindlessManager.h"
#include "ShadowPass.h"
#include "PostProcessPass.h"
#include "QuantaMeshPass.h"
#include "UIRenderer.h"
#include "UIDrawList.h"
#include "FontManager.h"
#include "RenderTypes.h"
#include "UIContext.h"
#include "../Launcher.h"

using Microsoft::WRL::ComPtr;

enum class EngineState {
    Launcher,
    Editor
};

class DXRenderer {
public:
    void Initialize(HWND hwnd, int width, int height);
    void OnResize(int width, int height);
    void Render();
    void Shutdown();

private:
    static const int FrameCount = 2;

    int m_width = 0;
    int m_height = 0;
    uint32_t m_frameIndex = 0;
    uint64_t m_fenceValue = 0;
    UINT m_frameHeapOffset = 0;

    HWND m_hwnd;
    EngineState m_engineState = EngineState::Launcher;

    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12Resource> m_depthBuffer;
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;

    ComPtr<ID3D12Resource> m_constantBuffer;
    void* m_pCbvDataBegin = nullptr;

    BindlessManager m_bindlessManager;
    ShadowPass m_shadowPass;
    PostProcessPass m_postProcessPass;
    QuantaMeshPass m_quantaMeshPass;
    
    UIRenderer m_uiRenderer;
    UIDrawList m_uiDrawList;
    FontManager m_fontManager;
    UIContext m_uiContext;

    Camera m_camera;
    std::map<std::string, Mesh*> m_primitives;
    std::vector<std::shared_ptr<Asset>> m_assets;
    std::vector<GameObject> m_gameObjects;

    Texture* m_texWhite = nullptr;
    Texture* m_texBlack = nullptr;
    Texture* m_texNormal = nullptr;

    void CreateDefaultTextures();
    void CreateDepthBuffer();
    void CreateConstantBuffer();
    void FlushGPU();
};