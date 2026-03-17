#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <DirectXMath.h>
#include "RenderTypes.h"

enum class PropertyType { Float, Vector4, Texture };

struct MaterialProperty {
    PropertyType type;
    std::string name;
    float floatVal;
    DirectX::XMFLOAT4 vec4Val;
    int textureIndex; 
};

class Material {
public:
    std::string name;
    std::unordered_map<std::string, MaterialProperty> properties;

    void SetFloat(const std::string& name, float val) {
        properties[name] = { PropertyType::Float, name, val, {0,0,0,0}, -1 };
    }

    void SetVector(const std::string& name, DirectX::XMFLOAT4 val) {
        properties[name] = { PropertyType::Vector4, name, 0, val, -1 };
    }

    void SetTexture(const std::string& name, int index) {
        properties[name] = { PropertyType::Texture, name, 0, {0,0,0,0}, index };
    }
};