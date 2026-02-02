#pragma once
#include <string>
#include <directxmath.h>
#include "../Resources/Mesh.h" 

struct GameObject {
    std::string name;
    DirectX::XMFLOAT3 position = { 0, 0, 0 };
    DirectX::XMFLOAT3 rotation = { 0, 0, 0 };
    DirectX::XMFLOAT3 scale = { 1, 1, 1 };
    DirectX::XMFLOAT4 color = { 1, 1, 1, 1 };

    Mesh* mesh = nullptr; 
};