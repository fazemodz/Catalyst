#include "UIContext.h"
#include "Camera.h"
#include <algorithm>
#include <cmath>

bool UIContext::TransformGizmo(DirectX::XMFLOAT3& position, Camera& camera,
                               float viewX, float viewY, float viewW, float viewH,
                               bool& isHovered) {
    isHovered = false;
    if (!m_drawList || !m_inputManager) return false;

    DirectX::XMMATRIX viewProj = camera.GetViewMatrix() * camera.GetProjectionMatrix();

    // Transform without the built-in perspective divide so w can be inspected:
    // a point at or behind the eye has w <= 0 and dividing by it mirrors the
    // point to a bogus on-screen position (or produces NaN at w == 0).
    auto Project = [&](DirectX::XMVECTOR pos, DirectX::XMFLOAT2& outScreen) -> bool {
        DirectX::XMFLOAT4 clip;
        DirectX::XMStoreFloat4(&clip, DirectX::XMVector3Transform(pos, viewProj));
        if (!(clip.w > 0.0001f)) {
            return false;
        }

        const float invW = 1.0f / clip.w;
        const float ndcX = clip.x * invW;
        const float ndcY = clip.y * invW;
        const float ndcZ = clip.z * invW;
        outScreen.x = viewX + (ndcX + 1.0f) * 0.5f * viewW;
        outScreen.y = viewY + (1.0f - ndcY) * 0.5f * viewH;
        return ndcZ >= 0.0f && ndcZ <= 1.0f;
    };

    DirectX::XMVECTOR wPos = DirectX::XMLoadFloat3(&position);
    DirectX::XMFLOAT2 cScr;
    if (!Project(wPos, cScr)) return false;

    float dist      = DirectX::XMVectorGetZ(DirectX::XMVector3Transform(wPos, camera.GetViewMatrix()));
    float gizmoScale = (std::max)(0.01f, dist * 0.15f);

    // An axis tip can cross behind the near plane while the origin is still in
    // view; each handle is only usable when its own endpoint projected cleanly.
    DirectX::XMFLOAT2 xScr = cScr, yScr = cScr, zScr = cScr;
    const bool axisValid[3] = {
        Project(DirectX::XMVectorAdd(wPos, DirectX::XMVectorSet(gizmoScale, 0, 0, 0)), xScr),
        Project(DirectX::XMVectorAdd(wPos, DirectX::XMVectorSet(0, gizmoScale, 0, 0)), yScr),
        Project(DirectX::XMVectorAdd(wPos, DirectX::XMVectorSet(0, 0, gizmoScale, 0)), zScr)
    };

    int mx = m_inputManager->GetMouseX();
    int my = m_inputManager->GetMouseY();
    DirectX::XMFLOAT2 mPos = { (float)mx, (float)my };
    bool inViewport = (mx >= viewX && mx <= viewX + viewW && my >= viewY && my <= viewY + viewH);

    auto DistLine = [](DirectX::XMFLOAT2 p, DirectX::XMFLOAT2 a, DirectX::XMFLOAT2 b) {
        float l2 = (b.x - a.x)*(b.x - a.x) + (b.y - a.y)*(b.y - a.y);
        if (l2 == 0.0f) return sqrtf((p.x - a.x)*(p.x - a.x) + (p.y - a.y)*(p.y - a.y));
        float t = (std::max)(0.0f, (std::min)(1.0f,
            ((p.x - a.x)*(b.x - a.x) + (p.y - a.y)*(b.y - a.y)) / l2));
        DirectX::XMFLOAT2 proj = { a.x + t*(b.x - a.x), a.y + t*(b.y - a.y) };
        return sqrtf((p.x - proj.x)*(p.x - proj.x) + (p.y - proj.y)*(p.y - proj.y));
    };

    int hoveredAxis = -1;
    if (m_activeGizmoAxis == -1 && inViewport) {
        if      (axisValid[0] && DistLine(mPos, cScr, xScr) < 12.0f) hoveredAxis = 0;
        else if (axisValid[1] && DistLine(mPos, cScr, yScr) < 12.0f) hoveredAxis = 1;
        else if (axisValid[2] && DistLine(mPos, cScr, zScr) < 12.0f) hoveredAxis = 2;

        if (hoveredAxis != -1 && m_inputManager->IsMouseButtonPressed(0)) {
            m_activeGizmoAxis = hoveredAxis;
            m_lastGizmoMouse  = mPos;
        }
    }

    bool changed = false;
    if (m_activeGizmoAxis != -1 && !axisValid[m_activeGizmoAxis]) {
        // The handle being dragged swung behind the camera — drop the drag
        // rather than steer by a garbage axis direction.
        m_activeGizmoAxis = -1;
    }
    if (m_activeGizmoAxis != -1) {
        if (!m_inputManager->IsMouseButtonDown(0)) {
            m_activeGizmoAxis = -1;
        } else {
            float dx = mPos.x - m_lastGizmoMouse.x;
            float dy = mPos.y - m_lastGizmoMouse.y;

            DirectX::XMFLOAT2 axisVec = {0, 0};
            if (m_activeGizmoAxis == 0) axisVec = { xScr.x - cScr.x, xScr.y - cScr.y };
            if (m_activeGizmoAxis == 1) axisVec = { yScr.x - cScr.x, yScr.y - cScr.y };
            if (m_activeGizmoAxis == 2) axisVec = { zScr.x - cScr.x, zScr.y - cScr.y };

            float len = sqrtf(axisVec.x*axisVec.x + axisVec.y*axisVec.y);
            if (len > 0.001f) {
                axisVec.x /= len; axisVec.y /= len;
                float dragAmt = dx * axisVec.x + dy * axisVec.y;
                float move3D  = dragAmt * dist * 0.0015f;

                if (m_activeGizmoAxis == 0) position.x += move3D;
                if (m_activeGizmoAxis == 1) position.y += move3D;
                if (m_activeGizmoAxis == 2) position.z += move3D;
                changed = true;
            }
            m_lastGizmoMouse = mPos;
        }
    }

    isHovered = (hoveredAxis != -1 || m_activeGizmoAxis != -1);

    uint32_t colX = (m_activeGizmoAxis == 0 || hoveredAxis == 0) ? 0xFF5555FF : 0xFF2222DD;
    uint32_t colY = (m_activeGizmoAxis == 1 || hoveredAxis == 1) ? 0xFF55FF55 : 0xFF22DD22;
    uint32_t colZ = (m_activeGizmoAxis == 2 || hoveredAxis == 2) ? 0xFFFF5555 : 0xFFDD2222;

    if (axisValid[0]) {
        m_drawList->AddLine(cScr.x, cScr.y, xScr.x, xScr.y, 4.0f, colX);
        m_drawList->AddRectFilled(xScr.x - 5, xScr.y - 5, 10, 10, colX);
    }
    if (axisValid[1]) {
        m_drawList->AddLine(cScr.x, cScr.y, yScr.x, yScr.y, 4.0f, colY);
        m_drawList->AddRectFilled(yScr.x - 5, yScr.y - 5, 10, 10, colY);
    }
    if (axisValid[2]) {
        m_drawList->AddLine(cScr.x, cScr.y, zScr.x, zScr.y, 4.0f, colZ);
        m_drawList->AddRectFilled(zScr.x - 5, zScr.y - 5, 10, 10, colZ);
    }

    return changed;
}
