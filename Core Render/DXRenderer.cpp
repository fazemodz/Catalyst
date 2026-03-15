#include "DXRenderer.h"
#include <d3dcompiler.h>
#include <iostream>
#include <DirectXCollision.h> 
#include <cmath>

using namespace DirectX;
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

void DXRenderer::Initialize(HWND hwnd, int width, int height) {
    m_width = width; m_height = height;

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

    DXGI_SWAP_CHAIN_DESC1 scDesc = {}; scDesc.Width = width; scDesc.Height = height; scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; scDesc.SampleDesc = { 1, 0 }; scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; scDesc.BufferCount = FrameCount; scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> sc1; ThrowIfFailed(factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &scDesc, nullptr, nullptr, &sc1));
    sc1.As(&m_swapChain); m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FrameCount }; ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap)));
    uint32_t rtvSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < FrameCount; i++) { ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]))); m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle); rtvHandle.ptr += rtvSize; }

    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList))); m_commandList->Close();

    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))); m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    CreateDepthBuffer();
    CreateConstantBuffer(); 
    
    // Setup Bindless
    m_bindlessManager.Initialize(m_device.Get(), 1024);
    CreateDefaultTextures(); 
    CreateFrameHeap();

    m_shadowPass.Initialize(m_device.Get()); 
    m_postProcessPass.Initialize(m_device.Get(), width, height);
    m_quantaMeshPass.Initialize(m_device.Get());

    m_primitives["Cube"] = PrimitiveGenerator::CreateCube(m_device.Get(), m_commandQueue.Get()); 
    m_primitives["Sphere"] = PrimitiveGenerator::CreateSphere(m_device.Get(), m_commandQueue.Get()); 
    m_primitives["Plane"] = PrimitiveGenerator::CreatePlane(m_device.Get(), m_commandQueue.Get()); 
    m_primitives["Cylinder"] = PrimitiveGenerator::CreateCylinder(m_device.Get(), m_commandQueue.Get());

    auto cubeAsset = std::make_shared<Asset>(); cubeAsset->id = 0; cubeAsset->name = "Basic Cube"; cubeAsset->mesh = m_primitives["Cube"]; m_assets.push_back(cubeAsset);
    auto sphereAsset = std::make_shared<Asset>(); sphereAsset->id = 1; sphereAsset->name = "Basic Sphere"; sphereAsset->mesh = m_primitives["Sphere"]; m_assets.push_back(sphereAsset);
    auto planeAsset = std::make_shared<Asset>(); planeAsset->id = 2; planeAsset->name = "Basic Plane"; planeAsset->mesh = m_primitives["Plane"]; m_assets.push_back(planeAsset);

    auto skyAsset = std::make_shared<Asset>(); 
    skyAsset->id = 3; 
    skyAsset->name = "Default Sky"; 
    skyAsset->type = AssetType::TextureHDR; 
    skyAsset->hdrTexture = new Texture();
    
    try { 
        skyAsset->hdrTexture->LoadHDR("Assets/sky.hdr", m_device.Get(), m_commandQueue.Get()); 
    } catch (...) { 
        skyAsset->hdrTexture->Create1x1Color(m_device.Get(), m_commandQueue.Get(), 0xFF444444); 
    }
    
    // Bindless Registration for Skybox
    if (skyAsset->hdrTexture && skyAsset->hdrTexture->GetResource()) {
        uint32_t skyIndex = m_bindlessManager.AddTexture(m_device.Get(), skyAsset->hdrTexture->GetResource());
        skyAsset->hdrTexture->SetBindlessIndex(skyIndex);
    }
    m_assets.push_back(skyAsset);

    m_ui.Initialize(hwnd, m_device.Get(), m_commandQueue.Get(), FrameCount); m_ui.SetPrimitives(m_primitives); 
    m_previewPass.Initialize(m_device.Get(), m_ui.GetSRVHeap());
    m_camera.SetProjection(45.0f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 5000.0f);

    // Shift objects forward (+Z) and floor down (-Y) so they are visible from the origin (0,0,0)
    GameObject skyObj; skyObj.name = "Skybox Environment"; skyObj.type = ObjectType::Skybox; skyObj.asset = skyAsset.get(); m_gameObjects.push_back(skyObj);
    GameObject floor; floor.name = "Floor"; floor.position = {0, -3.0f, 15.0f}; floor.scale = {10,1,10}; floor.color = {0.3f, 0.3f, 0.3f, 1}; floor.asset = m_assets[2].get(); m_gameObjects.push_back(floor);
    GameObject sun; sun.name = "Sun Light"; sun.position = {0, 10.0f, 15.0f}; sun.rotation = {0.78f, 0.78f, 0}; sun.scale = {1,1,1}; sun.color = {1,1,1,1}; sun.asset = m_assets[1].get(); sun.type = ObjectType::Light; sun.lightIntensity = 1.5f; m_gameObjects.push_back(sun);
}

void DXRenderer::OnResize(int width, int height) {
    if (width == 0 || height == 0) return; FlushGPU(); m_width = width; m_height = height;
    for (int i = 0; i < FrameCount; ++i) m_renderTargets[i].Reset(); m_depthBuffer.Reset();
    ThrowIfFailed(m_swapChain->ResizeBuffers(FrameCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0)); m_frameIndex = 0;
    uint32_t rtvSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV); D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < FrameCount; i++) { ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]))); m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle); rtvHandle.ptr += rtvSize; }
    CreateDepthBuffer();
    m_postProcessPass.OnResize(m_device.Get(), width, height); 
    m_camera.SetProjection(45.0f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 5000.0f); 
}

void DXRenderer::Render() {
    m_camera.Update(0.016f); 
    if (ImGui::IsMouseClicked(0)) { if (!ImGui::GetIO().WantCaptureMouse) { POINT pt; GetCursorPos(&pt); ScreenToClient(GetActiveWindow(), &pt); PickObject(pt.x, pt.y); } }

    if (Asset* editing = m_ui.GetEditingAsset()) {
        if (editing->type == AssetType::Mesh) {
            m_commandAllocator->Reset(); m_commandList->Reset(m_commandAllocator.Get(), m_quantaMeshPass.GetPreviewPipelineState());
            m_previewPass.Render(m_commandList.Get(), editing, m_quantaMeshPass.GetPreviewRootSignature(), m_quantaMeshPass.GetPreviewPipelineState(), m_constantBuffer.Get(), m_pCbvDataBegin, m_dsvHeap->GetCPUDescriptorHandleForHeapStart(), m_texWhite, m_ui.GetPreviewYaw(), m_ui.GetPreviewPitch(), m_ui.GetPreviewDistance());
            m_commandList->Close(); ID3D12CommandList* lists[] = { m_commandList.Get() }; m_commandQueue->ExecuteCommandLists(1, lists); FlushGPU();
        }
    }

    XMMATRIX mView = m_camera.GetViewMatrix(); XMMATRIX mProj = m_camera.GetProjectionMatrix();
    m_ui.Update(m_gameObjects, m_assets, m_selectedObjectIndex, mView, mProj, m_previewPass.GetOutputHandle(), m_globalPP);
    m_commandAllocator->Reset(); m_commandList->Reset(m_commandAllocator.Get(), nullptr);

    DirectX::XMFLOAT3 activeLightDir = { 0, -1, 0 }; float activeIntensity = 0.0f; Texture* activeSkybox = nullptr;
    XMMATRIX lightViewProj = XMMatrixIdentity(); XMMATRIX lightSpaceMatrix = XMMatrixIdentity();

    for (const auto& obj : m_gameObjects) {
        if (obj.type == ObjectType::Light) { 
            XMMATRIX lightRot = XMMatrixRotationRollPitchYaw(obj.rotation.x, obj.rotation.y, obj.rotation.z); XMVECTOR forwardVar = XMVector3TransformCoord(XMVectorSet(0, 0, 1, 0), lightRot); XMStoreFloat3(&activeLightDir, forwardVar); activeIntensity = obj.lightIntensity; 
            XMMATRIX lightView = XMMatrixLookAtLH(XMVectorSet(activeLightDir.x * -50.0f, activeLightDir.y * -50.0f, activeLightDir.z * -50.0f, 1.0f), XMVectorSet(0, 0, 0, 1), XMVectorSet(0, 1, 0, 0)); XMMATRIX lightProj = XMMatrixOrthographicLH(100.0f, 100.0f, 1.0f, 200.0f);
            lightViewProj = lightView * lightProj; lightSpaceMatrix = XMMatrixTranspose(lightViewProj);
        } else if (obj.type == ObjectType::Skybox && obj.asset && obj.asset->hdrTexture) activeSkybox = obj.asset->hdrTexture;
    }

    UINT objSize = (sizeof(ConstantBufferData) + 255) & ~255; D3D12_GPU_VIRTUAL_ADDRESS cbAddress = m_constantBuffer->GetGPUVirtualAddress(); 
    m_shadowPass.Render(m_commandList.Get(), m_gameObjects, m_pCbvDataBegin, cbAddress, objSize, lightViewProj);

    m_postProcessPass.TransitionToRTV(m_commandList.Get());
    m_postProcessPass.Clear(m_commandList.Get());
    D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv = m_postProcessPass.GetRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    m_commandList->OMSetRenderTargets(1, &hdrRtv, FALSE, &dsv);
    m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // Bindless setup
    ID3D12DescriptorHeap* heaps[] = { m_bindlessManager.GetHeap() }; 
    m_commandList->SetDescriptorHeaps(1, heaps);

    m_quantaMeshPass.Render(m_commandList.Get(), m_device.Get(), m_gameObjects, m_camera, m_width, m_height, 
                            lightSpaceMatrix, activeLightDir, activeIntensity, activeSkybox, 
                            m_bindlessManager.GetHeap(), m_device.Get()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV), m_frameHeapOffset, 
                            m_texWhite, m_texNormal, m_texBlack, m_shadowPass.GetSRVHeap(),
                            m_primitives["Sphere"]); 

    m_postProcessPass.TransitionToSRV(m_commandList.Get());
    D3D12_RESOURCE_BARRIER swapBarrier = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION }; swapBarrier.Transition.pResource = m_renderTargets[m_frameIndex].Get(); swapBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT; swapBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET; m_commandList->ResourceBarrier(1, &swapBarrier);
    D3D12_CPU_DESCRIPTOR_HANDLE swapRtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart(); swapRtv.ptr += m_frameIndex * m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_commandList->OMSetRenderTargets(1, &swapRtv, FALSE, nullptr);
    const float clearBlack[] = { 0.0f, 0.0f, 0.0f, 1.0f }; m_commandList->ClearRenderTargetView(swapRtv, clearBlack, 0, nullptr);

    m_postProcessPass.Render(m_commandList.Get(), m_camera, m_gameObjects, m_globalPP, m_width, m_height);
    m_ui.Draw(m_commandList.Get());

    swapBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; swapBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT; m_commandList->ResourceBarrier(1, &swapBarrier); m_commandList->Close();
    ID3D12CommandList* lists[] = { m_commandList.Get() }; m_commandQueue->ExecuteCommandLists(1, lists); m_swapChain->Present(1, 0); FlushGPU(); m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void DXRenderer::CreateDefaultTextures() { 
    m_texWhite = new Texture(); 
    try { 
        m_texWhite->Load("Assets/white.png", m_device.Get(), m_commandQueue.Get()); 
    } catch(...) { 
        m_texWhite->Create1x1Color(m_device.Get(), m_commandQueue.Get(), 0xFFFFFFFF); 
    } 
    m_texWhite->SetBindlessIndex(m_bindlessManager.AddTexture(m_device.Get(), m_texWhite->GetResource()));

    m_texBlack = new Texture(); 
    m_texBlack->Create1x1Color(m_device.Get(), m_commandQueue.Get(), 0xFF000000); 
    m_texBlack->SetBindlessIndex(m_bindlessManager.AddTexture(m_device.Get(), m_texBlack->GetResource()));

    m_texNormal = new Texture(); 
    m_texNormal->Create1x1Color(m_device.Get(), m_commandQueue.Get(), 0xFFFF7F7F); 
    m_texNormal->SetBindlessIndex(m_bindlessManager.AddTexture(m_device.Get(), m_texNormal->GetResource()));
}

void DXRenderer::CreateFrameHeap() { 
    // Deprecated/Unused in favor of Bindless 
}

void DXRenderer::CreateDepthBuffer() { 
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {}; 
    dsvHeapDesc.NumDescriptors = 1; 
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; 
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; 
    ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap))); 
    
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
    ThrowIfFailed(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearVal, IID_PPV_ARGS(&m_depthBuffer))); 
    
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvViewDesc = {}; 
    dsvViewDesc.Format = DXGI_FORMAT_D32_FLOAT; 
    dsvViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; 
    dsvViewDesc.Flags = D3D12_DSV_FLAG_NONE; 
    m_device->CreateDepthStencilView(m_depthBuffer.Get(), &dsvViewDesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart()); 
}

void DXRenderer::CreateConstantBuffer() { 
    const UINT objSize = (sizeof(ConstantBufferData) + 255) & ~255; 
    const UINT bufferSize = objSize * MAX_OBJECTS; 
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_UPLOAD }; 
    D3D12_RESOURCE_DESC rd = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, bufferSize, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1,0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE }; 
    ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_constantBuffer))); 
    ThrowIfFailed(m_constantBuffer->Map(0, nullptr, (void**)&m_pCbvDataBegin)); 
}

void DXRenderer::PickObject(int mouseX, int mouseY) {
    using namespace DirectX;
    float vx = (2.0f * mouseX / m_width - 1.0f);
    float vy = (-2.0f * mouseY / m_height + 1.0f);
    XMMATRIX proj = m_camera.GetProjectionMatrix();
    XMMATRIX view = m_camera.GetViewMatrix();
    XMMATRIX invProj = XMMatrixInverse(nullptr, proj);
    XMMATRIX invView = XMMatrixInverse(nullptr, view);
    XMVECTOR rayOrigin = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    XMVECTOR rayDir = XMVectorSet(vx, vy, 1.0f, 0.0f);
    rayDir = XMVector4Transform(rayDir, invProj);
    rayDir = XMVectorSet(XMVectorGetX(rayDir), XMVectorGetY(rayDir), XMVectorGetZ(rayDir), 0.0f);
    rayOrigin = XMVector3TransformCoord(rayOrigin, invView);
    rayDir = XMVector3TransformNormal(rayDir, invView);
    rayDir = XMVector3Normalize(rayDir);
    float closestDist = FLT_MAX;
    int hitIndex = -1;
    for (int i = 0; i < m_gameObjects.size(); ++i) {
        const auto& obj = m_gameObjects[i];
        if (obj.type == ObjectType::Skybox || obj.type == ObjectType::Light || obj.type == ObjectType::PostProcessVolume) continue;
        float maxScale = max(obj.scale.x, max(obj.scale.y, obj.scale.z));
        BoundingSphere sphere(obj.position, maxScale);
        float dist;
        if (sphere.Intersects(rayOrigin, rayDir, dist)) {
            if (dist < closestDist) { closestDist = dist; hitIndex = i; }
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
    for (auto& pair : m_primitives) delete pair.second; 
    delete m_texWhite; delete m_texBlack; delete m_texNormal; 
    if (m_fenceEvent) CloseHandle(m_fenceEvent); 
}