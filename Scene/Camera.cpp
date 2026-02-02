#include "Camera.h"
#include <algorithm> // For min/max clamping
#include <windows.h> // For input keys

// Access Global Input from Window.cpp
extern bool g_Keys[256];
extern bool g_RightMouseDown;
extern int g_MouseDeltaX;
extern int g_MouseDeltaY;
using namespace std;
using namespace DirectX;

Camera::Camera() {
    m_pos = { 0.0f, 0.0f, -5.0f };
    m_pitch = 0.0f;
    m_yaw = 0.0f;
    m_moveSpeed = 0.1f;         // Adjust speed here
    m_lookSensitivity = 0.002f; // Adjust sensitivity here
}

void Camera::SetProjection(float fovDegrees, float aspectRatio, float nearZ, float farZ) {
    m_fovRadians = XMConvertToRadians(fovDegrees);
    m_aspectRatio = aspectRatio;
    m_nearZ = nearZ;
    m_farZ = farZ;
}

void Camera::Update() {
    // 1. Handle Rotation (Mouse)
    if (g_RightMouseDown) {
        m_yaw += g_MouseDeltaX * m_lookSensitivity;
        m_pitch += g_MouseDeltaY * m_lookSensitivity;
        
        // Reset Delta so it doesn't keep spinning if we stop moving
        g_MouseDeltaX = 0;
        g_MouseDeltaY = 0;

        // Clamp Pitch so we can't look upside down (like in FPS games)
        // 1.55 radians is roughly 89 degrees
        m_pitch = max(-1.55f, min(1.55f, m_pitch));
    }

    // 2. Handle Movement (WASD)
    if (g_RightMouseDown) { // Only move when right click is held (Editor Style)
        XMMATRIX rotMat = XMMatrixRotationRollPitchYaw(m_pitch, m_yaw, 0.0f);
        XMVECTOR forward = XMVector3TransformNormal(XMVectorSet(0, 0, 1, 0), rotMat);
        XMVECTOR right = XMVector3TransformNormal(XMVectorSet(1, 0, 0, 0), rotMat);
        XMVECTOR up = XMVectorSet(0, 1, 0, 0); // World Up

        XMVECTOR currentPos = XMLoadFloat3(&m_pos);

        if (g_Keys['W']) currentPos += forward * m_moveSpeed;
        if (g_Keys['S']) currentPos -= forward * m_moveSpeed;
        if (g_Keys['D']) currentPos += right * m_moveSpeed;
        if (g_Keys['A']) currentPos -= right * m_moveSpeed;
        if (g_Keys['E']) currentPos += up * m_moveSpeed;   // Fly Up
        if (g_Keys['Q']) currentPos -= up * m_moveSpeed;   // Fly Down

        XMStoreFloat3(&m_pos, currentPos);
    }
}

DirectX::XMMATRIX Camera::GetViewMatrix() const {
    XMMATRIX rotMat = XMMatrixRotationRollPitchYaw(m_pitch, m_yaw, 0.0f);
    XMVECTOR forward = XMVector3TransformNormal(XMVectorSet(0, 0, 1, 0), rotMat);
    XMVECTOR pos = XMLoadFloat3(&m_pos);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);
    return XMMatrixLookToLH(pos, forward, up);
}

DirectX::XMMATRIX Camera::GetProjectionMatrix() const {
    return XMMatrixPerspectiveFovLH(m_fovRadians, m_aspectRatio, m_nearZ, m_farZ);
}