#include "UIDrawList.h"
#include <algorithm>

namespace {
bool SameClipRect(const UIClipRect& a, const UIClipRect& b) {
    return a.Enabled == b.Enabled &&
           a.X == b.X && a.Y == b.Y &&
           a.Width == b.Width && a.Height == b.Height;
}
}

void UIDrawList::Clear() {
    Vertices.clear();
    Indices.clear();
    Commands.clear();
    m_clipRectStack.clear();
}

UIClipRect UIDrawList::GetActiveClipRect() const {
    return m_clipRectStack.empty() ? UIClipRect{} : m_clipRectStack.back();
}

void UIDrawList::PushClipRect(float x, float y, float width, float height) {
    UIClipRect clipRect;
    clipRect.X       = x;
    clipRect.Y       = y;
    clipRect.Width   = (std::max)(0.0f, width);
    clipRect.Height  = (std::max)(0.0f, height);
    clipRect.Enabled = true;

    if (!m_clipRectStack.empty() && m_clipRectStack.back().Enabled) {
        const UIClipRect& cur = m_clipRectStack.back();
        const float left   = (std::max)(cur.X, clipRect.X);
        const float top    = (std::max)(cur.Y, clipRect.Y);
        const float right  = (std::min)(cur.X + cur.Width,  clipRect.X + clipRect.Width);
        const float bottom = (std::min)(cur.Y + cur.Height, clipRect.Y + clipRect.Height);
        clipRect.X      = left;
        clipRect.Y      = top;
        clipRect.Width  = (std::max)(0.0f, right  - left);
        clipRect.Height = (std::max)(0.0f, bottom - top);
    }

    m_clipRectStack.push_back(clipRect);
}

void UIDrawList::PopClipRect() {
    if (!m_clipRectStack.empty())
        m_clipRectStack.pop_back();
}

void UIDrawList::PushTextureBatch(uint32_t textureID) {
    const UIClipRect activeClipRect = GetActiveClipRect();
    if (Commands.empty() ||
        Commands.back().TextureID != textureID ||
        !SameClipRect(Commands.back().ClipRect, activeClipRect)) {
        UIDrawCommand cmd;
        cmd.ElementCount = 0;
        cmd.IndexOffset  = (uint32_t)Indices.size();
        cmd.TextureID    = textureID;
        cmd.ClipRect     = activeClipRect;
        Commands.push_back(cmd);
    }
}
