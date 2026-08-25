#include "PhysicsSystem.h"

#include <algorithm>
#include <cmath>

#include "../Resources/Mesh.h"
#include "../Scene/GameObject.h"

using namespace DirectX;

namespace {
constexpr float kFixedTimeStep = 1.0f / 60.0f;
constexpr float kMaximumAccumulatedTime = 0.25f;
constexpr float kMinimumMass = 0.001f;
constexpr float kMinimumColliderSize = 0.05f;
constexpr float kCorrectionPercent = 0.8f;
constexpr float kCorrectionSlop = 0.001f;

struct ColliderWorldState {
    XMFLOAT3 center = {0.0f, 0.0f, 0.0f};
    XMFLOAT3 boxExtents = {0.5f, 0.5f, 0.5f};
    float sphereRadius = 0.5f;
};

struct CollisionManifold {
    bool intersects = false;
    XMFLOAT3 normal = {0.0f, 1.0f, 0.0f};
    float penetration = 0.0f;
};

float ClampFloat(float value, float minimum, float maximum) {
    return (std::max)(minimum, (std::min)(maximum, value));
}

XMFLOAT3 Add(const XMFLOAT3& a, const XMFLOAT3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

XMFLOAT3 Subtract(const XMFLOAT3& a, const XMFLOAT3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

XMFLOAT3 Scale(const XMFLOAT3& value, float scalar) {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

XMFLOAT3 Multiply(const XMFLOAT3& a, const XMFLOAT3& b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

XMFLOAT3 Abs(const XMFLOAT3& value) {
    return {std::abs(value.x), std::abs(value.y), std::abs(value.z)};
}

float Dot(const XMFLOAT3& a, const XMFLOAT3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
 
float LengthSquared(const XMFLOAT3& value) {
    return Dot(value, value);
}

float Length(const XMFLOAT3& value) {
    return std::sqrt(LengthSquared(value));
}

XMFLOAT3 Normalize(const XMFLOAT3& value) {
    const float length = Length(value);
    if (length <= 0.00001f) {
        return {0.0f, 1.0f, 0.0f};
    }
    return Scale(value, 1.0f / length);
}

XMFLOAT3 TransformNormal(const XMFLOAT3& value, const XMMATRIX& matrix) {
    XMFLOAT3 transformed;
    XMStoreFloat3(&transformed, XMVector3TransformNormal(XMLoadFloat3(&value), matrix));
    return transformed;
}

float MaxComponent(const XMFLOAT3& value) {
    return (std::max)(value.x, (std::max)(value.y, value.z));
}

bool IsDynamicBody(const GameObject& object) {
    return object.physics.rigidBody.enabled && object.physics.rigidBody.bodyType == PhysicsBodyType::Dynamic;
}

bool IsKinematicBody(const GameObject& object) {
    return object.physics.rigidBody.enabled && object.physics.rigidBody.bodyType == PhysicsBodyType::Kinematic;
}

bool HasSolidCollider(const GameObject& object) {
    return object.physics.collider.enabled && !object.physics.collider.isTrigger;
}

float GetInverseMass(const GameObject& object) {
    if (!IsDynamicBody(object)) {
        return 0.0f;
    }
    return 1.0f / (std::max)(object.physics.rigidBody.mass, kMinimumMass);
}

void ClampPhysicsSettings(GameObject& object) {
    object.physics.rigidBody.mass = (std::max)(object.physics.rigidBody.mass, kMinimumMass);
    object.physics.rigidBody.linearDamping = (std::max)(0.0f, object.physics.rigidBody.linearDamping);
    object.physics.rigidBody.restitution = ClampFloat(object.physics.rigidBody.restitution, 0.0f, 1.0f);
    object.physics.collider.boxExtents.x = (std::max)(object.physics.collider.boxExtents.x, kMinimumColliderSize);
    object.physics.collider.boxExtents.y = (std::max)(object.physics.collider.boxExtents.y, kMinimumColliderSize);
    object.physics.collider.boxExtents.z = (std::max)(object.physics.collider.boxExtents.z, kMinimumColliderSize);
    object.physics.collider.sphereRadius = (std::max)(object.physics.collider.sphereRadius, kMinimumColliderSize);
}

ColliderWorldState BuildColliderWorldState(const GameObject& object) {
    ColliderWorldState worldState;
    const XMFLOAT3 absoluteScale = Abs(object.scale);
    const XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw(object.rotation.x, object.rotation.y, object.rotation.z);

    const XMFLOAT3 scaledOffset = Multiply(object.physics.collider.centerOffset, object.scale);
    const XMFLOAT3 rotatedOffset = TransformNormal(scaledOffset, rotationMatrix);
    worldState.center = Add(object.position, rotatedOffset);

    const XMFLOAT3 scaledExtents = {
        (std::max)(kMinimumColliderSize, object.physics.collider.boxExtents.x * absoluteScale.x),
        (std::max)(kMinimumColliderSize, object.physics.collider.boxExtents.y * absoluteScale.y),
        (std::max)(kMinimumColliderSize, object.physics.collider.boxExtents.z * absoluteScale.z)
    };

    const XMFLOAT3 axisX = TransformNormal({scaledExtents.x, 0.0f, 0.0f}, rotationMatrix);
    const XMFLOAT3 axisY = TransformNormal({0.0f, scaledExtents.y, 0.0f}, rotationMatrix);
    const XMFLOAT3 axisZ = TransformNormal({0.0f, 0.0f, scaledExtents.z}, rotationMatrix);
    worldState.boxExtents = {
        (std::max)(kMinimumColliderSize, std::abs(axisX.x) + std::abs(axisY.x) + std::abs(axisZ.x)),
        (std::max)(kMinimumColliderSize, std::abs(axisX.y) + std::abs(axisY.y) + std::abs(axisZ.y)),
        (std::max)(kMinimumColliderSize, std::abs(axisX.z) + std::abs(axisY.z) + std::abs(axisZ.z))
    };

    worldState.sphereRadius = (std::max)(kMinimumColliderSize, object.physics.collider.sphereRadius * MaxComponent(absoluteScale));
    return worldState;
}

CollisionManifold BuildSphereSphereManifold(const ColliderWorldState& a, const ColliderWorldState& b) {
    CollisionManifold manifold;
    const XMFLOAT3 delta = Subtract(b.center, a.center);
    const float radiusSum = a.sphereRadius + b.sphereRadius;
    const float distanceSquared = LengthSquared(delta);
    if (distanceSquared > radiusSum * radiusSum) {
        return manifold;
    }

    const float distance = std::sqrt(distanceSquared);
    manifold.intersects = true;
    manifold.normal = distance > 0.00001f ? Scale(delta, 1.0f / distance) : XMFLOAT3{0.0f, 1.0f, 0.0f};
    manifold.penetration = radiusSum - distance;
    return manifold;
}

CollisionManifold BuildBoxBoxManifold(const ColliderWorldState& a, const ColliderWorldState& b) {
    CollisionManifold manifold;
    const XMFLOAT3 delta = Subtract(b.center, a.center);
    const XMFLOAT3 overlap = {
        a.boxExtents.x + b.boxExtents.x - std::abs(delta.x),
        a.boxExtents.y + b.boxExtents.y - std::abs(delta.y),
        a.boxExtents.z + b.boxExtents.z - std::abs(delta.z)
    };

    if (overlap.x <= 0.0f || overlap.y <= 0.0f || overlap.z <= 0.0f) {
        return manifold;
    }

    manifold.intersects = true;
    manifold.penetration = overlap.x;
    manifold.normal = {delta.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f};

    if (overlap.y < manifold.penetration) {
        manifold.penetration = overlap.y;
        manifold.normal = {0.0f, delta.y >= 0.0f ? 1.0f : -1.0f, 0.0f};
    }

    if (overlap.z < manifold.penetration) {
        manifold.penetration = overlap.z;
        manifold.normal = {0.0f, 0.0f, delta.z >= 0.0f ? 1.0f : -1.0f};
    }

    return manifold;
}

CollisionManifold BuildSphereBoxManifold(const ColliderWorldState& sphere, const ColliderWorldState& box) {
    CollisionManifold manifold;
    const XMFLOAT3 sphereLocal = Subtract(sphere.center, box.center);
    const XMFLOAT3 closestLocal = {
        ClampFloat(sphereLocal.x, -box.boxExtents.x, box.boxExtents.x),
        ClampFloat(sphereLocal.y, -box.boxExtents.y, box.boxExtents.y),
        ClampFloat(sphereLocal.z, -box.boxExtents.z, box.boxExtents.z)
    };

    const XMFLOAT3 closestPoint = Add(box.center, closestLocal);
    const XMFLOAT3 delta = Subtract(closestPoint, sphere.center);
    const float distanceSquared = LengthSquared(delta);
    if (distanceSquared > sphere.sphereRadius * sphere.sphereRadius) {
        return manifold;
    }

    manifold.intersects = true;

    if (distanceSquared > 0.00001f) {
        const float distance = std::sqrt(distanceSquared);
        manifold.normal = Scale(delta, 1.0f / distance);
        manifold.penetration = sphere.sphereRadius - distance;
        return manifold;
    }

    const XMFLOAT3 distanceToFace = {
        box.boxExtents.x - std::abs(sphereLocal.x),
        box.boxExtents.y - std::abs(sphereLocal.y),
        box.boxExtents.z - std::abs(sphereLocal.z)
    };

    manifold.penetration = sphere.sphereRadius + distanceToFace.x;
    manifold.normal = {sphereLocal.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f};

    if (distanceToFace.y < manifold.penetration) {
        manifold.penetration = sphere.sphereRadius + distanceToFace.y;
        manifold.normal = {0.0f, sphereLocal.y >= 0.0f ? 1.0f : -1.0f, 0.0f};
    }

    if (distanceToFace.z < manifold.penetration) {
        manifold.penetration = sphere.sphereRadius + distanceToFace.z;
        manifold.normal = {0.0f, 0.0f, sphereLocal.z >= 0.0f ? 1.0f : -1.0f};
    }

    return manifold;
}

CollisionManifold BuildCollisionManifold(const GameObject& a, const GameObject& b) {
    const ColliderWorldState worldA = BuildColliderWorldState(a);
    const ColliderWorldState worldB = BuildColliderWorldState(b);

    if (a.physics.collider.shape == PhysicsColliderShape::Sphere &&
        b.physics.collider.shape == PhysicsColliderShape::Sphere) {
        return BuildSphereSphereManifold(worldA, worldB);
    }

    if (a.physics.collider.shape == PhysicsColliderShape::Box &&
        b.physics.collider.shape == PhysicsColliderShape::Box) {
        return BuildBoxBoxManifold(worldA, worldB);
    }

    if (a.physics.collider.shape == PhysicsColliderShape::Sphere) {
        return BuildSphereBoxManifold(worldA, worldB);
    }

    CollisionManifold manifold = BuildSphereBoxManifold(worldB, worldA);
    if (manifold.intersects) {
        manifold.normal = Scale(manifold.normal, -1.0f);
    }
    return manifold;
}

void ResolveCollision(GameObject& a, GameObject& b) {
    if (!HasSolidCollider(a) || !HasSolidCollider(b)) {
        return;
    }

    const float inverseMassA = GetInverseMass(a);
    const float inverseMassB = GetInverseMass(b);
    if (inverseMassA + inverseMassB <= 0.0f) {
        return;
    }

    const CollisionManifold manifold = BuildCollisionManifold(a, b);
    if (!manifold.intersects || manifold.penetration <= 0.0f) {
        return;
    }

    const float correctionMagnitude =
        ((std::max)(manifold.penetration - kCorrectionSlop, 0.0f) / (inverseMassA + inverseMassB)) * kCorrectionPercent;
    const XMFLOAT3 correction = Scale(manifold.normal, correctionMagnitude);
    if (inverseMassA > 0.0f) {
        a.position = Subtract(a.position, Scale(correction, inverseMassA));
    }
    if (inverseMassB > 0.0f) {
        b.position = Add(b.position, Scale(correction, inverseMassB));
    }

    XMVECTOR normal = XMLoadFloat3(&manifold.normal);
    XMVECTOR velocityA = XMLoadFloat3(&a.physics.rigidBody.velocity);
    XMVECTOR velocityB = XMLoadFloat3(&b.physics.rigidBody.velocity);
    const XMVECTOR relativeVelocity = XMVectorSubtract(velocityB, velocityA);
    const float velocityAlongNormal = XMVectorGetX(XMVector3Dot(relativeVelocity, normal));
    if (velocityAlongNormal > 0.0f) {
        return;
    }

    const float restitution = (std::max)(a.physics.rigidBody.restitution, b.physics.rigidBody.restitution);
    const float impulseMagnitude = -(1.0f + restitution) * velocityAlongNormal / (inverseMassA + inverseMassB);
    const XMVECTOR impulse = XMVectorScale(normal, impulseMagnitude);

    if (inverseMassA > 0.0f) {
        velocityA = XMVectorSubtract(velocityA, XMVectorScale(impulse, inverseMassA));
        XMStoreFloat3(&a.physics.rigidBody.velocity, velocityA);
    }

    if (inverseMassB > 0.0f) {
        velocityB = XMVectorAdd(velocityB, XMVectorScale(impulse, inverseMassB));
        XMStoreFloat3(&b.physics.rigidBody.velocity, velocityB);
    }
}
}

void PhysicsSystem::Reset() {
    m_accumulator = 0.0f;
}

void PhysicsSystem::Step(std::vector<GameObject>& gameObjects, float deltaTime) {
    if (deltaTime <= 0.0f || gameObjects.empty()) {
        return;
    }

    m_accumulator = (std::min)(m_accumulator + deltaTime, kMaximumAccumulatedTime);
    const float fixedStep = m_fixedTimeStep > 0.0f ? m_fixedTimeStep : kFixedTimeStep;
    while (m_accumulator >= fixedStep) {
        SimulateFixedStep(gameObjects, fixedStep);
        m_accumulator -= fixedStep;
    }
}

void PhysicsSystem::InitializeDefaultCollider(GameObject& object, bool enableRigidBody, bool enableCollider) {
    object.physics.rigidBody.enabled = enableRigidBody;
    object.physics.rigidBody.bodyType = enableRigidBody ? PhysicsBodyType::Dynamic : PhysicsBodyType::Static;
    object.physics.rigidBody.useGravity = true;
    object.physics.rigidBody.mass = 1.0f;
    object.physics.rigidBody.linearDamping = 0.12f;
    object.physics.rigidBody.restitution = 0.2f;
    object.physics.rigidBody.velocity = {0.0f, 0.0f, 0.0f};

    object.physics.collider.enabled = enableCollider;
    object.physics.collider.shape = PhysicsColliderShape::Box;
    object.physics.collider.isTrigger = false;
    object.physics.collider.centerOffset = {0.0f, 0.0f, 0.0f};
    object.physics.collider.boxExtents = {0.5f, 0.5f, 0.5f};
    object.physics.collider.sphereRadius = 0.5f;

    if (object.asset && object.asset->mesh) {
        const BoundingBox& bounds = object.asset->mesh->GetBounds();
        object.physics.collider.centerOffset = bounds.Center;
        object.physics.collider.boxExtents = {
            (std::max)(bounds.Extents.x, kMinimumColliderSize),
            (std::max)(bounds.Extents.y, kMinimumColliderSize),
            (std::max)(bounds.Extents.z, kMinimumColliderSize)
        };
        object.physics.collider.sphereRadius = (std::max)(
            std::sqrt(bounds.Extents.x * bounds.Extents.x +
                      bounds.Extents.y * bounds.Extents.y +
                      bounds.Extents.z * bounds.Extents.z),
            0.25f);
    }
}

void PhysicsSystem::SimulateFixedStep(std::vector<GameObject>& gameObjects, float timeStep) {
    for (GameObject& object : gameObjects) {
        if (!object.enabled) {
            continue;
        }

        ClampPhysicsSettings(object);

        if (!object.physics.rigidBody.enabled) {
            continue;
        }

        if (IsDynamicBody(object)) {
            if (object.physics.rigidBody.useGravity) {
                object.physics.rigidBody.velocity = Add(object.physics.rigidBody.velocity, Scale(m_gravity, timeStep));
            }

            const float dampingFactor = 1.0f / (1.0f + object.physics.rigidBody.linearDamping * timeStep);
            object.physics.rigidBody.velocity = Scale(object.physics.rigidBody.velocity, dampingFactor);
            object.position = Add(object.position, Scale(object.physics.rigidBody.velocity, timeStep));
            continue;
        }

        if (IsKinematicBody(object)) {
            object.position = Add(object.position, Scale(object.physics.rigidBody.velocity, timeStep));
        }
    }

        for (int iteration = 0; iteration < 3; ++iteration) {
        for (size_t i = 0; i < gameObjects.size(); ++i) {
            for (size_t j = i + 1; j < gameObjects.size(); ++j) {
                if (!gameObjects[i].enabled || !gameObjects[j].enabled) {
                    continue;
                }
                ResolveCollision(gameObjects[i], gameObjects[j]);
            }
        }
    }
}
