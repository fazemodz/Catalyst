#pragma once
#include <DirectXMath.h>
#include "../UI/Input/InputManager.h" 

class Camera {
public:
    Camera();
    void SetProjection(float fovDegrees, float aspectRatio, float nearZ, float farZ);
    void Update(float deltaTime);
    
    DirectX::XMMATRIX GetViewMatrix() const;
    DirectX::XMMATRIX GetProjectionMatrix() const;
    DirectX::XMFLOAT3 GetPosition() const { return m_position; }

private:
    DirectX::XMFLOAT3 m_position;
    DirectX::XMFLOAT3 m_forward;
    DirectX::XMFLOAT3 m_up;
    DirectX::XMFLOAT3 m_right;

    DirectX::XMMATRIX m_viewMatrix;
    DirectX::XMMATRIX m_projMatrix;

    float m_yaw;
    float m_pitch;
    float m_moveSpeed;
    float m_turnSpeed;

    int m_lastMouseX;
    int m_lastMouseY;
    bool m_isDragging;
};