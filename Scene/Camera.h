#pragma once
#include <directxmath.h>
#include <windows.h> 

class Camera {
public:
    Camera();

    void SetProjection(float fovDegrees, float aspectRatio, float nearZ, float farZ);
    void Update(float deltaTime = 0.016f); // Added deltaTime for smooth movement

    DirectX::XMMATRIX GetViewMatrix() const;
    DirectX::XMMATRIX GetProjectionMatrix() const;
    DirectX::XMFLOAT3 GetPosition() const { return m_position; }

private:
    DirectX::XMFLOAT3 m_position;
    DirectX::XMFLOAT3 m_rotation; // Pitch, Yaw, Roll

    DirectX::XMMATRIX m_viewMatrix;
    DirectX::XMMATRIX m_projectionMatrix;

    // --- Input State ---
    POINT m_lastMousePos;
    bool m_isFlying = false; // Are we currently moving the camera?
};