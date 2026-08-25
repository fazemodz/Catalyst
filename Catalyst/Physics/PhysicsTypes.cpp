#include "PhysicsTypes.h"

#include <array>

namespace {
const std::array<BlueprintFieldDescriptor, 6> kRigidBodyBlueprintFields = {{
    {"Enabled", "Physics.RigidBody", BlueprintFieldType::Bool, offsetof(RigidBodyComponent, enabled)},
    {"BodyType", "Physics.RigidBody", BlueprintFieldType::Enum, offsetof(RigidBodyComponent, bodyType)},
    {"UseGravity", "Physics.RigidBody", BlueprintFieldType::Bool, offsetof(RigidBodyComponent, useGravity)},
    {"Mass", "Physics.RigidBody", BlueprintFieldType::Float, offsetof(RigidBodyComponent, mass)},
    {"LinearDamping", "Physics.RigidBody", BlueprintFieldType::Float, offsetof(RigidBodyComponent, linearDamping)},
    {"Velocity", "Physics.RigidBody", BlueprintFieldType::Float3, offsetof(RigidBodyComponent, velocity)},
}};

const std::array<BlueprintFieldDescriptor, 6> kColliderBlueprintFields = {{
    {"Enabled", "Physics.Collider", BlueprintFieldType::Bool, offsetof(ColliderComponent, enabled)},
    {"Shape", "Physics.Collider", BlueprintFieldType::Enum, offsetof(ColliderComponent, shape)},
    {"IsTrigger", "Physics.Collider", BlueprintFieldType::Bool, offsetof(ColliderComponent, isTrigger)},
    {"CenterOffset", "Physics.Collider", BlueprintFieldType::Float3, offsetof(ColliderComponent, centerOffset)},
    {"BoxExtents", "Physics.Collider", BlueprintFieldType::Float3, offsetof(ColliderComponent, boxExtents)},
    {"SphereRadius", "Physics.Collider", BlueprintFieldType::Float, offsetof(ColliderComponent, sphereRadius)},
}};
}

const char* PhysicsBodyTypeToString(PhysicsBodyType type) {
    switch (type) {
    case PhysicsBodyType::Static:
        return "Static";
    case PhysicsBodyType::Kinematic:
        return "Kinematic";
    case PhysicsBodyType::Dynamic:
    default:
        return "Dynamic";
    }
}

PhysicsBodyType PhysicsBodyTypeFromString(const std::string& value) {
    if (value == "Static") {
        return PhysicsBodyType::Static;
    }
    if (value == "Kinematic") {
        return PhysicsBodyType::Kinematic;
    }
    return PhysicsBodyType::Dynamic;
}

const char* PhysicsColliderShapeToString(PhysicsColliderShape shape) {
    switch (shape) {
    case PhysicsColliderShape::Sphere:
        return "Sphere";
    case PhysicsColliderShape::Box:
    default:
        return "Box";
    }
}

PhysicsColliderShape PhysicsColliderShapeFromString(const std::string& value) {
    if (value == "Sphere") {
        return PhysicsColliderShape::Sphere;
    }
    return PhysicsColliderShape::Box;
}

std::span<const BlueprintFieldDescriptor> GetRigidBodyBlueprintFields() {
    return kRigidBodyBlueprintFields;
}

std::span<const BlueprintFieldDescriptor> GetColliderBlueprintFields() {
    return kColliderBlueprintFields;
}
