#pragma once
#include "UIDrawList.h"
#include <algorithm>

inline bool UICtx_PointInRect(float px, float py, float x, float y, float width, float height) {
    return px >= x && px <= (x + width) && py >= y && py <= (y + height);
}

inline bool UICtx_PointInWidgetClip(float px, float py, float x, float y, float width, float height, const UIClipRect& clipRect) {
    const float clipX     = clipRect.Enabled ? (std::max)(x, clipRect.X) : x;
    const float clipY     = clipRect.Enabled ? (std::max)(y, clipRect.Y) : y;
    const float clipRight  = clipRect.Enabled ? (std::min)(x + width,  clipRect.X + clipRect.Width)  : (x + width);
    const float clipBottom = clipRect.Enabled ? (std::min)(y + height, clipRect.Y + clipRect.Height) : (y + height);
    return clipRight > clipX && clipBottom > clipY &&
           UICtx_PointInRect(px, py, clipX, clipY, clipRight - clipX, clipBottom - clipY);
}
