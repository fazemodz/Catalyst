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
    for (char ch : text) {
        if (ch == '\n') {
            break;
        }
        width += fontManager.GetGlyph(ch).Advance;
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
    for (char ch : text) {
        const float glyphWidth = fontManager.GetGlyph(ch).Advance;
        if ((width + glyphWidth + ellipsisWidth) > maxWidth) {
            break;
        }
        fitted.push_back(ch);
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
    for (char ch : text) {
        if (ch == '\n') {
            cursorX = 0.0f;
            ++lineCount;
            continue;
        }

        const float glyphWidth = fontManager.GetGlyph(ch).Advance;
        if ((cursorX + glyphWidth) > wrapWidth) {
            cursorX = 0.0f;
            ++lineCount;
            if (ch == ' ') {
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

std::string EscapeJsonString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());

    for (char ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += ch; break;
        }
    }

    return escaped;
}

enum class JsonValueType {
    Null,
    Number,
    String,
    Bool,
    Array,
    Object
};

struct JsonValue {
    JsonValueType type = JsonValueType::Null;
    double numberValue = 0.0;
    bool boolValue = false;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::map<std::string, JsonValue> objectValue;
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text)
        : m_text(text) {
    }

    bool Parse(JsonValue& outValue) {
        SkipWhitespace();
        if (!ParseValue(outValue)) {
            return false;
        }

        SkipWhitespace();
        return m_pos == m_text.size();
    }

private:
    bool ParseValue(JsonValue& outValue) {
        SkipWhitespace();
        if (m_pos >= m_text.size()) {
            return false;
        }

        const char current = m_text[m_pos];
        if (current == '{') {
            return ParseObject(outValue);
        }
        if (current == '[') {
            return ParseArray(outValue);
        }
        if (current == '"') {
            outValue.type = JsonValueType::String;
            return ParseString(outValue.stringValue);
        }
        if (current == '-' || (current >= '0' && current <= '9')) {
            outValue.type = JsonValueType::Number;
            return ParseNumber(outValue.numberValue);
        }
        if (ConsumeLiteral("true")) {
            outValue.type = JsonValueType::Bool;
            outValue.boolValue = true;
            return true;
        }
        if (ConsumeLiteral("false")) {
            outValue.type = JsonValueType::Bool;
            outValue.boolValue = false;
            return true;
        }
        if (ConsumeLiteral("null")) {
            outValue.type = JsonValueType::Null;
            return true;
        }

        return false;
    }

    bool ParseObject(JsonValue& outValue) {
        if (!Match('{')) {
            return false;
        }

        outValue.type = JsonValueType::Object;
        outValue.objectValue.clear();
        SkipWhitespace();
        if (Match('}')) {
            return true;
        }

        while (m_pos < m_text.size()) {
            std::string key;
            if (!ParseString(key)) {
                return false;
            }
            SkipWhitespace();
            if (!Match(':')) {
                return false;
            }

            JsonValue child;
            if (!ParseValue(child)) {
                return false;
            }
            outValue.objectValue[key] = child;

            SkipWhitespace();
            if (Match('}')) {
                return true;
            }
            if (!Match(',')) {
                return false;
            }
            SkipWhitespace();
        }

        return false;
    }

    bool ParseArray(JsonValue& outValue) {
        if (!Match('[')) {
            return false;
        }

        outValue.type = JsonValueType::Array;
        outValue.arrayValue.clear();
        SkipWhitespace();
        if (Match(']')) {
            return true;
        }

        while (m_pos < m_text.size()) {
            JsonValue child;
            if (!ParseValue(child)) {
                return false;
            }
            outValue.arrayValue.push_back(child);

            SkipWhitespace();
            if (Match(']')) {
                return true;
            }
            if (!Match(',')) {
                return false;
            }
            SkipWhitespace();
        }

        return false;
    }

    bool ParseString(std::string& outValue) {
        if (!Match('"')) {
            return false;
        }

        outValue.clear();
        while (m_pos < m_text.size()) {
            const char current = m_text[m_pos++];
            if (current == '"') {
                return true;
            }
            if (current != '\\') {
                outValue += current;
                continue;
            }
            if (m_pos >= m_text.size()) {
                return false;
            }

            const char escaped = m_text[m_pos++];
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                outValue += escaped;
                break;
            case 'b':
                outValue += '\b';
                break;
            case 'f':
                outValue += '\f';
                break;
            case 'n':
                outValue += '\n';
                break;
            case 'r':
                outValue += '\r';
                break;
            case 't':
                outValue += '\t';
                break;
            default:
                return false;
            }
        }

        return false;
    }

    bool ParseNumber(double& outValue) {
        const size_t start = m_pos;
        if (m_text[m_pos] == '-') {
            ++m_pos;
        }
        while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos])) != 0) {
            ++m_pos;
        }
        if (m_pos < m_text.size() && m_text[m_pos] == '.') {
            ++m_pos;
            while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos])) != 0) {
                ++m_pos;
            }
        }
        if (m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E')) {
            ++m_pos;
            if (m_pos < m_text.size() && (m_text[m_pos] == '+' || m_text[m_pos] == '-')) {
                ++m_pos;
            }
            while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos])) != 0) {
                ++m_pos;
            }
        }

        try {
            outValue = std::stod(m_text.substr(start, m_pos - start));
            return true;
        } catch (...) {
            return false;
        }
    }

    void SkipWhitespace() {
        while (m_pos < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_pos])) != 0) {
            ++m_pos;
        }
    }

    bool Match(char expected) {
        if (m_pos < m_text.size() && m_text[m_pos] == expected) {
            ++m_pos;
            return true;
        }
        return false;
    }

    bool ConsumeLiteral(const char* literal) {
        const size_t literalLength = std::char_traits<char>::length(literal);
        if (m_pos + literalLength > m_text.size()) {
            return false;
        }
        if (m_text.compare(m_pos, literalLength, literal) != 0) {
            return false;
        }
        m_pos += literalLength;
        return true;
    }

    const std::string& m_text;
    size_t m_pos = 0;
};

const JsonValue* FindJsonField(const JsonValue& objectValue, const char* key) {
    if (objectValue.type != JsonValueType::Object) {
        return nullptr;
    }

    auto found = objectValue.objectValue.find(key);
    return found != objectValue.objectValue.end() ? &found->second : nullptr;
}

std::string GetJsonString(const JsonValue& objectValue, const char* key, const std::string& fallback = "") {
    const JsonValue* field = FindJsonField(objectValue, key);
    return (field != nullptr && field->type == JsonValueType::String) ? field->stringValue : fallback;
}

float GetJsonNumber(const JsonValue& objectValue, const char* key, float fallback) {
    const JsonValue* field = FindJsonField(objectValue, key);
    return (field != nullptr && field->type == JsonValueType::Number) ? static_cast<float>(field->numberValue) : fallback;
}

int GetJsonInt(const JsonValue& objectValue, const char* key, int fallback) {
    const JsonValue* field = FindJsonField(objectValue, key);
    return (field != nullptr && field->type == JsonValueType::Number) ? static_cast<int>(field->numberValue) : fallback;
}

bool GetJsonBool(const JsonValue& objectValue, const char* key, bool fallback) {
    const JsonValue* field = FindJsonField(objectValue, key);
    return (field != nullptr && field->type == JsonValueType::Bool) ? field->boolValue : fallback;
}

uint32_t GetJsonUInt(const JsonValue& objectValue, const char* key, uint32_t fallback) {
    const JsonValue* field = FindJsonField(objectValue, key);
    return (field != nullptr && field->type == JsonValueType::Number) ? static_cast<uint32_t>(field->numberValue) : fallback;
}

DirectX::XMFLOAT3 GetJsonFloat3(const JsonValue& objectValue, const char* key, const DirectX::XMFLOAT3& fallback) {
    const JsonValue* field = FindJsonField(objectValue, key);
    if (field == nullptr || field->type != JsonValueType::Array || field->arrayValue.size() != 3) {
        return fallback;
    }

    for (const JsonValue& component : field->arrayValue) {
        if (component.type != JsonValueType::Number) {
            return fallback;
        }
    }

    return {
        static_cast<float>(field->arrayValue[0].numberValue),
        static_cast<float>(field->arrayValue[1].numberValue),
        static_cast<float>(field->arrayValue[2].numberValue)
    };
}

DirectX::XMFLOAT4 GetJsonFloat4(const JsonValue& objectValue, const char* key, const DirectX::XMFLOAT4& fallback) {
    const JsonValue* field = FindJsonField(objectValue, key);
    if (field == nullptr || field->type != JsonValueType::Array || field->arrayValue.size() != 4) {
        return fallback;
    }

    for (const JsonValue& component : field->arrayValue) {
        if (component.type != JsonValueType::Number) {
            return fallback;
        }
    }

    return {
        static_cast<float>(field->arrayValue[0].numberValue),
        static_cast<float>(field->arrayValue[1].numberValue),
        static_cast<float>(field->arrayValue[2].numberValue),
        static_cast<float>(field->arrayValue[3].numberValue)
    };
}

void WriteJsonFloat3(std::ostream& outStream, const DirectX::XMFLOAT3& value) {
    outStream << "[" << value.x << ", " << value.y << ", " << value.z << "]";
}

void WriteJsonFloat4(std::ostream& outStream, const DirectX::XMFLOAT4& value) {
    outStream << "[" << value.x << ", " << value.y << ", " << value.z << ", " << value.w << "]";
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
