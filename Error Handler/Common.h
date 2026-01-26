#pragma once
#include <windows.h>
#include <stdexcept>
#include <wrl.h>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
inline void ThrowIfFailed(HRESULT hr) {
    if (FAILED(hr)) throw std::runtime_error("DirectX Error");
}