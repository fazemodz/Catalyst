#include "Camera.h"
#include <windows.h>
#include <algorithm>

// Hook into our custom Input Manager!
extern InputManager* g_InputManager;

using namespace DirectX;

Camera::Camera() : m_position(0.0f, 2.0f, -10.0f), m_yaw(0.0f), m_pitch(0.0f), 
                   m_moveSpeed(15.0f), m_turnSpeed(0.005f), 
                   m_lastMouseX(0), m_lastMouseY(0), m_isDragging(false) {
    m_forward = {0.0f, 0.0f, 1.0f};
    m_up = {0.0f, 1.0f, 0.0f};
    m_right = {1.0f, 0.0f, 0.0f};
    m_viewMatrix = XMMatrixIdentity();
    m_projMatrix = XMMatrixIdentity();
}

void Camera::SetProjection(float fovDegrees, float aspectRatio, float nearZ, float farZ) {
    m_projMatrix = XMMatrixPerspectiveFovLH(XMConvertToRadians(fovDegrees), aspectRatio, nearZ, farZ);
}

void Camera::SetLookAt(const XMFLOAT3& position, const XMFLOAT3& target, const XMFLOAT3& upVector) {
    m_position = position;

    const XMVECTOR pos = XMLoadFloat3(&position);
    const XMVECTOR targetPos = XMLoadFloat3(&target);
    const XMVECTOR up = XMVector3Normalize(XMLoadFloat3(&upVector));
    const XMVECTOR forward = XMVector3Normalize(XMVectorSubtract(targetPos, pos));
    const XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, forward));
    const XMVECTOR correctedUp = XMVector3Normalize(XMVector3Cross(forward, right));

    XMStoreFloat3(&m_forward, forward);
    XMStoreFloat3(&m_right, right);
    XMStoreFloat3(&m_up, correctedUp);
    m_viewMatrix = XMMatrixLookAtLH(pos, targetPos, correctedUp);
}

void Camera::Update(float deltaTime, bool allowInteraction) {
    if (!g_InputManager) return;

    if (allowInteraction && g_InputManager->IsMouseButtonPressed(1)) {
        m_isDragging = true;
        m_lastMouseX = g_InputManager->GetMouseX();
        m_lastMouseY = g_InputManager->GetMouseY();
    } else if (!g_InputManager->IsMouseButtonDown(1)) {
        m_isDragging = false;
    }

    if (m_isDragging) {
        int mouseX = g_InputManager->GetMouseX();
        int mouseY = g_InputManager->GetMouseY();

        float deltaX = static_cast<float>(mouseX - m_lastMouseX);
        float deltaY = static_cast<float>(mouseY - m_lastMouseY);

        m_yaw += deltaX * m_turnSpeed;
        m_pitch += deltaY * m_turnSpeed;

        // Clamp pitch to prevent the camera from doing backward somersaults
        m_pitch = std::clamp(m_pitch, -XM_PIDIV2 + 0.01f, XM_PIDIV2 - 0.01f);

        m_lastMouseX = mouseX;
        m_lastMouseY = mouseY;
    }

    // Calculate new direction vectors
    XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw(m_pitch, m_yaw, 0.0f);
    XMVECTOR forward = XMVector3TransformCoord(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotationMatrix);
    XMVECTOR up = XMVector3TransformCoord(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), rotationMatrix);
    XMVECTOR right = XMVector3Cross(up, forward);

    XMStoreFloat3(&m_forward, forward);
    XMStoreFloat3(&m_up, up);
    XMStoreFloat3(&m_right, right);

    XMVECTOR pos = XMLoadFloat3(&m_position);
    
    // Only allow WASD flying if we are actively holding right-click
    if (m_isDragging) { 
        if (GetAsyncKeyState('W') & 0x8000) pos += forward * m_moveSpeed * deltaTime;
        if (GetAsyncKeyState('S') & 0x8000) pos -= forward * m_moveSpeed * deltaTime;
        if (GetAsyncKeyState('A') & 0x8000) pos -= right * m_moveSpeed * deltaTime;
        if (GetAsyncKeyState('D') & 0x8000) pos += right * m_moveSpeed * deltaTime;
    }
    
    XMStoreFloat3(&m_position, pos);
    m_viewMatrix = XMMatrixLookAtLH(pos, pos + forward, up);
}

XMMATRIX Camera::GetViewMatrix() const { return m_viewMatrix; }
XMMATRIX Camera::GetProjectionMatrix() const { return m_projMatrix; }
