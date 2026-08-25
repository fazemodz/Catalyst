#pragma once

// Internal-only helpers shared across BlueprintEditor translation units.
// This header is not intended for inclusion outside BlueprintEditor implementation files.

#include "BlueprintEditor.h"

#include "Nodes/BlueprintNodeLibrary.h"
#include "../Launcher.h"
#include "../UI/Input/InputManager.h"
#include "../UI/Render/FontManager.h"
#include "../UI/Render/UIDrawList.h"
#include "../UI/UIContext.h"

#include "Json.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace {
std::wstring NormalizeAssetPath(const std::wstring& path) {
    if (path.empty()) {
        return L"";
    }

    std::error_code ec;
    fs::path absolutePath = fs::absolute(fs::path(path), ec);
    if (ec) {
        absolutePath = fs::path(path);
    }

    return absolutePath.lexically_normal().wstring();
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return "";
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return "";
    }

    std::string converted(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, converted.data(), size, nullptr, nullptr);
    converted.pop_back();
    return converted;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size <= 1) {
        return L"";
    }

    std::wstring converted(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, converted.data(), size);
    converted.pop_back();
    return converted;
}

std::wstring DecodeStoredPath(const std::string& value) {
    std::wstring converted = Utf8ToWide(value);
    if (converted.empty() && !value.empty()) {
        converted.assign(value.begin(), value.end());
    }
    return converted;
}

std::wstring ResolveBlueprintReferencePath(const std::wstring& blueprintAssetPath, const std::string& storedValue) {
    const std::wstring decoded = DecodeStoredPath(storedValue);
    if (decoded.empty()) {
        return L"";
    }

    fs::path decodedPath(decoded);
    if (decodedPath.is_relative() && !blueprintAssetPath.empty()) {
        decodedPath = fs::path(blueprintAssetPath).parent_path() / decodedPath;
    }

    return NormalizeAssetPath(decodedPath.wstring());
}

std::wstring MakeBlueprintReferencePath(const std::wstring& blueprintAssetPath, const std::wstring& targetPath) {
    const std::wstring normalizedTargetPath = NormalizeAssetPath(targetPath);
    if (normalizedTargetPath.empty()) {
        return L"";
    }

    if (blueprintAssetPath.empty()) {
        return normalizedTargetPath;
    }

    std::error_code ec;
    const fs::path blueprintFolder = fs::path(blueprintAssetPath).parent_path();
    const fs::path relativePath = fs::relative(fs::path(normalizedTargetPath), blueprintFolder, ec);
    if (!ec && !relativePath.empty()) {
        return relativePath.lexically_normal().wstring();
    }

    return normalizedTargetPath;
}

float MeasureTextWidth(FontManager& fontManager, const std::string& text) {
    float width = 0.0f;
    for (size_t i = 0; i < text.size();) {
        const uint32_t codepoint = TextUtf8::NextCodepoint(text, i);
        if (codepoint == '\n') {
            break;
        }
        width += fontManager.GetGlyph(codepoint).Advance;
    }
    return width;
}

std::string FitTextToWidth(FontManager& fontManager, const std::string& text, float maxWidth) {
    if (maxWidth <= 0.0f) {
        return "";
    }

    if (MeasureTextWidth(fontManager, text) <= maxWidth) {
        return text;
    }

    const std::string ellipsis = "...";
    const float ellipsisWidth = MeasureTextWidth(fontManager, ellipsis);
    if (ellipsisWidth >= maxWidth) {
        return "";
    }

    std::string fitted;
    fitted.reserve(text.size());
    float width = 0.0f;
    for (size_t i = 0; i < text.size();) {
        // Copy whole UTF-8 sequences so truncation never splits a character.
        const size_t sequenceStart = i;
        const uint32_t codepoint = TextUtf8::NextCodepoint(text, i);
        const float glyphWidth = fontManager.GetGlyph(codepoint).Advance;
        if ((width + glyphWidth + ellipsisWidth) > maxWidth) {
            break;
        }
        fitted.append(text, sequenceStart, i - sequenceStart);
        width += glyphWidth;
    }

    fitted += ellipsis;
    return fitted;
}

float MeasureWrappedTextHeight(FontManager& fontManager, const std::string& text, float wrapWidth) {
    if (text.empty()) {
        return 0.0f;
    }

    constexpr float kLineHeight = 28.0f;
    if (wrapWidth <= 0.0f) {
        return kLineHeight;
    }

    float cursorX = 0.0f;
    int lineCount = 1;
    for (size_t i = 0; i < text.size();) {
        const uint32_t codepoint = TextUtf8::NextCodepoint(text, i);
        if (codepoint == '\n') {
            cursorX = 0.0f;
            ++lineCount;
            continue;
        }

        const float glyphWidth = fontManager.GetGlyph(codepoint).Advance;
        if ((cursorX + glyphWidth) > wrapWidth) {
            cursorX = 0.0f;
            ++lineCount;
            if (codepoint == ' ') {
                continue;
            }
        }

        cursorX += glyphWidth;
    }

    return static_cast<float>(lineCount) * kLineHeight;
}

bool IsPointInRect(float px, float py, float x, float y, float width, float height) {
    return px >= x && px <= (x + width) && py >= y && py <= (y + height);
}

const char* ShortCategoryName(const char* category) {
    if (category == nullptr) {
        return "Field";
    }
    if (std::strcmp(category, "Physics.RigidBody") == 0) {
        return "Rigid Body";
    }
    if (std::strcmp(category, "Physics.Collider") == 0) {
        return "Collider";
    }
    return category;
}

uint32_t NodeAccentColor(BlueprintEditor::NodeVisualKind visualKind) {
    switch (visualKind) {
    case BlueprintEditor::NodeVisualKind::Event:
        return 0xFF1F8E6E;
    case BlueprintEditor::NodeVisualKind::Field:
        return 0xFF3368A8;
    case BlueprintEditor::NodeVisualKind::Component:
        return 0xFF2E6F66;
    case BlueprintEditor::NodeVisualKind::UIElement:
        return 0xFF4F6375;
    case BlueprintEditor::NodeVisualKind::Comment:
        return 0xFF8D7A39;
    case BlueprintEditor::NodeVisualKind::Function:
    default:
        return 0xFFB36A0B;
    }
}

uint32_t FieldTypeColor(BlueprintFieldType type) {
    switch (type) {
    case BlueprintFieldType::Bool:
        return 0xFF5FB36C;
    case BlueprintFieldType::Float3:
        return 0xFF5A86D6;
    case BlueprintFieldType::Enum:
        return 0xFFD6983A;
    case BlueprintFieldType::Float:
    default:
        return 0xFF7B7B7B;
    }
}

uint32_t ComponentAccentColor(BlueprintEditor::ComponentKind kind) {
    switch (kind) {
    case BlueprintEditor::ComponentKind::Camera:
        return 0xFF5D8CD6;
    case BlueprintEditor::ComponentKind::Trigger:
        return 0xFFBF6A3A;
    case BlueprintEditor::ComponentKind::SkeletalMesh:
        return 0xFF5E9F7A;
    case BlueprintEditor::ComponentKind::StaticMesh:
    default:
        return 0xFF4D7BA8;
    }
}

uint32_t UIElementAccentColor(BlueprintEditor::UIElementKind kind) {
    switch (kind) {
    case BlueprintEditor::UIElementKind::Canvas:
        return 0xFF3A6C7A;
    case BlueprintEditor::UIElementKind::Button:
        return 0xFF5A7BD0;
    case BlueprintEditor::UIElementKind::Image:
        return 0xFF8B6DAE;
    case BlueprintEditor::UIElementKind::TextBlock:
        return 0xFF3A8E5B;
    case BlueprintEditor::UIElementKind::None:
    default:
        return 0xFF4F6375;
    }
}

bool IsUIBlueprintPath(const std::wstring& assetPath) {
    if (assetPath.empty()) {
        return false;
    }

    std::wstring extension = fs::path(assetPath).extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
    return extension == L".catalystuiblueprint";
}

bool IsCreateWidgetNodeType(const std::string& nodeTypeId) {
    return nodeTypeId == BlueprintNodes::kCreateWidgetNodeId;
}

bool IsAddToViewportNodeType(const std::string& nodeTypeId) {
    return nodeTypeId == BlueprintNodes::kAddToViewportNodeId;
}

bool IsSwapWidgetNodeType(const std::string& nodeTypeId) {
    return nodeTypeId == BlueprintNodes::kSwapWidgetNodeId;
}

bool IsOpenLevelNodeType(const std::string& nodeTypeId) {
    return nodeTypeId == BlueprintNodes::kOpenLevelNodeId;
}

bool IsWidgetConstructNodeType(const std::string& nodeTypeId) {
    return nodeTypeId == BlueprintNodes::kWidgetConstructNodeId;
}

bool IsSetTextColorNodeType(const std::string& nodeTypeId) {
    return nodeTypeId == BlueprintNodes::kSetTextColorNodeId;
}

bool IsCanvasElementNodeType(const std::string& nodeTypeId) {
    return nodeTypeId == BlueprintNodes::kCanvasElementNodeId;
}

bool IsButtonElementNodeType(const std::string& nodeTypeId) {
    return nodeTypeId == BlueprintNodes::kButtonElementNodeId;
}

bool IsImageElementNodeType(const std::string& nodeTypeId) {
    return nodeTypeId == BlueprintNodes::kImageElementNodeId;
}

bool IsTextBlockElementNodeType(const std::string& nodeTypeId) {
    return nodeTypeId == BlueprintNodes::kTextBlockElementNodeId;
}

bool IsUIElementNodeType(const std::string& nodeTypeId) {
    return IsCanvasElementNodeType(nodeTypeId) ||
           IsButtonElementNodeType(nodeTypeId) ||
           IsImageElementNodeType(nodeTypeId) ||
           IsTextBlockElementNodeType(nodeTypeId);
}

BlueprintEditor::UIElementKind UIElementKindFromNodeType(const std::string& nodeTypeId) {
    if (IsCanvasElementNodeType(nodeTypeId)) {
        return BlueprintEditor::UIElementKind::Canvas;
    }
    if (IsButtonElementNodeType(nodeTypeId)) {
        return BlueprintEditor::UIElementKind::Button;
    }
    if (IsImageElementNodeType(nodeTypeId)) {
        return BlueprintEditor::UIElementKind::Image;
    }
    if (IsTextBlockElementNodeType(nodeTypeId)) {
        return BlueprintEditor::UIElementKind::TextBlock;
    }
    return BlueprintEditor::UIElementKind::None;
}

const char* UIElementKindToString(BlueprintEditor::UIElementKind kind) {
    switch (kind) {
    case BlueprintEditor::UIElementKind::Canvas:
        return "Canvas";
    case BlueprintEditor::UIElementKind::Button:
        return "Button";
    case BlueprintEditor::UIElementKind::Image:
        return "Image";
    case BlueprintEditor::UIElementKind::TextBlock:
        return "TextBlock";
    case BlueprintEditor::UIElementKind::None:
    default:
        return "None";
    }
}

BlueprintEditor::UIElementKind UIElementKindFromString(const std::string& value) {
    if (value == "Canvas") {
        return BlueprintEditor::UIElementKind::Canvas;
    }
    if (value == "Button") {
        return BlueprintEditor::UIElementKind::Button;
    }
    if (value == "Image") {
        return BlueprintEditor::UIElementKind::Image;
    }
    if (value == "TextBlock") {
        return BlueprintEditor::UIElementKind::TextBlock;
    }
    return BlueprintEditor::UIElementKind::None;
}

DirectX::XMFLOAT4 UIntColorToFloat4(uint32_t color) {
    const float a = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    const float r = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    const float g = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
    const float b = static_cast<float>(color & 0xFF) / 255.0f;
    return {r, g, b, a};
}

uint32_t Float4ToUIntColor(const DirectX::XMFLOAT4& color) {
    const auto ToByte = [](float value) {
        const float clamped = (std::max)(0.0f, (std::min)(1.0f, value));
        return static_cast<uint32_t>(std::lround(clamped * 255.0f));
    };

    const uint32_t a = ToByte(color.w);
    const uint32_t r = ToByte(color.x);
    const uint32_t g = ToByte(color.y);
    const uint32_t b = ToByte(color.z);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

void DrawSectionHeader(UIDrawList& drawList, FontManager& fontManager, const std::string& title,
                       float x, float y, float width) {
    drawList.AddRectFilled(x, y, width, 26.0f, 0xFF31343A);
    drawList.AddText(fontManager, title, x + 10.0f, y + 16.0f, 0xFFFFFFFF);
}

void DrawFieldBadge(UIDrawList& drawList, FontManager& fontManager, const std::string& label,
                    float x, float y, uint32_t color) {
    const float width = MeasureTextWidth(fontManager, label) + 18.0f;
    drawList.AddRectFilled(x, y, width, 22.0f, color);
    drawList.AddText(fontManager, label, x + 9.0f, y + 15.0f, 0xFFFFFFFF);
}

void DrawPinRow(UIDrawList& drawList, FontManager& fontManager, const std::string& label,
                float x, float y, float width, bool isOutput, uint32_t pinColor, float scale = 1.0f) {
    const float pinSize = (std::max)(4.0f, 6.0f * scale);
    const float pinInset = 10.0f * scale;
    const float pinX = isOutput ? (x + width - pinInset) : (x + 4.0f * scale);
    drawList.AddRectFilled(pinX, y - pinSize - 1.0f, pinSize, pinSize, pinColor);
    if (isOutput) {
        const float textWidth = MeasureTextWidth(fontManager, label);
        drawList.AddText(fontManager, label, x + width - textWidth - 18.0f * scale, y + 2.0f, 0xFFD4D4D4);
    } else {
        drawList.AddText(fontManager, label, x + 16.0f * scale, y + 2.0f, 0xFFD4D4D4);
    }
}


std::string ToLowerCopy(const std::string& value) {
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    return ToLowerCopy(haystack).find(ToLowerCopy(needle)) != std::string::npos;
}

bool DrawInlineTextInput(UIDrawList& drawList, FontManager& fontManager, InputManager& inputManager,
                         const std::string& placeholder, std::string& text, bool& isActive,
                         float x, float y, float width, float height, bool allowInteraction = true) {
    const float mouseX = static_cast<float>(inputManager.GetMouseX());
    const float mouseY = static_cast<float>(inputManager.GetMouseY());
    const bool isHovered = allowInteraction && IsPointInRect(mouseX, mouseY, x, y, width, height);

    if (allowInteraction && inputManager.IsMouseButtonPressed(0)) {
        isActive = isHovered;
    }

    if (allowInteraction && isActive) {
        for (char ch : inputManager.GetTypedCharacters()) {
            if (ch == '\b') {
                if (!text.empty()) {
                    text.pop_back();
                }
            } else if (ch >= 32 && ch <= 126) {
                text.push_back(ch);
            }
        }
    }

    drawList.AddRectFilled(x - 2.0f, y - 2.0f, width + 4.0f, height + 4.0f, isActive ? 0xFFD77800 : 0xFF3B3B3F);
    drawList.AddRectFilled(x, y, width, height, isHovered ? 0xFF17181C : 0xFF111216);

    std::string displayText = text;
    if (isActive && ((GetTickCount() / 500) % 2 == 0)) {
        displayText += "_";
    }
    if (displayText.empty() && !isActive) {
        displayText = placeholder;
    }

    drawList.AddText(fontManager, displayText, x + 10.0f, y + 20.0f,
                     (text.empty() && !isActive) ? 0xFF7C8087 : 0xFFFFFFFF,
                     width - 20.0f);
    return isActive;
}

struct CreateActionItem {
    std::string id;
    std::string label;
    std::string subtitle;
    BlueprintEditor::NodeVisualKind visual = BlueprintEditor::NodeVisualKind::Function;
};

const char* ComponentKindToString(BlueprintEditor::ComponentKind kind) {
    switch (kind) {
    case BlueprintEditor::ComponentKind::SkeletalMesh:
        return "SkeletalMesh";
    case BlueprintEditor::ComponentKind::Camera:
        return "Camera";
    case BlueprintEditor::ComponentKind::Trigger:
        return "Trigger";
    case BlueprintEditor::ComponentKind::StaticMesh:
    default:
        return "StaticMesh";
    }
}

BlueprintEditor::ComponentKind ComponentKindFromString(const std::string& value) {
    if (value == "SkeletalMesh") {
        return BlueprintEditor::ComponentKind::SkeletalMesh;
    }
    if (value == "Camera") {
        return BlueprintEditor::ComponentKind::Camera;
    }
    if (value == "Trigger") {
        return BlueprintEditor::ComponentKind::Trigger;
    }
    return BlueprintEditor::ComponentKind::StaticMesh;
}

std::string NodeVisualToString(BlueprintEditor::NodeVisualKind visual) {
    switch (visual) {
    case BlueprintEditor::NodeVisualKind::Event: return "Event";
    case BlueprintEditor::NodeVisualKind::Field: return "Field";
    case BlueprintEditor::NodeVisualKind::Component: return "Component";
    case BlueprintEditor::NodeVisualKind::UIElement: return "UIElement";
    case BlueprintEditor::NodeVisualKind::Comment: return "Comment";
    case BlueprintEditor::NodeVisualKind::Function:
    default: return "Function";
    }
}

BlueprintEditor::NodeVisualKind NodeVisualFromString(const std::string& value) {
    if (value == "Event") {
        return BlueprintEditor::NodeVisualKind::Event;
    }
    if (value == "Field") {
        return BlueprintEditor::NodeVisualKind::Field;
    }
    if (value == "Component") {
        return BlueprintEditor::NodeVisualKind::Component;
    }
    if (value == "UIElement") {
        return BlueprintEditor::NodeVisualKind::UIElement;
    }
    if (value == "Comment") {
        return BlueprintEditor::NodeVisualKind::Comment;
    }
    return BlueprintEditor::NodeVisualKind::Function;
}

const char* PinKindToString(BlueprintEditor::PinKind kind) {
    switch (kind) {
    case BlueprintEditor::PinKind::Data:
        return "Data";
    case BlueprintEditor::PinKind::Exec:
    default:
        return "Exec";
    }
}

BlueprintEditor::PinKind PinKindFromString(const std::string& value) {
    if (value == "Data") {
        return BlueprintEditor::PinKind::Data;
    }
    return BlueprintEditor::PinKind::Exec;
}

float ScreenToGraph(float screenValue, float canvasValue, float panValue, float zoom) {
    return (screenValue - canvasValue - panValue) / zoom;
}
} // namespace
