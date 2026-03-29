#pragma once

#include <vector>
#include <directxmath.h>

struct GameObject;

class PhysicsSystem {
public:
    void Reset();
    void Step(std::vector<GameObject>& gameObjects, float deltaTime);

    static void InitializeDefaultCollider(GameObject& object, bool enableRigidBody, bool enableCollider = true);

private:
    void SimulateFixedStep(std::vector<GameObject>& gameObjects, float timeStep);

    float m_accumulator = 0.0f;
    DirectX::XMFLOAT3 m_gravity = {0.0f, -9.81f, 0.0f};
};
