#pragma once
#include <directxmath.h>

class Camera {
public:
    Camera();

    // Setup projection (Perspective view)
    void SetProjection(float fovDegrees, float aspectRatio, float nearZ, float farZ);

    // Update logic (Process Input & Math)
    void Update();

    // Getters for the Renderer
    DirectX::XMMATRIX GetViewMatrix() const;
    DirectX::XMMATRIX GetProjectionMatrix() const;
    DirectX::XMFLOAT3 GetPosition() const { return m_pos; }

private:
    // Transform
    DirectX::XMFLOAT3 m_pos;
    float m_pitch;
    float m_yaw;

    // Projection settings
    float m_fovRadians;
    float m_aspectRatio;
    float m_nearZ;
    float m_farZ;

    // Settings
    float m_moveSpeed;
    float m_lookSensitivity;
};