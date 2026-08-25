#pragma once
#include "UIXMLParser.h"
#include <string>
#include <functional>
#include <map>
#include <cstdint>
#include <filesystem>

class UIDrawList;
class UIContext;
class FontManager;

namespace UIXML {

// Callback invoked when a Button node is clicked. Passes the node's id attribute.
using ButtonCallback = std::function<void(const std::string& buttonId)>;

// Resolver called to obtain a bindless texture ID for a given source path.
// Registered by DXRenderer; returns a white-texture fallback on miss.
using TextureResolver = std::function<uint32_t(const std::wstring& srcPath)>;

// Per-node dynamic overrides injected by C++ code each frame (e.g. EditorUIState).
// Only set fields for properties you actually want to override.
struct NodeOverride {
    bool     hasVisible    = false; bool        visible     = true;
    bool     hasText       = false; std::string text;
    bool     hasBackground = false; uint32_t    background  = 0;
    bool     hasTextColor  = false; uint32_t    textColor   = 0xFFFFFFFF;
};

class UIXMLRuntime {
public:
    // Load a document. Call once after ParseFromFile/ParseFromText.
    void SetDocument(const Document& doc, const std::wstring& sourcePath = L"");
    void SetDocument(Document&& doc,      const std::wstring& sourcePath = L"");

    // Optional style sheet applied on top of the parsed document.
    void SetStyleSheet(const StyleSheet* sheet) { m_styles = sheet; }

    // Register callbacks
    void SetButtonCallback(ButtonCallback cb)   { m_buttonCallback = std::move(cb); }
    void SetTextureResolver(TextureResolver res) { m_resolver       = std::move(res); }

    // Per-frame overrides (cleared automatically at end of Draw)
    void SetNodeOverride(const std::string& nodeId, const NodeOverride& override);
    void ClearNodeOverrides();

    // Called once per frame during the draw phase.
    // offsetX/offsetY: top-left screen position of the containing region.
    void Draw(UIDrawList& drawList, UIContext& uiCtx, FontManager& fontManager,
              float offsetX, float offsetY, float availW, float availH);

    // Hot-reload: re-parse source file if it changed on disk. Returns true if reloaded.
    bool CheckAndReload();

    bool IsValid() const { return m_doc.valid; }

private:
    void DrawNode(const Node& node,
                  UIDrawList& drawList, UIContext& uiCtx, FontManager& fontManager,
                  float parentX, float parentY);

    uint32_t ResolveTexture(const std::string& src) const;

    // Brighten / darken an ARGB color by a factor (>1 brightens, <1 darkens)
    static uint32_t AdjustBrightness(uint32_t argb, float factor);

    Document    m_doc;
    const StyleSheet* m_styles = nullptr;

    ButtonCallback  m_buttonCallback;
    TextureResolver m_resolver;

    std::map<std::string, NodeOverride> m_overrides;

    // Texture cache: src string → bindless index
    mutable std::map<std::string, uint32_t> m_textureCache;

    // Hot-reload
    std::wstring m_sourcePath;
    std::filesystem::file_time_type m_lastWriteTime = {};
};

} // namespace UIXML
