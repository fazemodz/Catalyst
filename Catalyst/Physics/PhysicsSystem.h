#pragma once

#include <vector>
#include <directxmath.h>

struct GameObject;

class PhysicsSystem {
public:
    void Reset();
    void Step(std::vector<GameObject>& gameObjects, float deltaTime);

    static void InitializeDefaultCollider(GameObject& object, bool enableRigidBody, bool enableCollider = true);

    void SetGravity(const DirectX::XMFLOAT3& gravity) { m_gravity = gravity; }
    DirectX::XMFLOAT3 GetGravity() const { return m_gravity; }

    // Guarded by the caller's ClampToValidRanges(); a non-positive step would
    // make the fixed-step loop never terminate.
    void SetFixedTimeStep(float step) { if (step > 0.0f) m_fixedTimeStep = step; }
    float GetFixedTimeStep() const { return m_fixedTimeStep; }

private:
    void SimulateFixedStep(std::vector<GameObject>& gameObjects, float timeStep);

    float m_accumulator = 0.0f;
    float m_fixedTimeStep = 1.0f / 60.0f;
    DirectX::XMFLOAT3 m_gravity = {0.0f, -9.81f, 0.0f};
};
