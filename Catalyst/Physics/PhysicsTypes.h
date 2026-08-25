#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <directxmath.h>

enum class PhysicsBodyType {
    Static,
    Dynamic,
    Kinematic
};

enum class PhysicsColliderShape {
    Box,
    Sphere
};

enum class BlueprintFieldType {
    Bool,
    Float,
    Float3,
    Enum
};

struct BlueprintFieldDescriptor {
    const char* name = "";
    const char* category = "";
    BlueprintFieldType type = BlueprintFieldType::Float;
    size_t offset = 0;
};

struct RigidBodyComponent {
    bool enabled = false;
    PhysicsBodyType bodyType = PhysicsBodyType::Dynamic;
    bool useGravity = true;
    float mass = 1.0f;
    float linearDamping = 0.12f;
    float restitution = 0.2f;
    DirectX::XMFLOAT3 velocity = {0.0f, 0.0f, 0.0f};
};

struct ColliderComponent {
    bool enabled = false;
    PhysicsColliderShape shape = PhysicsColliderShape::Box;
    bool isTrigger = false;
    DirectX::XMFLOAT3 centerOffset = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 boxExtents = {0.5f, 0.5f, 0.5f};
    float sphereRadius = 0.5f;
};

struct PhysicsComponent {
    RigidBodyComponent rigidBody;
    ColliderComponent collider;
};

const char* PhysicsBodyTypeToString(PhysicsBodyType type);
PhysicsBodyType PhysicsBodyTypeFromString(const std::string& value);

const char* PhysicsColliderShapeToString(PhysicsColliderShape shape);
PhysicsColliderShape PhysicsColliderShapeFromString(const std::string& value);

std::span<const BlueprintFieldDescriptor> GetRigidBodyBlueprintFields();
std::span<const BlueprintFieldDescriptor> GetColliderBlueprintFields();
