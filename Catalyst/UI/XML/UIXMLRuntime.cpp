#include "UIXMLRuntime.h"
#include "UIXMLParser.h"
#include "UIXMLStyleSheet.h"
#include "UIDrawList.h"
#include "UIContext.h"
#include "FontManager.h"
#include <fstream>
#include <sstream>

namespace UIXML {

// ── Document loading ──────────────────────────────────────────────────────────
void UIXMLRuntime::SetDocument(const Document& doc, const std::wstring& sourcePath) {
    m_doc        = doc;
    m_sourcePath = sourcePath;
    m_textureCache.clear();
    if (!sourcePath.empty() && std::filesystem::exists(sourcePath))
        m_lastWriteTime = std::filesystem::last_write_time(sourcePath);
}

void UIXMLRuntime::SetDocument(Document&& doc, const std::wstring& sourcePath) {
    m_doc        = std::move(doc);
    m_sourcePath = sourcePath;
    m_textureCache.clear();
    if (!sourcePath.empty() && std::filesystem::exists(sourcePath))
        m_lastWriteTime = std::filesystem::last_write_time(sourcePath);
}

// ── Overrides ─────────────────────────────────────────────────────────────────
void UIXMLRuntime::SetNodeOverride(const std::string& nodeId, const NodeOverride& override) {
    m_overrides[nodeId] = override;
}

void UIXMLRuntime::ClearNodeOverrides() {
    m_overrides.clear();
}

// ── Texture resolver ──────────────────────────────────────────────────────────
uint32_t UIXMLRuntime::ResolveTexture(const std::string& src) const {
    if (src.empty()) return 0;
    auto it = m_textureCache.find(src);
    if (it != m_textureCache.end()) return it->second;
    uint32_t id = 0;
    if (m_resolver) {
        std::wstring wide(src.begin(), src.end());
        id = m_resolver(wide);
    }
    m_textureCache[src] = id;
    return id;
}

// ── Color helpers ─────────────────────────────────────────────────────────────
uint32_t UIXMLRuntime::AdjustBrightness(uint32_t argb, float factor) {
    uint8_t a = (argb >> 24) & 0xFF;
    uint8_t r = (argb >> 16) & 0xFF;
    uint8_t g = (argb >>  8) & 0xFF;
    uint8_t b = (argb      ) & 0xFF;
    auto clampByte = [](float v) -> uint8_t {
        if (v < 0.0f)   return 0;
        if (v > 255.0f) return 255;
        return static_cast<uint8_t>(v);
    };
    return (static_cast<uint32_t>(a) << 24)
         | (static_cast<uint32_t>(clampByte(r * factor)) << 16)
         | (static_cast<uint32_t>(clampByte(g * factor)) <<  8)
         |  static_cast<uint32_t>(clampByte(b * factor));
}

// ── DrawNode ──────────────────────────────────────────────────────────────────
void UIXMLRuntime::DrawNode(const Node& node,
                             UIDrawList& dl, UIContext& uiCtx, FontManager& fm,
                             float parentX, float parentY) {
    // Apply overrides
    bool     visible    = node.visible;
    uint32_t background = node.background;
    uint32_t textColor  = node.textColor;
    uint32_t tint       = node.tint;
    std::string text    = node.text;

    if (!node.id.empty()) {
        auto it = m_overrides.find(node.id);
        if (it != m_overrides.end()) {
            const NodeOverride& ov = it->second;
            if (ov.hasVisible)    visible    = ov.visible;
            if (ov.hasBackground) background = ov.background;
            if (ov.hasTextColor)  textColor  = ov.textColor;
            if (ov.hasText)       text       = ov.text;
        }
    }
    if (!visible) return;

    float ex = parentX + node.x;
    float ey = parentY + node.y;

    switch (node.type) {
    case NodeType::Canvas:
    case NodeType::Panel:
        if ((background >> 24) > 0)  // only draw if not fully transparent
            dl.AddRectFilled(ex, ey, node.w, node.h, background);
        for (const auto& child : node.children)
            DrawNode(child, dl, uiCtx, fm, ex, ey);
        break;

    case NodeType::Button: {
        // Derive hover/click colors from background unless explicitly set
        uint32_t hoverColor = AdjustBrightness(background, 1.35f);
        uint32_t clickColor = AdjustBrightness(background, 0.70f);

        // Allow explicit override via hoverBackground / clickBackground attrs
        auto hbIt = node.attrs.find("hoverBackground");
        if (hbIt != node.attrs.end())
            hoverColor = ParseHexColor(hbIt->second.c_str(), hoverColor);
        auto cbIt = node.attrs.find("clickBackground");
        if (cbIt != node.attrs.end())
            clickColor = ParseHexColor(cbIt->second.c_str(), clickColor);

        bool clicked = uiCtx.Button(text, ex, ey, node.w, node.h,
                                     background, hoverColor, clickColor);
        if (clicked && m_buttonCallback)
            m_buttonCallback(node.id);

        // Draw label text on top if non-empty
        if (!text.empty())
            uiCtx.AddText(text, ex + 4.0f, ey + (node.h - 14.0f) * 0.5f, textColor);

        for (const auto& child : node.children)
            DrawNode(child, dl, uiCtx, fm, ex, ey);
        break;
    }

    case NodeType::TextBlock:
        uiCtx.AddText(text, ex, ey, textColor, node.w > 0 ? node.w : 0.0f);
        break;

    case NodeType::Image: {
        uint32_t texId = ResolveTexture(node.src);
        uiCtx.Image(texId, ex, ey, node.w, node.h, tint);
        break;
    }

    case NodeType::Unknown:
    default:
        // Still recurse children of unknown containers
        for (const auto& child : node.children)
            DrawNode(child, dl, uiCtx, fm, ex, ey);
        break;
    }
}

// ── Draw ──────────────────────────────────────────────────────────────────────
void UIXMLRuntime::Draw(UIDrawList& drawList, UIContext& uiCtx, FontManager& fontManager,
                         float offsetX, float offsetY, float /*availW*/, float /*availH*/) {
    if (!m_doc.valid) return;
    DrawNode(m_doc.root, drawList, uiCtx, fontManager, offsetX, offsetY);
    m_overrides.clear();
}

// ── Hot-reload ────────────────────────────────────────────────────────────────
bool UIXMLRuntime::CheckAndReload() {
    if (m_sourcePath.empty()) return false;
    std::error_code ec;
    auto current = std::filesystem::last_write_time(m_sourcePath, ec);
    if (ec || current == m_lastWriteTime) return false;

    // Load style sheet if companion .xmlstyle exists
    std::filesystem::path stylePath = m_sourcePath;
    stylePath.replace_extension(L".xmlstyle");
    StyleSheet sheet;
    if (std::filesystem::exists(stylePath))
        sheet = StyleSheet::ParseFromFile(stylePath.wstring());

    Document doc = ParseFromFile(m_sourcePath, sheet.Empty() ? nullptr : &sheet);
    if (!doc.valid) return false;

    m_doc           = std::move(doc);
    m_lastWriteTime = current;
    m_textureCache.clear();
    return true;
}

} // namespace UIXML
