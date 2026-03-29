#pragma once
#include "Asset.h"
#include "../Physics/PhysicsTypes.h"
#include <directxmath.h>
#include <vector>

class Material;

enum class ObjectType { Mesh, Light, Skybox, PostProcessVolume };

struct PostProcessSettings {
    float exposure = 1.0f;
    DirectX::XMFLOAT3 colorTint = {1.0f, 1.0f, 1.0f};
    
    float bloomThreshold = 1.0f; 
    float bloomIntensity = 0.5f; 

    float blendRadius = 1.0f;
};

struct GameObject {
    std::string name;

    DirectX::XMFLOAT3 position = {0,0,0};
    DirectX::XMFLOAT3 rotation = {0,0,0};
    DirectX::XMFLOAT3 scale = {1,1,1};

    DirectX::XMFLOAT4 color = {1,1,1,1};
    DirectX::XMFLOAT4 skyHorizonColor = {1,1,1,1};

    Asset* asset = nullptr;
    Material* assignedMaterial = nullptr;

    Texture* overrideAlbedo    = nullptr;
    Texture* overrideNormal    = nullptr;
    Texture* overrideMetallic  = nullptr;
    Texture* overrideRoughness = nullptr;
    Texture* overrideAO        = nullptr;

    std::wstring blueprintAssetPath = L"";
    bool blueprintPlayerControlled = false;
    bool blueprintSpaceJumpEnabled = false;
    float blueprintMoveSpeed = 6.0f;
    float blueprintJumpImpulse = 5.0f;
    float blueprintJumpVelocity = 0.0f;
    float blueprintGroundHeight = 0.0f;
    bool blueprintPossessCamera = false;
    DirectX::XMFLOAT3 blueprintCameraOffset = {0.0f, 2.0f, -5.0f};
    bool blueprintHasTrigger = false;
    PhysicsColliderShape blueprintTriggerShape = PhysicsColliderShape::Box;
    DirectX::XMFLOAT3 blueprintTriggerOffset = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 blueprintTriggerExtents = {0.75f, 0.75f, 0.75f};
    float blueprintTriggerRadius = 0.75f;
    std::vector<std::wstring> blueprintViewportWidgetAssetPaths;

    ObjectType type = ObjectType::Mesh;
    float lightIntensity = 1.0f;

    PostProcessSettings ppSettings;
    PhysicsComponent physics;
};
