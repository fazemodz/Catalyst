#define NOMINMAX
#include "Camera.h"
#include "imgui.h" // We need to check if UI is blocking us
#include <algorithm>

using namespace DirectX;

Camera::Camera() : m_position(0, 0, -5), m_rotation(0, 0, 0), m_isFlying(false) {
    m_lastMousePos = { 0, 0 };
}

void Camera::SetProjection(float fovDegrees, float aspectRatio, float nearZ, float farZ) {
    float fovRadians = fovDegrees * (3.14159f / 180.0f);
    m_projectionMatrix = XMMatrixPerspectiveFovLH(fovRadians, aspectRatio, nearZ, farZ);
}

void Camera::Update(float deltaTime) {
    // 1. Check if we should enter "Fly Mode"
    // We only fly if Right Click is held AND we aren't hovering a UI window (unless we are already flying)
    bool rightClickDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    bool uiBlocking = ImGui::GetIO().WantCaptureMouse;

    if (rightClickDown && (!uiBlocking || m_isFlying)) {
        if (!m_isFlying) {
            // Just started flying: Capture initial mouse position
            GetCursorPos(&m_lastMousePos);
            m_isFlying = true;
            ShowCursor(FALSE); // Hide cursor
        }

        // 2. Handle Rotation (Mouse Look)
        POINT currentMousePos;
        GetCursorPos(&currentMousePos);

        float dx = static_cast<float>(currentMousePos.x - m_lastMousePos.x);
        float dy = static_cast<float>(currentMousePos.y - m_lastMousePos.y);

        // Sensitivity
        float sensitivity = 0.002f;
        m_rotation.y += dx * sensitivity; // Yaw
        m_rotation.x += dy * sensitivity; // Pitch

        // Clamp Pitch (Prevent backflip)
        m_rotation.x = std::max(-1.5f, std::min(1.5f, m_rotation.x));

        // Reset Cursor to center of "last pos" to prevent hitting screen edge (Infinite Scroll)
        SetCursorPos(m_lastMousePos.x, m_lastMousePos.y);

        // 3. Handle Movement (WASD)
        float speed = 5.0f * deltaTime;
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) speed *= 2.0f; // Sprint

        XMVECTOR pos = XMLoadFloat3(&m_position);
        XMVECTOR forward = XMVectorSet(0, 0, 1, 0);
        XMVECTOR right = XMVectorSet(1, 0, 0, 0);
        XMVECTOR up = XMVectorSet(0, 1, 0, 0);

        // Rotate movement vectors by Camera Yaw
        XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw(m_rotation.x, m_rotation.y, 0);
        forward = XMVector3TransformCoord(forward, rotationMatrix);
        right = XMVector3TransformCoord(right, rotationMatrix);

        if (GetAsyncKeyState('W') & 0x8000) pos += forward * speed;
        if (GetAsyncKeyState('S') & 0x8000) pos -= forward * speed;
        if (GetAsyncKeyState('D') & 0x8000) pos += right * speed;
        if (GetAsyncKeyState('A') & 0x8000) pos -= right * speed;
        if (GetAsyncKeyState('E') & 0x8000) pos += up * speed;
        if (GetAsyncKeyState('Q') & 0x8000) pos -= up * speed;

        XMStoreFloat3(&m_position, pos);
    } 
    else {
        // Stop flying
        if (m_isFlying) {
            m_isFlying = false;
            ShowCursor(TRUE); // Show cursor
        }
    }

    // 4. Update View Matrix
    XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw(m_rotation.x, m_rotation.y, m_rotation.z);
    XMVECTOR pos = XMLoadFloat3(&m_position);
    XMVECTOR target = XMVector3TransformCoord(XMVectorSet(0, 0, 1, 0), rotationMatrix);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);

    // LookAt(Position, Position + Forward, Up)
    m_viewMatrix = XMMatrixLookAtLH(pos, pos + target, up);
}

DirectX::XMMATRIX Camera::GetViewMatrix() const {
    return m_viewMatrix;
}

DirectX::XMMATRIX Camera::GetProjectionMatrix() const {
    return m_projectionMatrix;
}