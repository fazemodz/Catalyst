#include "BlueprintEditor.h"

#include "Nodes/BlueprintNodeLibrary.h"
#include "../Scene/GameObject.h"
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
}

void BlueprintEditor::Open(const GameObject* targetObject) {
    m_isOpen = true;
    m_assetKind = AssetKind::Actor;
    m_graphDirty = true;
    m_activeTab = DocumentTab::EventGraph;
    m_targetLabel = BuildTargetLabel(targetObject);
    m_assetPath.clear();
    m_components.clear();
    m_assetBrowserItems.clear();
    m_assetBrowserDirty = true;
    m_draggingAssetBrowserIndex = -1;
    m_nextComponentId = 1;
    ClearLinkDrag();
    m_showCreateMenu = false;
    m_createSearch.clear();
    m_createSearchActive = false;
    m_showRigidBodyFields = false;
    m_showColliderFields = false;
    m_selectedNodeTextInputActive = false;
    m_draggingDesignerNodeId = -1;
    m_isResizingDesignerNode = false;
    m_contextClickPending = false;
    m_selectedComponentId = -1;
    m_statusMessage.clear();
    m_statusUntil = 0;
    m_savedAssetDocument.clear();
    m_hasSavedAssetDocument = false;
}

void BlueprintEditor::OpenAsset(const std::wstring& assetPath) {
    m_isOpen = true;
    m_assetKind = IsUIBlueprintPath(assetPath) ? AssetKind::UI : AssetKind::Actor;
    m_graphDirty = true;
    m_activeTab = DocumentTab::Viewport;
    m_assetPath = assetPath;
    m_targetLabel = BuildTargetLabel(nullptr);
    m_components.clear();
    m_assetBrowserItems.clear();
    m_assetBrowserDirty = true;
    m_draggingAssetBrowserIndex = -1;
    m_nextComponentId = 1;
    ClearLinkDrag();
    m_showCreateMenu = false;
    m_createSearch.clear();
    m_createSearchActive = false;
    m_showRigidBodyFields = false;
    m_showColliderFields = false;
    m_selectedNodeTextInputActive = false;
    m_draggingDesignerNodeId = -1;
    m_isResizingDesignerNode = false;
    m_contextClickPending = false;
    m_selectedComponentId = -1;
    m_statusMessage.clear();
    m_statusUntil = 0;
    m_savedAssetDocument.clear();
    m_hasSavedAssetDocument = false;
}

void BlueprintEditor::Close() {
    m_isOpen = false;
    m_assetKind = AssetKind::Actor;
    m_draggingNodeId = -1;
    ClearLinkDrag();
    m_isPanning = false;
    m_showCreateMenu = false;
    m_createSearchActive = false;
    m_showRigidBodyFields = false;
    m_showColliderFields = false;
    m_selectedNodeTextInputActive = false;
    m_draggingDesignerNodeId = -1;
    m_isResizingDesignerNode = false;
    m_contextClickPending = false;
    m_assetPath.clear();
    m_selectedComponentId = -1;
    m_assetBrowserItems.clear();
    m_assetBrowserDirty = true;
    m_draggingAssetBrowserIndex = -1;
    m_savedAssetDocument.clear();
    m_hasSavedAssetDocument = false;
}

bool BlueprintEditor::HasUnsavedChanges() const {
    if (!m_isOpen || !HasOpenAsset() || m_graphDirty || !m_hasSavedAssetDocument) {
        return false;
    }

    return BuildAssetDocument() != m_savedAssetDocument;
}

bool BlueprintEditor::SaveOpenAsset() {
    return SaveAsset();
}

std::wstring BlueprintEditor::GetDisplayName() const {
    if (HasOpenAsset()) {
        return fs::path(m_assetPath).stem().wstring();
    }

    return Utf8ToWide(m_targetLabel);
}

void BlueprintEditor::Draw(UIDrawList& drawList, UIContext& uiCtx, FontManager& fontManager, InputManager& inputManager,
                           float width, float height, const GameObject* selectedObject,
                           bool isStandaloneWindow, HWND windowHandle, bool interactionsBlocked) {
    if (!m_isOpen) {
        return;
    }

    (void)isStandaloneWindow;

    EnsureSeeded(selectedObject);
    if (m_assetBrowserDirty) {
        RefreshAssetBrowserItems();
    }

    const float menuBarH = 28.0f;
    const float toolBarH = 44.0f;
    const float graphTabH = 32.0f;
    const float graphToolsH = 34.0f;
    const float workspaceTop = menuBarH + toolBarH + graphTabH + graphToolsH;
    const float paletteW = std::clamp(width * 0.17f, 316.0f, 356.0f);
    const float inspectorW = std::clamp(width * 0.22f, 384.0f, 440.0f);
    const float canvasX = paletteW;
    const float canvasY = workspaceTop;
    const float canvasW = (std::max)(280.0f, width - paletteW - inspectorW);
    const float canvasH = (std::max)(180.0f, height - workspaceTop);
    const float inspectorX = width - inspectorW;
    const float contentBottom = height;
    const DWORD now = GetTickCount();
    const bool isViewportTab = m_activeTab == DocumentTab::Viewport;
    const bool isConstructionTab = m_activeTab == DocumentTab::ConstructionScript;
    const bool isEventGraphTab = m_activeTab == DocumentTab::EventGraph;
    const bool isUIAsset = IsUIAsset();
    const float toolY = menuBarH + 5.0f;
    const float toolTextY = toolY + 15.0f;
    const float assetInfoW = std::clamp(width * 0.18f, 180.0f, 280.0f);
    const float assetInfoX = width - assetInfoW - 18.0f;
    auto MeasureInfoBoxHeight = [&](const std::string& text, float wrapWidth, float minHeight,
                                    float topPadding = 18.0f, float bottomPadding = 16.0f) {
        return (std::max)(minHeight, topPadding + MeasureWrappedTextHeight(fontManager, text, wrapWidth) + bottomPadding);
    };

    drawList.AddRectFilled(0.0f, 0.0f, width, height, 0xFF111114);
    drawList.AddRectFilled(0.0f, 0.0f, width, menuBarH, 0xFF0D0D0F);
    drawList.AddRectFilled(0.0f, menuBarH, width, toolBarH, 0xFF17181B);
    drawList.AddRectFilled(0.0f, menuBarH + toolBarH, width, graphTabH, 0xFF1C1D21);
    drawList.AddRectFilled(0.0f, menuBarH + toolBarH + graphTabH, width, graphToolsH, 0xFF202228);

    float menuX = 12.0f;
    for (const char* menu : {"File", "Edit", "Asset", "View", "Debug", "Window", "Help"}) {
        uiCtx.Button(menu, menuX, 0.0f, 68.0f, menuBarH, 0x00000000, 0xFF232323, 0xFF303030);
        menuX += 70.0f;
    }

    auto HasNodeType = [&](const char* nodeTypeId) {
        return std::any_of(m_nodes.begin(), m_nodes.end(), [nodeTypeId](const Node& node) {
            return node.nodeTypeId == nodeTypeId;
        });
    };
    auto HasComponentOfKind = [&](ComponentKind kind) {
        return std::any_of(m_components.begin(), m_components.end(), [kind](const BlueprintComponent& component) {
            return component.kind == kind;
        });
    };
    auto PromptToAddCompileCamera = [&]() -> std::string {
        if (!HasOpenAsset() ||
            !HasNodeType(BlueprintNodes::kPlayerCharacterControllerNodeId) ||
            HasComponentOfKind(ComponentKind::Camera)) {
            return "";
        }

        const int popupResult = MessageBoxW(windowHandle != nullptr ? windowHandle : GetActiveWindow(),
                                            L"This Blueprint uses the Player Character node but has no camera component.\n\nDo you want to add a camera now?",
                                            L"Add Camera Component",
                                            MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON1);
        if (popupResult != IDYES) {
            return "";
        }

        m_selectedComponentId = AddComponent(ComponentKind::Camera);
        m_activeTab = DocumentTab::Viewport;
        m_showCreateMenu = false;
        m_createSearchActive = false;
        const BlueprintComponent* addedCamera = FindComponent(m_selectedComponentId);
        return addedCamera != nullptr ? addedCamera->name : "Camera";
    };

    if (uiCtx.Button("Compile", 14.0f, toolY, 84.0f, 26.0f,
                     0xFF29573B, 0xFF317146, 0xFF20462F)) {
        if (HasOpenAsset()) {
            const std::string addedCameraName = PromptToAddCompileCamera();
            const bool saved = SaveAsset();
            SetStatus(saved ? (addedCameraName.empty()
                                   ? ("Compiled " + fs::path(m_assetPath).stem().string())
                                   : ("Added " + addedCameraName + " and compiled " + fs::path(m_assetPath).stem().string()))
                            : (addedCameraName.empty()
                                   ? "Compile failed while saving the Blueprint asset"
                                   : "Compile failed after adding a camera component"),
                      saved ? 0xFF89D185 : 0xFFE07A7A, saved ? 2800 : 4200);
        } else {
            SetStatus("Compile is available for saved Blueprint assets. Open a .catalystblueprint to compile.",
                      0xFFE0C36F, 3800);
        }
    }
    if (uiCtx.Button("Save", 104.0f, toolY, 64.0f, 26.0f,
                     0xFF2B3A4E, 0xFF38506B, 0xFF223043)) {
        if (HasOpenAsset()) {
            const bool saved = SaveAsset();
            SetStatus(saved ? ("Saved " + fs::path(m_assetPath).filename().string()) : "Save failed",
                      saved ? 0xFF89D185 : 0xFFE07A7A, saved ? 2600 : 4200);
        } else {
            SetStatus("This preview graph is transient. Create or open a Blueprint asset to save it.",
                      0xFFE0C36F, 3600);
        }
    }
    if (uiCtx.Button("Find", 174.0f, toolY, 58.0f, 26.0f,
                     0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
        if (isEventGraphTab) {
            OpenCreateMenu(canvasX + canvasW * 0.5f - 120.0f, canvasY + 60.0f, canvasX, canvasY);
        } else {
            m_activeTab = DocumentTab::EventGraph;
            m_showCreateMenu = false;
            SetStatus("Switched to EventGraph. Use Find there to add nodes.", 0xFFB7BEC8, 2400);
        }
    }
    if (uiCtx.Button("Settings", 238.0f, toolY, 90.0f, 26.0f,
                     0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
        SetStatus("Class Settings is scaffolded. Use Viewport for components and EventGraph for logic.",
                  0xFFE0C36F, 3200);
    }
    if (uiCtx.Button("Defaults", 334.0f, toolY, 88.0f, 26.0f,
                     0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
        SetStatus("Class Defaults is scaffolded. Component authoring and graph persistence are active in this pass.",
                  0xFFE0C36F, 3200);
    }
    if (uiCtx.Button(isEventGraphTab ? "Reset Layout" : "Deselect", 428.0f, toolY, 96.0f, 26.0f,
                     0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
        if (isEventGraphTab) {
            RebuildGraph(selectedObject);
            SetStatus(HasOpenAsset() ? "Reset the EventGraph to the default Blueprint starter layout. Save to keep it."
                                     : "Reset the preview EventGraph layout.",
                      0xFFB7BEC8, 3200);
        } else {
            m_selectedComponentId = -1;
            SetStatus("Cleared the selected Blueprint component.", 0xFFB7BEC8, 2200);
        }
    }
    if (uiCtx.Button(isEventGraphTab ? "Frame Node" : "EventGraph", 530.0f, toolY, 98.0f, 26.0f,
                     0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
        if (isEventGraphTab) {
            if (m_nodes.empty()) {
                SetStatus("Nothing to frame yet. Add a node with right click.", 0xFFE0C36F, 2600);
            } else {
                FrameNode(m_selectedNodeId, canvasW, canvasH);
            }
        } else {
            m_activeTab = DocumentTab::EventGraph;
            m_showCreateMenu = false;
        }
    }

    const std::string headerHint = isViewportTab
        ? (isUIAsset ? "Preview this UI Blueprint's designer surface here."
                     : "Build this Blueprint's component setup here.")
        : isConstructionTab
        ? (isUIAsset ? "UI hierarchy scaffold." : "Reserved for component setup flow.")
        : HasOpenAsset() ? (isUIAsset
                                ? "Right click to add graph nodes. UI widget creation happens from gameplay Blueprints."
                                : "Right click to add nodes. Drag pins to connect. Drag UI Blueprint assets onto Create Widget pins.")
                         : selectedObject ? "Previewing Blueprint authoring for the selected actor."
                                          : "Open a Blueprint asset or actor to start authoring.";
    const float hintX = 650.0f;
    float hintRight = assetInfoX - 12.0f;
    if (!m_statusMessage.empty() && now <= m_statusUntil) {
        const std::string statusLabel = FitTextToWidth(fontManager, m_statusMessage, 220.0f);
        const float statusWidth = MeasureTextWidth(fontManager, statusLabel) + 18.0f;
        const float statusX = assetInfoX - statusWidth - 12.0f;
        if (statusX > hintX + 96.0f) {
            DrawFieldBadge(drawList, fontManager, statusLabel, statusX, toolY + 2.0f, m_statusColor);
            hintRight = statusX - 12.0f;
        }
    }
    const float hintWidth = (std::max)(0.0f, hintRight - hintX);
    if (hintWidth > 90.0f) {
        drawList.AddText(fontManager, FitTextToWidth(fontManager, headerHint, hintWidth),
                         hintX, toolTextY, 0xFFD0D4DB);
    }
    drawList.AddText(fontManager,
                     FitTextToWidth(fontManager, HasOpenAsset()
                                                      ? fs::path(m_assetPath).filename().string()
                                                      : (isUIAsset ? "Parent Class: UserWidget" : "Parent Class: Actor"),
                                    assetInfoW),
                     assetInfoX, toolTextY, 0xFFB8C0CB);

    const float tabY = menuBarH + toolBarH;
    auto DrawDocumentTab = [&](const char* label, DocumentTab tab, float x, float tabWidth) {
        const bool active = m_activeTab == tab;
        drawList.AddRectFilled(x, tabY + 4.0f, tabWidth, 24.0f, active ? 0xFF2C2E35 : 0xFF1C1D21);
        if (active) {
            drawList.AddRectFilled(x, tabY + 4.0f, tabWidth, 3.0f, 0xFFD77800);
        }
        if (uiCtx.Button("", x, tabY + 4.0f, tabWidth, 24.0f, 0x00000000, 0x12FFFFFF, 0x16FFFFFF)) {
            m_activeTab = tab;
            m_showCreateMenu = false;
            m_createSearchActive = false;
            ClearLinkDrag();
        }
        drawList.AddText(fontManager, label, x + 10.0f, tabY + 19.0f, active ? 0xFFFFFFFF : 0xFF7E7E84);
    };
    DrawDocumentTab(isUIAsset ? "Designer" : "Viewport", DocumentTab::Viewport, canvasX + 10.0f, isUIAsset ? 96.0f : 98.0f);
    DrawDocumentTab(isUIAsset ? "Widget Tree" : "Construction Script", DocumentTab::ConstructionScript, canvasX + 112.0f, isUIAsset ? 116.0f : 152.0f);
    DrawDocumentTab(isUIAsset ? "Graph" : "EventGraph", DocumentTab::EventGraph, canvasX + (isUIAsset ? 232.0f : 268.0f), isUIAsset ? 92.0f : 116.0f);

    const float graphToolsY = menuBarH + toolBarH + graphTabH;
    const float visibleCenterGraphX = (canvasW * 0.5f - m_pan.x) / m_zoom;
    const float visibleCenterGraphY = (canvasH * 0.5f - m_pan.y) / m_zoom;
    if (isViewportTab) {
        if (isUIAsset) {
            drawList.AddText(fontManager, "Add UI Element", canvasX + 14.0f, graphToolsY + 18.0f, 0xFFD7DADF);
            auto AddUIElementFromToolbar = [&](const char* label, UIElementKind kind, float x, float buttonWidth) {
                if (uiCtx.Button(label, x, graphToolsY + 2.0f, buttonWidth, 24.0f,
                                 0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
                    m_selectedNodeId = AddUIElementNode(kind, visibleCenterGraphX - 120.0f, visibleCenterGraphY - 40.0f);
                    SetStatus("Added " + BuildUIElementTypeLabel(kind) + ".", 0xFFB7BEC8, 2200);
                }
            };
            AddUIElementFromToolbar("Canvas", UIElementKind::Canvas, canvasX + 126.0f, 82.0f);
            AddUIElementFromToolbar("Button", UIElementKind::Button, canvasX + 214.0f, 82.0f);
            AddUIElementFromToolbar("Text", UIElementKind::TextBlock, canvasX + 302.0f, 70.0f);
            AddUIElementFromToolbar("Image", UIElementKind::Image, canvasX + 378.0f, 78.0f);
            if (uiCtx.Button("Open Graph", canvasX + 462.0f, graphToolsY + 2.0f, 100.0f, 24.0f,
                             0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
                m_activeTab = DocumentTab::EventGraph;
            }
            const int uiElementCount = static_cast<int>(std::count_if(m_nodes.begin(), m_nodes.end(), [](const Node& node) {
                return node.visual == NodeVisualKind::UIElement;
            }));
            drawList.AddText(fontManager,
                             std::to_string(uiElementCount) + (uiElementCount == 1 ? " UI element" : " UI elements") + " in this widget",
                             canvasX + canvasW - 250.0f, graphToolsY + 18.0f, 0xFFC0C8D2);
        } else {
            drawList.AddText(fontManager, "Add Component", canvasX + 14.0f, graphToolsY + 18.0f, 0xFFD7DADF);
            auto AddComponentFromToolbar = [&](const char* label, ComponentKind kind, float x, float buttonWidth) {
                if (uiCtx.Button(label, x, graphToolsY + 2.0f, buttonWidth, 24.0f,
                                 0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
                    m_selectedComponentId = AddComponent(kind);
                    if (const BlueprintComponent* component = FindComponent(m_selectedComponentId)) {
                        SetStatus("Added " + component->name + ".", 0xFFB7BEC8, 2200);
                    }
                }
            };
            AddComponentFromToolbar("Static Mesh", ComponentKind::StaticMesh, canvasX + 136.0f, 106.0f);
            AddComponentFromToolbar("Skeletal Mesh", ComponentKind::SkeletalMesh, canvasX + 248.0f, 120.0f);
            AddComponentFromToolbar("Camera", ComponentKind::Camera, canvasX + 374.0f, 86.0f);
            AddComponentFromToolbar("Trigger", ComponentKind::Trigger, canvasX + 466.0f, 86.0f);
            drawList.AddText(fontManager,
                             std::to_string(m_components.size()) + (m_components.size() == 1 ? " component" : " components") + " in this Blueprint",
                             canvasX + canvasW - 248.0f, graphToolsY + 18.0f, 0xFFC0C8D2);
        }
    } else if (isConstructionTab) {
        if (uiCtx.Button(isUIAsset ? "Open Designer" : "Open Viewport", canvasX + 14.0f, graphToolsY + 2.0f, 112.0f, 24.0f,
                         0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
            m_activeTab = DocumentTab::Viewport;
        }
        if (uiCtx.Button(isUIAsset ? "Open Graph" : "Open EventGraph", canvasX + 132.0f, graphToolsY + 2.0f, 118.0f, 24.0f,
                         0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
            m_activeTab = DocumentTab::EventGraph;
        }
        drawList.AddText(fontManager, isUIAsset ? "Widget hierarchy and ordering" : "Construction Script scaffold",
                         canvasX + canvasW - 234.0f, graphToolsY + 18.0f, 0xFFC0C8D2);
    } else {
        if (uiCtx.Button("Add Comment", canvasX + 14.0f, graphToolsY + 2.0f, 98.0f, 24.0f,
                         0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
            m_selectedNodeId = AddCommentNode("Comment", visibleCenterGraphX - 160.0f, visibleCenterGraphY - 90.0f);
            SetStatus("Added a comment box to the current graph.", 0xFFB7BEC8, 2400);
        }
        if (uiCtx.Button("Zoom To Fit", canvasX + 118.0f, graphToolsY + 2.0f, 92.0f, 24.0f,
                         0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
            FrameNode(-1, canvasW, canvasH);
        }
        if (uiCtx.Button("Straighten", canvasX + 216.0f, graphToolsY + 2.0f, 88.0f, 24.0f,
                         0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
            Node* selectedNode = FindNode(m_selectedNodeId);
            if (selectedNode != nullptr) {
                selectedNode->x = std::round(selectedNode->x / 24.0f) * 24.0f;
                selectedNode->y = std::round(selectedNode->y / 24.0f) * 24.0f;
                SetStatus("Snapped the selected node to the graph grid.", 0xFFB7BEC8, 2200);
            } else {
                SetStatus("Select a node to straighten it.", 0xFFE0C36F, 2200);
            }
        }
        drawList.AddText(fontManager,
                         "Graph: EventGraph  |  Zoom " + std::to_string(static_cast<int>(std::round(m_zoom * 100.0f))) + "%",
                         canvasX + canvasW - 228.0f, graphToolsY + 18.0f, 0xFFC0C8D2);
        if (!interactionsBlocked) {
            UpdateInteraction(inputManager, canvasX, canvasY, canvasW, canvasH);
        }
    }

    drawList.AddRectFilled(0.0f, workspaceTop, paletteW, contentBottom - workspaceTop, 0xFF16171A);
    drawList.AddRectFilled(0.0f, workspaceTop, paletteW, 32.0f, 0xFF101114);
    drawList.AddText(fontManager,
                     isViewportTab ? (isUIAsset ? "Designer" : "Components")
                                   : (isUIAsset ? "Widget Blueprint" : "My Blueprint"),
                     14.0f, workspaceTop + 21.0f, 0xFFFFFFFF);

    float paletteX = 14.0f;
    float paletteInnerW = paletteW - 28.0f;
    float paletteY = workspaceTop + 42.0f;
    drawList.AddRectFilled(paletteX, paletteY, paletteInnerW, 28.0f, 0xFF101010);
    drawList.AddText(fontManager,
                     isViewportTab ? (isUIAsset ? "Search Designer" : "Search Components")
                                   : (isUIAsset ? "Search Widget Blueprint" : "Search My Blueprint"),
                     paletteX + 10.0f, paletteY + 19.0f, 0xFF959CA6);
    paletteY += 42.0f;

    DrawSectionHeader(drawList, fontManager, "BLUEPRINT", paletteX, paletteY, paletteInnerW);
    paletteY += 32.0f;
    drawList.AddRectFilled(paletteX, paletteY, paletteInnerW, 56.0f, 0xFF101010);
    drawList.AddText(fontManager, FitTextToWidth(fontManager, m_targetLabel, paletteInnerW - 18.0f),
                     paletteX + 10.0f, paletteY + 20.0f, 0xFFFFFFFF);
    drawList.AddText(fontManager, HasOpenAsset()
                                      ? (isUIAsset ? "User Widget Blueprint" : "Data-Only Blueprint")
                                      : "Transient Graph Preview",
                     paletteX + 10.0f, paletteY + 44.0f, 0xFF9098A2);
    paletteY += 70.0f;

    if (!isViewportTab) {
        DrawSectionHeader(drawList, fontManager, "GRAPHS", paletteX, paletteY, paletteInnerW);
        paletteY += 32.0f;
        auto DrawGraphEntry = [&](const char* label, DocumentTab tab) {
            const bool active = m_activeTab == tab;
            drawList.AddRectFilled(paletteX, paletteY, paletteInnerW, 24.0f, active ? 0xFF1C2633 : 0xFF17181C);
            if (uiCtx.Button("", paletteX, paletteY, paletteInnerW, 24.0f,
                             0x00000000, 0x14FFFFFF, 0x18FFFFFF)) {
                m_activeTab = tab;
            }
            drawList.AddText(fontManager, label, paletteX + 12.0f, paletteY + 16.0f, active ? 0xFFFFFFFF : 0xFFC9CDD3);
            paletteY += 28.0f;
        };
        DrawGraphEntry(isUIAsset ? "Designer" : "Viewport", DocumentTab::Viewport);
        DrawGraphEntry(isUIAsset ? "Widget Tree" : "Construction Script", DocumentTab::ConstructionScript);
        DrawGraphEntry(isUIAsset ? "Graph" : "EventGraph", DocumentTab::EventGraph);
        paletteY += 8.0f;
    }

    DrawSectionHeader(drawList, fontManager, isUIAsset ? "WIDGET TREE" : "COMPONENTS", paletteX, paletteY, paletteInnerW);
    paletteY += 32.0f;
    if (isUIAsset) {
        int visibleElementCount = 0;
        for (const Node& node : m_nodes) {
            if (node.visual != NodeVisualKind::UIElement) {
                continue;
            }

            const bool selected = node.id == m_selectedNodeId;
            drawList.AddRectFilled(paletteX, paletteY, paletteInnerW, 44.0f, selected ? 0xFF1C2633 : 0xFF191A1E);
            drawList.AddRectFilled(paletteX + 6.0f, paletteY + 8.0f, 10.0f, 10.0f, UIElementAccentColor(node.uiElementKind));
            drawList.AddText(fontManager, FitTextToWidth(fontManager, node.title, paletteInnerW - 36.0f),
                             paletteX + 24.0f, paletteY + 16.0f, 0xFFF3F5F7, paletteInnerW - 36.0f);
            drawList.AddText(fontManager, BuildUIElementTypeLabel(node.uiElementKind),
                             paletteX + 24.0f, paletteY + 34.0f, 0xFFB5BEC8, paletteInnerW - 36.0f);
            if (uiCtx.Button("", paletteX, paletteY, paletteInnerW, 44.0f,
                             0x00000000, 0x14FFFFFF, 0x22FFFFFF)) {
                m_selectedNodeId = node.id;
                m_selectedComponentId = -1;
            }
            paletteY += 48.0f;
            ++visibleElementCount;
        }

        if (visibleElementCount == 0) {
            drawList.AddRectFilled(paletteX, paletteY, paletteInnerW, 50.0f, 0xFF101010);
            drawList.AddText(fontManager,
                             "Add Canvas, Button, Text, or Image elements from the Designer toolbar.",
                             paletteX + 10.0f, paletteY + 24.0f, 0xFFACB3BC, paletteInnerW - 20.0f);
            paletteY += 62.0f;
        } else {
            paletteY += 8.0f;
        }
    } else if (m_components.empty()) {
        const std::string emptyComponentsText = "No components added yet. Use the toolbar to add meshes, cameras, or triggers.";
        drawList.AddRectFilled(paletteX, paletteY, paletteInnerW, 44.0f, 0xFF101010);
        drawList.AddText(fontManager, FitTextToWidth(fontManager, emptyComponentsText, paletteInnerW - 20.0f),
                         paletteX + 10.0f, paletteY + 24.0f, 0xFFACB3BC);
        paletteY += 56.0f;
    } else {
        for (const BlueprintComponent& component : m_components) {
            const bool selected = component.id == m_selectedComponentId;
            drawList.AddRectFilled(paletteX, paletteY, paletteInnerW, 42.0f, selected ? 0xFF1C2633 : 0xFF191A1E);
            drawList.AddRectFilled(paletteX + 6.0f, paletteY + 8.0f, 10.0f, 10.0f, ComponentAccentColor(component.kind));
            drawList.AddText(fontManager, FitTextToWidth(fontManager, component.name, paletteInnerW - 36.0f),
                             paletteX + 24.0f, paletteY + 16.0f, 0xFFF3F5F7, paletteInnerW - 36.0f);
            drawList.AddText(fontManager, BuildComponentTypeLabel(component.kind),
                             paletteX + 24.0f, paletteY + 34.0f, 0xFFB5BEC8, paletteInnerW - 36.0f);
            if (uiCtx.Button("", paletteX, paletteY, paletteInnerW, 42.0f,
                             0x00000000, 0x14FFFFFF, 0x22FFFFFF)) {
                if (isViewportTab || isConstructionTab) {
                    m_selectedComponentId = component.id;
                    m_selectedNodeId = -1;
                } else {
                    m_selectedNodeId = AddComponentNode(component,
                                                        ((canvasW * 0.5f) - m_pan.x) / m_zoom - 122.0f,
                                                        ((canvasH * 0.35f) - m_pan.y) / m_zoom + static_cast<float>(m_nodes.size() % 4) * 32.0f);
                    SetStatus("Added component reference node for " + component.name + ".", 0xFFB7BEC8, 2200);
                }
            }
            paletteY += 46.0f;
        }
        paletteY += 10.0f;
    }

    if (!isViewportTab && isUIAsset) {
        DrawSectionHeader(drawList, fontManager, "UI FUNCTIONS", paletteX, paletteY, paletteInnerW);
        paletteY += 32.0f;
        drawList.AddRectFilled(paletteX, paletteY, paletteInnerW, 22.0f, 0xFF191A1E);
        drawList.AddRectFilled(paletteX + 6.0f, paletteY + 5.0f, 10.0f, 10.0f, 0xFFB36A0B);
        drawList.AddText(fontManager, "Set Text Color", paletteX + 24.0f, paletteY + 15.0f, 0xFFD7DADF);
        if (uiCtx.Button("", paletteX, paletteY, paletteInnerW, 22.0f,
                         0x00000000, 0x14FFFFFF, 0x22FFFFFF)) {
            Node node;
            node.id = NextNodeId();
            node.title = "Set Text Color";
            node.subtitle = "UI Function";
            node.x = visibleCenterGraphX - 126.0f;
            node.y = visibleCenterGraphY - 75.0f;
            node.width = 252.0f;
            node.height = 152.0f;
            node.visual = NodeVisualKind::Function;
            node.nodeTypeId = BlueprintNodes::kSetTextColorNodeId;
            node.tint = UIntColorToFloat4(0xFFFFC857);
            node.canDelete = true;
            m_selectedNodeId = node.id;
            m_nodes.push_back(node);
            SetStatus("Added Set Text Color.", 0xFFB7BEC8, 2200);
        }
        paletteY += 32.0f;
        drawList.AddText(fontManager,
                         "Buttons fire OnClicked exec links. Connect a TextBlock into Set Text Color's Target pin.",
                         paletteX + 6.0f, paletteY + 14.0f, 0xFF8E99A5, paletteInnerW - 12.0f);
        paletteY += 32.0f;
    } else if (!isViewportTab && !isUIAsset) {
        DrawSectionHeader(drawList, fontManager, "VARIABLES", paletteX, paletteY, paletteInnerW);
        paletteY += 32.0f;
        drawList.AddText(fontManager, "Optional physics fields", paletteX, paletteY + 14.0f, 0xFF8E8E8E);
        paletteY += 26.0f;
        uiCtx.Checkbox("Rigid Body Fields", m_showRigidBodyFields, paletteX, paletteY, 16.0f);
        paletteY += 24.0f;
        uiCtx.Checkbox("Collider Fields", m_showColliderFields, paletteX, paletteY, 16.0f);
        paletteY += 28.0f;

        auto DrawVariableGroup = [&](const char* title, std::span<const BlueprintFieldDescriptor> fields, const char* prefix) {
            drawList.AddRectFilled(paletteX, paletteY, paletteInnerW, 22.0f, 0xFF23252A);
            drawList.AddText(fontManager, title, paletteX + 10.0f, paletteY + 15.0f, 0xFFC9CDD3);
            paletteY += 28.0f;

            for (const BlueprintFieldDescriptor& field : fields) {
                drawList.AddRectFilled(paletteX, paletteY, paletteInnerW, 22.0f, 0xFF191A1E);
                drawList.AddRectFilled(paletteX + 6.0f, paletteY + 5.0f, 10.0f, 10.0f, FieldTypeColor(field.type));
                drawList.AddText(fontManager, std::string(prefix) + " " + field.name, paletteX + 24.0f, paletteY + 15.0f, 0xFFD7DADF);
                if (uiCtx.Button("", paletteX, paletteY, paletteInnerW, 22.0f,
                                 0x00000000, 0x14FFFFFF, 0x22FFFFFF)) {
                    const float spawnX = ((canvasW * 0.5f) - m_pan.x) / m_zoom - 110.0f;
                    const float spawnY = ((canvasH * 0.35f) - m_pan.y) / m_zoom + static_cast<float>(m_nodes.size() % 5) * 32.0f;
                    m_selectedNodeId = AddFieldNode(field, spawnX, spawnY);
                    SetStatus(std::string("Added variable node for ") + field.name + ".", 0xFFB7BEC8, 2200);
                }
                paletteY += 24.0f;
            }
            paletteY += 12.0f;
        };

        if (m_showRigidBodyFields) {
            DrawVariableGroup("Rigid Body", GetRigidBodyBlueprintFields(), "RB");
        }
        if (m_showColliderFields) {
            DrawVariableGroup("Collider", GetColliderBlueprintFields(), "Col");
        }

        DrawSectionHeader(drawList, fontManager, "FUNCTIONS", paletteX, paletteY, paletteInnerW);
        paletteY += 32.0f;
        for (const char* functionName : {"Apply Exposed Physics", "Refresh Preview Data"}) {
            drawList.AddRectFilled(paletteX, paletteY, paletteInnerW, 22.0f, 0xFF191A1E);
            drawList.AddRectFilled(paletteX + 6.0f, paletteY + 5.0f, 10.0f, 10.0f, 0xFFB36A0B);
            drawList.AddText(fontManager, functionName, paletteX + 24.0f, paletteY + 15.0f, 0xFFD7DADF);
            paletteY += 24.0f;
        }
        if (!isUIAsset) {
            paletteY += 12.0f;
            DrawSectionHeader(drawList, fontManager, "WIDGET ASSETS", paletteX, paletteY, paletteInnerW);
            paletteY += 32.0f;
            if (m_assetBrowserItems.empty()) {
                drawList.AddRectFilled(paletteX, paletteY, paletteInnerW, 50.0f, 0xFF101010);
                drawList.AddText(fontManager,
                                 "Open a saved Blueprint inside a project to browse UI Blueprint assets here.",
                                 paletteX + 10.0f, paletteY + 24.0f, 0xFFACB3BC, paletteInnerW - 20.0f);
                paletteY += 62.0f;
            } else {
                const float mouseX = static_cast<float>(inputManager.GetMouseX());
                const float mouseY = static_cast<float>(inputManager.GetMouseY());
                const int visibleAssetCount = (std::min)(static_cast<int>(m_assetBrowserItems.size()), 6);
                for (int itemIndex = 0; itemIndex < visibleAssetCount; ++itemIndex) {
                    const AssetBrowserItem& item = m_assetBrowserItems[static_cast<size_t>(itemIndex)];
                    const bool hovered = IsPointInRect(mouseX, mouseY, paletteX, paletteY, paletteInnerW, 28.0f);
                    drawList.AddRectFilled(paletteX, paletteY, paletteInnerW, 28.0f, hovered ? 0xFF1E2530 : 0xFF191A1E);
                    drawList.AddRectFilled(paletteX + 6.0f, paletteY + 6.0f, 10.0f, 10.0f, 0xFF86D7C7);
                    drawList.AddText(fontManager, FitTextToWidth(fontManager, item.label, paletteInnerW - 38.0f),
                                     paletteX + 24.0f, paletteY + 16.0f, 0xFFF3F5F7, paletteInnerW - 38.0f);
                    if (!interactionsBlocked && hovered && inputManager.IsMouseButtonPressed(0)) {
                        m_draggingAssetBrowserIndex = itemIndex;
                    }
                    paletteY += 32.0f;
                }
                if (static_cast<int>(m_assetBrowserItems.size()) > visibleAssetCount) {
                    drawList.AddText(fontManager,
                                     "Showing first " + std::to_string(visibleAssetCount) + " widget assets.",
                                     paletteX + 6.0f, paletteY + 14.0f, 0xFF8E99A5, paletteInnerW - 12.0f);
                    paletteY += 26.0f;
                }
            }
        }
    } else if (isViewportTab) {
        DrawSectionHeader(drawList, fontManager, "GRAPH ACCESS", paletteX, paletteY, paletteInnerW);
        paletteY += 32.0f;
        const std::string graphAccessText = "Select a component here, then switch to EventGraph to drop it as a reference node.";
        drawList.AddRectFilled(paletteX, paletteY, paletteInnerW, 52.0f, 0xFF101010);
        drawList.AddText(fontManager, FitTextToWidth(fontManager, graphAccessText, paletteInnerW - 20.0f),
                         paletteX + 10.0f, paletteY + 24.0f, 0xFFACB3BC);
        paletteY += 64.0f;
    } else {
        DrawSectionHeader(drawList, fontManager, "UI GRAPH", paletteX, paletteY, paletteInnerW);
        paletteY += 32.0f;
        drawList.AddRectFilled(paletteX, paletteY, paletteInnerW, 74.0f, 0xFF101010);
        drawList.AddText(fontManager,
                         "Use this graph for widget lifecycle logic. Gameplay Blueprints can instantiate this asset with Create Widget.",
                         paletteX + 10.0f, paletteY + 24.0f, 0xFFACB3BC, paletteInnerW - 20.0f);
        paletteY += 86.0f;
    }

    drawList.AddRectFilled(inspectorX, workspaceTop, inspectorW, contentBottom - workspaceTop, 0xFF17181C);
    drawList.AddRectFilled(inspectorX, workspaceTop, inspectorW, 32.0f, 0xFF101114);
    drawList.AddText(fontManager, "Details", inspectorX + 14.0f, workspaceTop + 21.0f, 0xFFFFFFFF);

    float inspectorRowX = inspectorX + 14.0f;
    float inspectorRowW = inspectorW - 28.0f;
    float inspectorY = workspaceTop + 42.0f;
    drawList.AddRectFilled(inspectorRowX, inspectorY, inspectorRowW, 28.0f, 0xFF101010);
    drawList.AddText(fontManager, "Search Details", inspectorRowX + 10.0f, inspectorY + 19.0f, 0xFF959CA6);
    inspectorY += 42.0f;

    auto DrawBlueprintSummary = [&]() {
        DrawSectionHeader(drawList, fontManager, "BLUEPRINT", inspectorRowX, inspectorY, inspectorRowW);
        inspectorY += 32.0f;
        drawList.AddRectFilled(inspectorRowX, inspectorY, inspectorRowW, 140.0f, 0xFF101010);
        drawList.AddText(fontManager,
                         FitTextToWidth(fontManager,
                                        "Asset: " + (HasOpenAsset() ? fs::path(m_assetPath).filename().string() : std::string("Transient Preview")),
                                        inspectorRowW - 20.0f),
                         inspectorRowX + 10.0f, inspectorY + 20.0f, 0xFFFFFFFF);
        drawList.AddText(fontManager, IsUIAsset() ? "Widget Type: UserWidget" : ("Components: " + std::to_string(m_components.size())),
                         inspectorRowX + 10.0f, inspectorY + 44.0f, 0xFFF1F3F5);
        drawList.AddText(fontManager,
                         FitTextToWidth(fontManager,
                                        "Nodes: " + std::to_string(m_nodes.size()) + "  |  Links: " + std::to_string(m_links.size()),
                                        inspectorRowW - 20.0f),
                         inspectorRowX + 10.0f, inspectorY + 68.0f, 0xFFD4D9E0);
        drawList.AddText(fontManager,
                         FitTextToWidth(fontManager, IsUIAsset()
                                                        ? "Save writes graph layout and UI widget asset metadata to the Blueprint asset."
                                                        : "Save writes graph layout and component data to the Blueprint asset.",
                                        inspectorRowW - 20.0f),
                         inspectorRowX + 10.0f, inspectorY + 92.0f, 0xFFD4D9E0);
        drawList.AddText(fontManager,
                         FitTextToWidth(fontManager,
                                        IsUIAsset()
                                            ? "Gameplay Blueprints can Create Widget from this asset and Add To Viewport in Play mode."
                                            : "The first mesh drives preview. Cameras and triggers apply on spawned Blueprint instances.",
                                        inspectorRowW - 20.0f),
                         inspectorRowX + 10.0f, inspectorY + 116.0f, 0xFFB6C0CA);
    };

    if (isViewportTab || isConstructionTab) {
        if (isUIAsset) {
            const std::string selectionHint = isViewportTab
                ? "This UI Blueprint currently previews as a viewport card. Create Widget and Add To Viewport will spawn it in Play mode."
                : "Widget tree authoring is scaffolded here for future UI hierarchy editing.";
            const float selectionHintH = MeasureInfoBoxHeight(selectionHint, inspectorRowW - 24.0f, 104.0f);
            DrawSectionHeader(drawList, fontManager, "SELECTION", inspectorRowX, inspectorY, inspectorRowW);
            inspectorY += 32.0f;
            drawList.AddRectFilled(inspectorRowX, inspectorY, inspectorRowW, selectionHintH, 0xFF101010);
            drawList.AddText(fontManager, FitTextToWidth(fontManager, selectionHint, inspectorRowW - 24.0f),
                             inspectorRowX + 12.0f, inspectorY + 26.0f, 0xFFD3D7DD);
            inspectorY += selectionHintH + 16.0f;
        } else {
            BlueprintComponent* selectedComponent = FindComponent(m_selectedComponentId);
            if (selectedComponent == nullptr) {
            const std::string selectionHint = "Select a Blueprint component to edit mesh assets, camera possession, and trigger settings.";
            const float selectionHintH = MeasureInfoBoxHeight(selectionHint, inspectorRowW - 24.0f, 88.0f);
            DrawSectionHeader(drawList, fontManager, "SELECTION", inspectorRowX, inspectorY, inspectorRowW);
            inspectorY += 32.0f;
            drawList.AddRectFilled(inspectorRowX, inspectorY, inspectorRowW, selectionHintH, 0xFF101010);
            drawList.AddText(fontManager, FitTextToWidth(fontManager, selectionHint, inspectorRowW - 24.0f),
                             inspectorRowX + 12.0f, inspectorY + 26.0f, 0xFFD3D7DD);
            inspectorY += selectionHintH + 16.0f;
            } else {
            DrawSectionHeader(drawList, fontManager, "SELECTION", inspectorRowX, inspectorY, inspectorRowW);
            inspectorY += 32.0f;
            drawList.AddRectFilled(inspectorRowX, inspectorY, inspectorRowW, 96.0f, 0xFF101010);
            drawList.AddRectFilled(inspectorRowX, inspectorY, inspectorRowW, 24.0f, ComponentAccentColor(selectedComponent->kind));
            drawList.AddText(fontManager, FitTextToWidth(fontManager, selectedComponent->name, inspectorRowW - 24.0f),
                             inspectorRowX + 10.0f, inspectorY + 16.0f, 0xFFFFFFFF);
            drawList.AddText(fontManager, BuildComponentTypeLabel(selectedComponent->kind),
                             inspectorRowX + 10.0f, inspectorY + 44.0f, 0xFFE3E7EC, inspectorRowW - 20.0f);
            drawList.AddText(fontManager, FitTextToWidth(fontManager, BuildComponentSummary(*selectedComponent), inspectorRowW - 20.0f),
                             inspectorRowX + 10.0f, inspectorY + 68.0f, 0xFFBAC3CE);
            inspectorY += 112.0f;

            DrawSectionHeader(drawList, fontManager, "ACCESS", inspectorRowX, inspectorY, inspectorRowW);
            inspectorY += 32.0f;
            drawList.AddRectFilled(inspectorRowX, inspectorY, inspectorRowW, 48.0f, 0xFF101010);
            uiCtx.Checkbox("Expose In EventGraph", selectedComponent->exposeToGraph, inspectorRowX + 10.0f, inspectorY + 10.0f, 16.0f);
            inspectorY += 60.0f;

            if (selectedComponent->kind == ComponentKind::StaticMesh || selectedComponent->kind == ComponentKind::SkeletalMesh) {
                DrawSectionHeader(drawList, fontManager, "MESH ASSET", inspectorRowX, inspectorY, inspectorRowW);
                inspectorY += 32.0f;
                drawList.AddRectFilled(inspectorRowX, inspectorY, inspectorRowW, 74.0f, 0xFF101010);
                drawList.AddText(fontManager,
                                 FitTextToWidth(fontManager,
                                                selectedComponent->assetPath.empty() ? "No .catalystactor assigned yet."
                                                                                     : fs::path(selectedComponent->assetPath).filename().string(),
                                                inspectorRowW - 20.0f),
                                 inspectorRowX + 10.0f, inspectorY + 22.0f, 0xFFFFFFFF);
                drawList.AddText(fontManager,
                                 FitTextToWidth(fontManager,
                                                selectedComponent->kind == ComponentKind::SkeletalMesh
                                                    ? "Skeletal meshes currently use the actor asset renderer without animation playback."
                                                    : "Static mesh components use the first assigned actor asset as the owning Blueprint preview mesh.",
                                                inspectorRowW - 20.0f),
                                 inspectorRowX + 10.0f, inspectorY + 48.0f, 0xFF9098A2);
                inspectorY += 86.0f;
                if (uiCtx.Button("Browse Actor Asset", inspectorRowX, inspectorY, 146.0f, 24.0f,
                                 0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
                    const std::wstring selectedAssetPath = BrowseForActorAssetFile(windowHandle != nullptr ? windowHandle : GetActiveWindow());
                    if (!selectedAssetPath.empty()) {
                        selectedComponent->assetPath = NormalizeAssetPath(selectedAssetPath);
                        SetStatus("Assigned " + selectedComponent->name + " to " + fs::path(selectedComponent->assetPath).filename().string() + ".",
                                  0xFF89D185, 2600);
                    }
                }
                if (uiCtx.Button("Clear Asset", inspectorRowX + 154.0f, inspectorY, 94.0f, 24.0f,
                                 0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
                    selectedComponent->assetPath.clear();
                    SetStatus("Cleared the asset on " + selectedComponent->name + ".", 0xFFB7BEC8, 2200);
                }
                inspectorY += 36.0f;
            } else if (selectedComponent->kind == ComponentKind::Camera) {
                DrawSectionHeader(drawList, fontManager, "CAMERA", inspectorRowX, inspectorY, inspectorRowW);
                inspectorY += 32.0f;
                drawList.AddRectFilled(inspectorRowX, inspectorY, inspectorRowW, 132.0f, 0xFF101010);
                if (uiCtx.Checkbox("Possess On Play", selectedComponent->possessOnPlay, inspectorRowX + 10.0f, inspectorY + 10.0f, 16.0f) &&
                    selectedComponent->possessOnPlay) {
                    for (BlueprintComponent& component : m_components) {
                        if (component.id != selectedComponent->id && component.kind == ComponentKind::Camera) {
                            component.possessOnPlay = false;
                        }
                    }
                    SetStatus(selectedComponent->name + " now owns play camera possession.", 0xFF89D185, 2400);
                }
                uiCtx.DragFloat("Cam Offset X", selectedComponent->location.x, 0.02f, inspectorRowX + 10.0f, inspectorY + 38.0f, inspectorRowW - 20.0f, 24.0f);
                inspectorY += 88.0f;
                uiCtx.DragFloat("Cam Offset Y", selectedComponent->location.y, 0.02f, inspectorRowX, inspectorY, inspectorRowW, 24.0f);
                inspectorY += 28.0f;
                uiCtx.DragFloat("Cam Offset Z", selectedComponent->location.z, 0.02f, inspectorRowX, inspectorY, inspectorRowW, 24.0f);
                inspectorY += 36.0f;
            } else if (selectedComponent->kind == ComponentKind::Trigger) {
                DrawSectionHeader(drawList, fontManager, "TRIGGER", inspectorRowX, inspectorY, inspectorRowW);
                inspectorY += 32.0f;
                drawList.AddRectFilled(inspectorRowX, inspectorY, inspectorRowW, 52.0f, 0xFF101010);
                drawList.AddText(fontManager, "Shape: " + std::string(PhysicsColliderShapeToString(selectedComponent->triggerShape)),
                                 inspectorRowX + 10.0f, inspectorY + 22.0f, 0xFFFFFFFF);
                if (uiCtx.Button("Toggle Shape", inspectorRowX + inspectorRowW - 112.0f, inspectorY + 10.0f, 102.0f, 24.0f,
                                 0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
                    selectedComponent->triggerShape = selectedComponent->triggerShape == PhysicsColliderShape::Box
                        ? PhysicsColliderShape::Sphere
                        : PhysicsColliderShape::Box;
                }
                inspectorY += 64.0f;
                uiCtx.DragFloat("Trig Offset X", selectedComponent->location.x, 0.02f, inspectorRowX, inspectorY, inspectorRowW, 24.0f);
                inspectorY += 28.0f;
                uiCtx.DragFloat("Trig Offset Y", selectedComponent->location.y, 0.02f, inspectorRowX, inspectorY, inspectorRowW, 24.0f);
                inspectorY += 28.0f;
                uiCtx.DragFloat("Trig Offset Z", selectedComponent->location.z, 0.02f, inspectorRowX, inspectorY, inspectorRowW, 24.0f);
                inspectorY += 36.0f;
                if (selectedComponent->triggerShape == PhysicsColliderShape::Box) {
                    uiCtx.DragFloat("Extent X", selectedComponent->triggerExtents.x, 0.02f, inspectorRowX, inspectorY, inspectorRowW, 24.0f);
                    inspectorY += 28.0f;
                    uiCtx.DragFloat("Extent Y", selectedComponent->triggerExtents.y, 0.02f, inspectorRowX, inspectorY, inspectorRowW, 24.0f);
                    inspectorY += 28.0f;
                    uiCtx.DragFloat("Extent Z", selectedComponent->triggerExtents.z, 0.02f, inspectorRowX, inspectorY, inspectorRowW, 24.0f);
                    inspectorY += 36.0f;
                } else {
                    uiCtx.DragFloat("Radius", selectedComponent->triggerRadius, 0.02f, inspectorRowX, inspectorY, inspectorRowW, 24.0f);
                    inspectorY += 36.0f;
                }
            }

            if (uiCtx.Button("Delete Component", inspectorRowX, inspectorY, 132.0f, 24.0f,
                             0xFF432323, 0xFF5A2D2D, 0xFF311818)) {
                const std::string removedName = selectedComponent->name;
                const int removedId = selectedComponent->id;
                RemoveComponent(removedId);
                SetStatus("Deleted " + removedName + ".", 0xFFB7BEC8, 2200);
            }
            inspectorY += 36.0f;
            }
        }
    } else {
        const Node* selectedNode = FindNode(m_selectedNodeId);
        if (selectedNode == nullptr) {
            const std::string selectionHint = "Select a node to inspect inputs, outputs, comments, and saved metadata.";
            const float selectionHintH = MeasureInfoBoxHeight(selectionHint, inspectorRowW - 24.0f, 88.0f);
            DrawSectionHeader(drawList, fontManager, "SELECTION", inspectorRowX, inspectorY, inspectorRowW);
            inspectorY += 32.0f;
            drawList.AddRectFilled(inspectorRowX, inspectorY, inspectorRowW, selectionHintH, 0xFF101010);
            drawList.AddText(fontManager, FitTextToWidth(fontManager, selectionHint, inspectorRowW - 24.0f),
                             inspectorRowX + 12.0f, inspectorY + 26.0f, 0xFFD3D7DD);
            inspectorY += selectionHintH + 16.0f;
        } else {
            DrawSectionHeader(drawList, fontManager, "SELECTION", inspectorRowX, inspectorY, inspectorRowW);
            inspectorY += 32.0f;
            drawList.AddRectFilled(inspectorRowX, inspectorY, inspectorRowW, 96.0f, 0xFF101010);
            drawList.AddRectFilled(inspectorRowX, inspectorY, inspectorRowW, 24.0f,
                                   selectedNode->visual == NodeVisualKind::UIElement
                                       ? UIElementAccentColor(selectedNode->uiElementKind)
                                       : NodeAccentColor(selectedNode->visual));
            drawList.AddText(fontManager, FitTextToWidth(fontManager, selectedNode->title, inspectorRowW - 24.0f),
                             inspectorRowX + 10.0f, inspectorY + 16.0f, 0xFFFFFFFF);
            drawList.AddText(fontManager, FitTextToWidth(fontManager, selectedNode->subtitle, inspectorRowW - 20.0f),
                             inspectorRowX + 10.0f, inspectorY + 44.0f, 0xFFCBCBCB);
            drawList.AddText(fontManager, "Node Category: " + BuildVisualTypeLabel(selectedNode->visual),
                             inspectorRowX + 10.0f, inspectorY + 68.0f, 0xFF8E99A5);
            inspectorY += 112.0f;

            DrawSectionHeader(drawList, fontManager, "NODE SETTINGS", inspectorRowX, inspectorY, inspectorRowW);
            inspectorY += 32.0f;
            drawList.AddRectFilled(inspectorRowX, inspectorY, inspectorRowW, 58.0f, 0xFF101010);
            drawList.AddText(fontManager, BuildVisualTypeLabel(selectedNode->visual) + " Node",
                             inspectorRowX + 10.0f, inspectorY + 22.0f, 0xFFFFFFFF);
            drawList.AddText(fontManager,
                             std::string("Size: ") + std::to_string(static_cast<int>(selectedNode->width)) + " x " +
                                 std::to_string(static_cast<int>(selectedNode->height)),
                             inspectorRowX + 10.0f, inspectorY + 46.0f, 0xFF9FA8B3);
            inspectorY += 72.0f;

            if (selectedNode->visual == NodeVisualKind::Field) {
                DrawSectionHeader(drawList, fontManager, "VARIABLE", inspectorRowX, inspectorY, inspectorRowW);
                inspectorY += 32.0f;
                drawList.AddRectFilled(inspectorRowX, inspectorY, inspectorRowW, 74.0f, 0xFF101010);
                const std::string fieldName = !selectedNode->fieldName.empty()
                    ? selectedNode->fieldName
                    : (selectedNode->field != nullptr ? selectedNode->field->name : "Unbound Field");
                const std::string fieldCategory = !selectedNode->fieldCategory.empty()
                    ? selectedNode->fieldCategory
                    : (selectedNode->field != nullptr ? selectedNode->field->category : "Unknown");
                drawList.AddText(fontManager, fieldName, inspectorRowX + 10.0f, inspectorY + 22.0f, 0xFFFFFFFF);
                if (selectedNode->field != nullptr) {
                    drawList.AddText(fontManager,
                                     std::string(ShortCategoryName(fieldCategory.c_str())) + "  |  " +
                                         BuildFieldTypeLabel(selectedNode->field->type),
                                     inspectorRowX + 10.0f, inspectorY + 46.0f, FieldTypeColor(selectedNode->field->type),
                                     inspectorRowW - 20.0f);
                } else {
                    drawList.AddText(fontManager,
                                     std::string(ShortCategoryName(fieldCategory.c_str())) + "  |  Missing descriptor",
                                     inspectorRowX + 10.0f, inspectorY + 46.0f, 0xFFE0C36F, inspectorRowW - 20.0f);
                }
                inspectorY += 88.0f;

                DrawSectionHeader(drawList, fontManager, "DEFAULT VALUE", inspectorRowX, inspectorY, inspectorRowW);
                inspectorY += 32.0f;
                drawList.AddRectFilled(inspectorRowX, inspectorY, inspectorRowW, 54.0f, 0xFF101010);
                drawList.AddText(fontManager,
                                 FitTextToWidth(fontManager,
                                                selectedNode->field != nullptr ? BuildFieldPreview(selectedObject, *selectedNode->field)
                                                                               : "Stored field binding no longer matches a live descriptor.",
                                                inspectorRowW - 20.0f),
                                 inspectorRowX + 10.0f, inspectorY + 22.0f,
                                 selectedNode->field != nullptr ? 0xFFFFFFFF : 0xFFE0C36F);
                inspectorY += 68.0f;
            } else if (selectedNode->visual == NodeVisualKind::Comment) {
                DrawSectionHeader(drawList, fontManager, "COMMENT", inspectorRowX, inspectorY, inspectorRowW);
                inspectorY += 32.0f;
                drawList.AddRectFilled(inspectorRowX, inspectorY, inspectorRowW, 72.0f, 0xFF101010);
                drawList.AddText(fontManager, "Comment boxes help organize sections of the EventGraph.",
                                 inspectorRowX + 10.0f, inspectorY + 24.0f, 0xFFB6B6B6, inspectorRowW - 20.0f);
                drawList.AddText(fontManager, selectedNode->canDelete ? "Delete removes this box." : "This comment is locked.",
                                 inspectorRowX + 10.0f, inspectorY + 52.0f, 0xFF9FA8B3, inspectorRowW - 20.0f);
                inspectorY += 86.0f;
            } else if (selectedNode->visual == NodeVisualKind::UIElement) {
                Node* editableNode = FindNode(selectedNode->id);
                if (editableNode != nullptr) {
                    DrawSectionHeader(drawList, fontManager, "UI ELEMENT", inspectorRowX, inspectorY, inspectorRowW);
                    inspectorY += 32.0f;
                    drawList.AddRectFilled(inspectorRowX, inspectorY, inspectorRowW, 70.0f, 0xFF101010);
                    drawList.AddText(fontManager, editableNode->title, inspectorRowX + 10.0f, inspectorY + 22.0f, 0xFFFFFFFF);
                    drawList.AddText(fontManager,
                                     BuildUIElementTypeLabel(editableNode->uiElementKind) + "  |  Output pins: " +
                                         (editableNode->uiElementKind == UIElementKind::Button ? "OnClicked + Self" : "Self"),
                                     inspectorRowX + 10.0f, inspectorY + 48.0f, 0xFFB5BEC8, inspectorRowW - 20.0f);
                    inspectorY += 84.0f;

                    DrawSectionHeader(drawList, fontManager, "CONTENT", inspectorRowX, inspectorY, inspectorRowW);
                    inspectorY += 32.0f;
                    drawList.AddRectFilled(inspectorRowX, inspectorY, inspectorRowW, 56.0f, 0xFF101010);
                    DrawInlineTextInput(drawList, fontManager, inputManager, "Element Text",
                                        editableNode->displayText, m_selectedNodeTextInputActive,
                                        inspectorRowX + 10.0f, inspectorY + 12.0f, inspectorRowW - 20.0f, 30.0f,
                                        !interactionsBlocked);
                    inspectorY += 70.0f;

                    DrawSectionHeader(drawList, fontManager, "LAYOUT", inspectorRowX, inspectorY, inspectorRowW);
                    inspectorY += 32.0f;
                    uiCtx.DragFloat("Canvas X", editableNode->canvasX, 0.02f, inspectorRowX, inspectorY, inspectorRowW, 24.0f);
                    inspectorY += 28.0f;
                    uiCtx.DragFloat("Canvas Y", editableNode->canvasY, 0.02f, inspectorRowX, inspectorY, inspectorRowW, 24.0f);
                    inspectorY += 28.0f;
                    uiCtx.DragFloat("Width", editableNode->canvasWidth, 0.02f, inspectorRowX, inspectorY, inspectorRowW, 24.0f);
                    editableNode->canvasWidth = (std::max)(28.0f, editableNode->canvasWidth);
                    inspectorY += 28.0f;
                    uiCtx.DragFloat("Height", editableNode->canvasHeight, 0.02f, inspectorRowX, inspectorY, inspectorRowW, 24.0f);
                    editableNode->canvasHeight = (std::max)(20.0f, editableNode->canvasHeight);
                    inspectorY += 36.0f;

                    DrawSectionHeader(drawList, fontManager, "STYLE", inspectorRowX, inspectorY, inspectorRowW);
                    inspectorY += 32.0f;
                    uiCtx.DragFloat("Tint R", editableNode->tint.x, 0.005f, inspectorRowX, inspectorY, inspectorRowW, 24.0f);
                    editableNode->tint.x = (std::max)(0.0f, (std::min)(1.0f, editableNode->tint.x));
                    inspectorY += 28.0f;
                    uiCtx.DragFloat("Tint G", editableNode->tint.y, 0.005f, inspectorRowX, inspectorY, inspectorRowW, 24.0f);
                    editableNode->tint.y = (std::max)(0.0f, (std::min)(1.0f, editableNode->tint.y));
                    inspectorY += 28.0f;
                    uiCtx.DragFloat("Tint B", editableNode->tint.z, 0.005f, inspectorRowX, inspectorY, inspectorRowW, 24.0f);
                    editableNode->tint.z = (std::max)(0.0f, (std::min)(1.0f, editableNode->tint.z));
                    inspectorY += 28.0f;
                    uiCtx.DragFloat("Tint A", editableNode->tint.w, 0.005f, inspectorRowX, inspectorY, inspectorRowW, 24.0f);
                    editableNode->tint.w = (std::max)(0.0f, (std::min)(1.0f, editableNode->tint.w));
                    inspectorY += 36.0f;

                    if (editableNode->canDelete) {
                        if (uiCtx.Button("Delete Element", inspectorRowX, inspectorY, 128.0f, 24.0f,
                                         0xFF432323, 0xFF5A2D2D, 0xFF311818)) {
                            const int removedNodeId = editableNode->id;
                            const std::string removedTitle = editableNode->title;
                            RemoveNode(removedNodeId);
                            SetStatus("Deleted " + removedTitle + ".", 0xFFB7BEC8, 2200);
                        }
                        inspectorY += 36.0f;
                    }
                }
            } else if (selectedNode->visual == NodeVisualKind::Function &&
                       (IsCreateWidgetNodeType(selectedNode->nodeTypeId) ||
                        IsAddToViewportNodeType(selectedNode->nodeTypeId) ||
                        IsSetTextColorNodeType(selectedNode->nodeTypeId))) {
                DrawSectionHeader(drawList, fontManager, "WIDGET", inspectorRowX, inspectorY, inspectorRowW);
                inspectorY += 32.0f;
                drawList.AddRectFilled(inspectorRowX, inspectorY, inspectorRowW, 72.0f, 0xFF101010);
                const std::string assetLabel = IsSetTextColorNodeType(selectedNode->nodeTypeId)
                    ? "Connect a TextBlock node into the Target data pin."
                    : selectedNode->assetPath.empty()
                    ? "No UI Blueprint asset assigned yet."
                    : fs::path(selectedNode->assetPath).filename().string();
                drawList.AddText(fontManager, FitTextToWidth(fontManager, assetLabel, inspectorRowW - 20.0f),
                                 inspectorRowX + 10.0f, inspectorY + 22.0f, 0xFFFFFFFF);
                drawList.AddText(fontManager,
                                 IsSetTextColorNodeType(selectedNode->nodeTypeId)
                                     ? "When exec fires, this node updates the target TextBlock tint below."
                                     : IsCreateWidgetNodeType(selectedNode->nodeTypeId)
                                     ? "Browse for a UI Blueprint, or drag one from Widget Assets onto this node's Widget pin."
                                     : "Connect the output of Create Widget into this node's Widget input.",
                                 inspectorRowX + 10.0f, inspectorY + 48.0f, 0xFF9FA8B3, inspectorRowW - 20.0f);
                inspectorY += 84.0f;

                if (IsSetTextColorNodeType(selectedNode->nodeTypeId)) {
                    Node* editableNode = FindNode(selectedNode->id);
                    if (editableNode != nullptr) {
                        DrawSectionHeader(drawList, fontManager, "TARGET COLOR", inspectorRowX, inspectorY, inspectorRowW);
                        inspectorY += 32.0f;
                        uiCtx.DragFloat("Color R", editableNode->tint.x, 0.005f, inspectorRowX, inspectorY, inspectorRowW, 24.0f);
                        editableNode->tint.x = (std::max)(0.0f, (std::min)(1.0f, editableNode->tint.x));
                        inspectorY += 28.0f;
                        uiCtx.DragFloat("Color G", editableNode->tint.y, 0.005f, inspectorRowX, inspectorY, inspectorRowW, 24.0f);
                        editableNode->tint.y = (std::max)(0.0f, (std::min)(1.0f, editableNode->tint.y));
                        inspectorY += 28.0f;
                        uiCtx.DragFloat("Color B", editableNode->tint.z, 0.005f, inspectorRowX, inspectorY, inspectorRowW, 24.0f);
                        editableNode->tint.z = (std::max)(0.0f, (std::min)(1.0f, editableNode->tint.z));
                        inspectorY += 28.0f;
                        uiCtx.DragFloat("Color A", editableNode->tint.w, 0.005f, inspectorRowX, inspectorY, inspectorRowW, 24.0f);
                        editableNode->tint.w = (std::max)(0.0f, (std::min)(1.0f, editableNode->tint.w));
                        inspectorY += 36.0f;
                    }
                } else if (IsCreateWidgetNodeType(selectedNode->nodeTypeId)) {
                    if (uiCtx.Button("Browse UI Blueprint", inspectorRowX, inspectorY, 154.0f, 24.0f,
                                     0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
                        const std::wstring selectedAssetPath = BrowseForUIBlueprintFile(windowHandle != nullptr ? windowHandle : GetActiveWindow());
                        if (!selectedAssetPath.empty()) {
                            Node* mutableNode = FindNode(selectedNode->id);
                            if (mutableNode != nullptr) {
                                AssignAssetToNode(*mutableNode, selectedAssetPath);
                            }
                        }
                    }
                    if (uiCtx.Button("Clear Widget", inspectorRowX + 162.0f, inspectorY, 104.0f, 24.0f,
                                     0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
                        Node* mutableNode = FindNode(selectedNode->id);
                        if (mutableNode != nullptr) {
                            mutableNode->assetPath.clear();
                            SetStatus("Cleared the widget asset on " + mutableNode->title + ".", 0xFFB7BEC8, 2200);
                        }
                    }
                    inspectorY += 36.0f;
                }
            }
        }
    }

    DrawBlueprintSummary();

    if (isViewportTab) {
        drawList.AddRectFilled(canvasX, canvasY, canvasW, canvasH, 0xFF121317);
        drawList.AddRectFilled(canvasX, canvasY, canvasW, 24.0f, 0xFF101114);
        drawList.AddText(fontManager, isUIAsset ? "Blueprint / Designer" : "Blueprint / Viewport",
                         canvasX + 14.0f, canvasY + 16.0f, 0xFFB8C0CB);
        drawList.AddRectFilled(canvasX + 28.0f, canvasY + 54.0f, canvasW - 56.0f, canvasH - 90.0f, 0xFF17181C);
        drawList.AddRectFilled(canvasX + 28.0f, canvasY + 54.0f, canvasW - 56.0f, 4.0f, isUIAsset ? 0xFF86D7C7 : 0xFF7A9BD4);
        if (isUIAsset) {
            const float designerX = canvasX + 48.0f;
            const float designerY = canvasY + 84.0f;
            const float designerW = canvasW - 96.0f;
            const float designerH = canvasH - 138.0f;
            drawList.AddRectFilled(designerX, designerY, designerW, designerH, 0xFF20242B);
            drawList.AddRectFilled(designerX, designerY, designerW, 4.0f, 0xFF86D7C7);
            drawList.AddText(fontManager, "Designer Surface", designerX + 14.0f, designerY + 22.0f, 0xFFFFFFFF);
            drawList.AddText(fontManager,
                             "The runtime layout pass reads UI Element nodes from this Blueprint and draws them using CanvasPosition and Size.",
                             designerX + 14.0f, designerY + 46.0f, 0xFFBEC6D0, designerW - 28.0f);
            drawList.AddText(fontManager,
                             "Drag elements to move. Drag the lower-right handle to resize. Press Delete to remove the selected element.",
                             designerX + 14.0f, designerY + 68.0f, 0xFF98A2AE, designerW - 28.0f);

            const float designerMouseX = static_cast<float>(inputManager.GetMouseX());
            const float designerMouseY = static_cast<float>(inputManager.GetMouseY());
            auto GetDesignerElementRect = [&](const Node& node, float& outX, float& outY, float& outW, float& outH) {
                outX = designerX + node.canvasX;
                outY = designerY + node.canvasY;
                outW = (std::max)(20.0f, node.canvasWidth);
                outH = (std::max)(20.0f, node.canvasHeight);
            };
            auto HitTestDesignerElement = [&](bool includeResizeHandle, bool* outResizeHandle) -> Node* {
                if (outResizeHandle != nullptr) {
                    *outResizeHandle = false;
                }

                for (auto it = m_nodes.rbegin(); it != m_nodes.rend(); ++it) {
                    if (it->visual != NodeVisualKind::UIElement) {
                        continue;
                    }

                    float elementX = 0.0f;
                    float elementY = 0.0f;
                    float elementW = 0.0f;
                    float elementH = 0.0f;
                    GetDesignerElementRect(*it, elementX, elementY, elementW, elementH);

                    const float resizeHandleSize = 12.0f;
                    if (includeResizeHandle &&
                        it->id == m_selectedNodeId &&
                        IsPointInRect(designerMouseX, designerMouseY,
                                      elementX + elementW - resizeHandleSize, elementY + elementH - resizeHandleSize,
                                      resizeHandleSize, resizeHandleSize)) {
                        if (outResizeHandle != nullptr) {
                            *outResizeHandle = true;
                        }
                        return &(*it);
                    }

                    if (IsPointInRect(designerMouseX, designerMouseY, elementX, elementY, elementW, elementH)) {
                        return &(*it);
                    }
                }

                return nullptr;
            };

            if (!interactionsBlocked) {
                if (inputManager.IsMouseButtonPressed(0)) {
                    bool hitResizeHandle = false;
                    Node* hitElement = HitTestDesignerElement(true, &hitResizeHandle);
                    if (hitElement != nullptr) {
                        m_selectedNodeId = hitElement->id;
                        m_selectedComponentId = -1;
                        m_draggingDesignerNodeId = hitElement->id;
                        m_isResizingDesignerNode = hitResizeHandle;
                        m_lastMouseX = inputManager.GetMouseX();
                        m_lastMouseY = inputManager.GetMouseY();
                    } else if (IsPointInRect(designerMouseX, designerMouseY, designerX, designerY, designerW, designerH)) {
                        m_selectedNodeId = -1;
                        m_draggingDesignerNodeId = -1;
                        m_isResizingDesignerNode = false;
                    }
                }

                if (m_draggingDesignerNodeId != -1) {
                    if (!inputManager.IsMouseButtonDown(0)) {
                        m_draggingDesignerNodeId = -1;
                        m_isResizingDesignerNode = false;
                    } else {
                        Node* activeElement = FindNode(m_draggingDesignerNodeId);
                        if (activeElement != nullptr && activeElement->visual == NodeVisualKind::UIElement) {
                            const float dx = static_cast<float>(inputManager.GetMouseX() - m_lastMouseX);
                            const float dy = static_cast<float>(inputManager.GetMouseY() - m_lastMouseY);

                            if (m_isResizingDesignerNode) {
                                activeElement->canvasWidth = (std::max)(20.0f, activeElement->canvasWidth + dx);
                                activeElement->canvasHeight = (std::max)(20.0f, activeElement->canvasHeight + dy);
                                activeElement->canvasWidth = (std::min)(activeElement->canvasWidth,
                                                                        (std::max)(20.0f, designerW - activeElement->canvasX - 4.0f));
                                activeElement->canvasHeight = (std::min)(activeElement->canvasHeight,
                                                                         (std::max)(20.0f, designerH - activeElement->canvasY - 4.0f));
                            } else {
                                const float maxCanvasX = (std::max)(0.0f, designerW - activeElement->canvasWidth);
                                const float maxCanvasY = (std::max)(0.0f, designerH - activeElement->canvasHeight);
                                activeElement->canvasX = (std::min)((std::max)(0.0f, activeElement->canvasX + dx),
                                                                    maxCanvasX);
                                activeElement->canvasY = (std::min)((std::max)(0.0f, activeElement->canvasY + dy),
                                                                    maxCanvasY);
                            }

                            m_lastMouseX = inputManager.GetMouseX();
                            m_lastMouseY = inputManager.GetMouseY();
                        }
                    }
                }
            } else {
                m_draggingDesignerNodeId = -1;
                m_isResizingDesignerNode = false;
            }

            const bool deleteIsDown = (GetAsyncKeyState(VK_DELETE) & 0x8000) != 0;
            if (!interactionsBlocked && deleteIsDown && !m_deleteWasDown && m_selectedNodeId != -1) {
                Node* selectedDesignerNode = FindNode(m_selectedNodeId);
                if (selectedDesignerNode != nullptr && selectedDesignerNode->visual == NodeVisualKind::UIElement) {
                    if (selectedDesignerNode->canDelete) {
                        const std::string removedTitle = selectedDesignerNode->title;
                        RemoveNode(selectedDesignerNode->id);
                        m_draggingDesignerNodeId = -1;
                        m_isResizingDesignerNode = false;
                        SetStatus("Deleted " + removedTitle + ".", 0xFFB7BEC8, 2200);
                    } else {
                        SetStatus("That UI element is locked and cannot be deleted.", 0xFFE0C36F, 2400);
                    }
                }
            }
            m_deleteWasDown = deleteIsDown;

            auto DrawDesignerElement = [&](const Node& node) {
                float elementX = 0.0f;
                float elementY = 0.0f;
                float elementW = 0.0f;
                float elementH = 0.0f;
                GetDesignerElementRect(node, elementX, elementY, elementW, elementH);
                const uint32_t tintColor = Float4ToUIntColor(node.tint);
                const bool isSelected = node.id == m_selectedNodeId;

                if (node.uiElementKind == UIElementKind::Canvas) {
                    drawList.AddRectFilled(elementX, elementY, elementW, elementH, tintColor);
                    drawList.AddText(fontManager, FitTextToWidth(fontManager, node.displayText, elementW - 20.0f),
                                     elementX + 10.0f, elementY + 20.0f, 0xFFFFFFFF, elementW - 20.0f);
                } else if (node.uiElementKind == UIElementKind::Button) {
                    drawList.AddRectFilled(elementX, elementY, elementW, elementH, tintColor);
                    drawList.AddRectFilled(elementX, elementY, elementW, 3.0f, 0xFFFFFFFF);
                    drawList.AddText(fontManager, FitTextToWidth(fontManager, node.displayText, elementW - 20.0f),
                                     elementX + 10.0f, elementY + elementH * 0.5f, 0xFFFFFFFF, elementW - 20.0f);
                } else if (node.uiElementKind == UIElementKind::TextBlock) {
                    drawList.AddText(fontManager, node.displayText, elementX + 4.0f, elementY + 20.0f, tintColor, elementW - 8.0f);
                } else if (node.uiElementKind == UIElementKind::Image) {
                    drawList.AddRectFilled(elementX, elementY, elementW, elementH, tintColor);
                    drawList.AddText(fontManager, "Image", elementX + 10.0f, elementY + 22.0f, 0xFFFFFFFF, elementW - 20.0f);
                }

                if (isSelected) {
                    drawList.AddRectFilled(elementX - 2.0f, elementY - 2.0f, elementW + 4.0f, 2.0f, 0xFFD77800);
                    drawList.AddRectFilled(elementX - 2.0f, elementY + elementH, elementW + 4.0f, 2.0f, 0xFFD77800);
                    drawList.AddRectFilled(elementX - 2.0f, elementY - 2.0f, 2.0f, elementH + 4.0f, 0xFFD77800);
                    drawList.AddRectFilled(elementX + elementW, elementY - 2.0f, 2.0f, elementH + 4.0f, 0xFFD77800);
                    drawList.AddRectFilled(elementX + elementW - 12.0f, elementY + elementH - 12.0f, 12.0f, 12.0f, 0xFFD77800);
                    drawList.AddRectFilled(elementX + elementW - 9.0f, elementY + elementH - 9.0f, 6.0f, 6.0f, 0xFF101114);
                }
            };

            for (const Node& node : m_nodes) {
                if (node.visual == NodeVisualKind::UIElement && node.uiElementKind == UIElementKind::Canvas) {
                    DrawDesignerElement(node);
                }
            }
            for (const Node& node : m_nodes) {
                if (node.visual == NodeVisualKind::UIElement && node.uiElementKind != UIElementKind::Canvas) {
                    DrawDesignerElement(node);
                }
            }
        } else {
            drawList.AddText(fontManager, "DefaultSceneRoot", canvasX + 48.0f, canvasY + 88.0f, 0xFFFFFFFF);
            drawList.AddText(fontManager, "Blueprint Components", canvasX + 48.0f, canvasY + 114.0f, 0xFFBEC6D0);
            float previewY = canvasY + 154.0f;
            if (m_components.empty()) {
                drawList.AddText(fontManager, "No components yet. Add a static mesh, skeletal mesh, camera, or trigger from the toolbar above.",
                                 canvasX + 48.0f, previewY, 0xFFC0C8D2, canvasW - 96.0f);
            } else {
                for (const BlueprintComponent& component : m_components) {
                    const bool selected = component.id == m_selectedComponentId;
                    drawList.AddRectFilled(canvasX + 48.0f, previewY, canvasW - 96.0f, 50.0f, selected ? 0xFF1C2633 : 0xFF1E2025);
                    drawList.AddRectFilled(canvasX + 48.0f, previewY, 4.0f, 50.0f, ComponentAccentColor(component.kind));
                    drawList.AddText(fontManager, FitTextToWidth(fontManager, component.name, canvasW - 140.0f),
                                     canvasX + 60.0f, previewY + 18.0f, 0xFFFFFFFF, canvasW - 140.0f);
                    drawList.AddText(fontManager, BuildComponentSummary(component),
                                     canvasX + 60.0f, previewY + 38.0f, 0xFFBEC6D0, canvasW - 140.0f);
                    previewY += 56.0f;
                }
            }
        }
    } else if (isConstructionTab) {
        drawList.AddRectFilled(canvasX, canvasY, canvasW, canvasH, 0xFF121317);
        drawList.AddRectFilled(canvasX, canvasY, canvasW, 24.0f, 0xFF101114);
        drawList.AddText(fontManager, isUIAsset ? "Blueprint / Widget Tree" : "Blueprint / Construction Script",
                         canvasX + 14.0f, canvasY + 16.0f, 0xFFB8C0CB);
        drawList.AddRectFilled(canvasX + 38.0f, canvasY + 74.0f, canvasW - 76.0f, 138.0f, 0xFF17181C);
        drawList.AddRectFilled(canvasX + 38.0f, canvasY + 74.0f, canvasW - 76.0f, 4.0f, 0xFFD77800);
        if (isUIAsset) {
            drawList.AddText(fontManager, "Widget Tree", canvasX + 56.0f, canvasY + 116.0f, 0xFFFFFFFF);
            drawList.AddText(fontManager,
                             "UI elements are listed in authoring order. Button nodes expose OnClicked in the Graph tab.",
                             canvasX + 56.0f, canvasY + 146.0f, 0xFF9DA6B1, canvasW - 112.0f);
            float treeY = canvasY + 226.0f;
            for (const Node& node : m_nodes) {
                if (node.visual != NodeVisualKind::UIElement) {
                    continue;
                }
                const bool isSelected = node.id == m_selectedNodeId;
                drawList.AddRectFilled(canvasX + 56.0f, treeY, canvasW - 112.0f, 38.0f, isSelected ? 0xFF1C2633 : 0xFF1E2025);
                drawList.AddRectFilled(canvasX + 56.0f, treeY, 4.0f, 38.0f, UIElementAccentColor(node.uiElementKind));
                drawList.AddText(fontManager, node.title, canvasX + 68.0f, treeY + 18.0f, 0xFFFFFFFF, canvasW - 144.0f);
                drawList.AddText(fontManager, BuildUIElementTypeLabel(node.uiElementKind), canvasX + 220.0f, treeY + 18.0f, 0xFFBEC6D0, canvasW - 280.0f);
                if (!interactionsBlocked &&
                    inputManager.IsMouseButtonPressed(0) &&
                    IsPointInRect(static_cast<float>(inputManager.GetMouseX()), static_cast<float>(inputManager.GetMouseY()),
                                  canvasX + 56.0f, treeY, canvasW - 112.0f, 38.0f)) {
                    m_selectedNodeId = node.id;
                }
                treeY += 44.0f;
            }
        } else {
            drawList.AddText(fontManager, "Construction Script Scaffold",
                             canvasX + 56.0f, canvasY + 116.0f, 0xFFFFFFFF);
            drawList.AddText(fontManager,
                             "Use Viewport to add components and EventGraph to reference them. This document tab is reserved for component setup flow.",
                             canvasX + 56.0f, canvasY + 146.0f, 0xFF9DA6B1, canvasW - 112.0f);
        }
    } else {
        drawList.AddRectFilled(canvasX, canvasY, canvasW, canvasH, 0xFF121317);
        drawList.AddRectFilled(canvasX, canvasY, canvasW, 24.0f, 0xFF101114);
        drawList.AddText(fontManager, isUIAsset ? "Blueprint / Graph" : "Blueprint / EventGraph",
                         canvasX + 14.0f, canvasY + 16.0f, 0xFFB8C0CB);
        drawList.PushClipRect(canvasX, canvasY + 24.0f, canvasW, canvasH - 24.0f);

        float gridSpacing = 48.0f * m_zoom;
        while (gridSpacing < 24.0f) {
            gridSpacing *= 2.0f;
        }
        const float gridOffsetX = std::fmod(m_pan.x, gridSpacing);
        const float gridOffsetY = std::fmod(m_pan.y, gridSpacing);
        for (float x = gridOffsetX; x < canvasW; x += gridSpacing) {
            drawList.AddLine(canvasX + x, canvasY + 34.0f, canvasX + x, canvasY + canvasH, 1.0f, 0xFF1C1C1F);
        }
        for (float y = 34.0f + gridOffsetY; y < canvasH; y += gridSpacing) {
            drawList.AddLine(canvasX, canvasY + y, canvasX + canvasW, canvasY + y, 1.0f, 0xFF1C1C1F);
        }

    auto DrawGraphNode = [&](const Node& node) {
        const float nodeX = canvasX + m_pan.x + node.x * m_zoom;
        const float nodeY = canvasY + m_pan.y + node.y * m_zoom;
        const float nodeW = node.width * m_zoom;
        const float nodeH = node.height * m_zoom;
        const bool isSelected = node.id == m_selectedNodeId;
        const uint32_t accentColor = node.visual == NodeVisualKind::Component
            ? ComponentAccentColor(node.componentKind)
            : node.visual == NodeVisualKind::UIElement
            ? UIElementAccentColor(node.uiElementKind)
            : NodeAccentColor(node.visual);
        const float headerH = (std::max)(22.0f, 26.0f * m_zoom);

        if (node.visual == NodeVisualKind::Comment) {
            drawList.AddRectFilled(nodeX, nodeY, nodeW, nodeH, isSelected ? 0x223C5A80 : 0x18262931);
            drawList.AddRectFilled(nodeX, nodeY, nodeW, 24.0f, accentColor);
            drawList.AddRectFilled(nodeX, nodeY, nodeW, 2.0f, isSelected ? 0xFFD7B24D : 0xFF4C5561);
            drawList.AddRectFilled(nodeX, nodeY + nodeH - 2.0f, nodeW, 2.0f, isSelected ? 0xFFD7B24D : 0xFF4C5561);
            drawList.AddRectFilled(nodeX, nodeY, 2.0f, nodeH, isSelected ? 0xFFD7B24D : 0xFF4C5561);
            drawList.AddRectFilled(nodeX + nodeW - 2.0f, nodeY, 2.0f, nodeH, isSelected ? 0xFFD7B24D : 0xFF4C5561);
            drawList.AddText(fontManager, FitTextToWidth(fontManager, node.title, nodeW - 18.0f),
                             nodeX + 10.0f, nodeY + 16.0f, 0xFFFFFFFF);
            drawList.AddText(fontManager, "Comment Box", nodeX + 10.0f, nodeY + 42.0f, 0xFFC3C7CE);
            return;
        }

        drawList.AddRectFilled(nodeX + 4.0f, nodeY + 4.0f, nodeW, nodeH, 0x66000000);
        drawList.AddRectFilled(nodeX - 2.0f, nodeY - 2.0f, nodeW + 4.0f, nodeH + 4.0f,
                               isSelected ? 0xFFD77800 : 0xFF0D0E10);
        drawList.AddRectFilled(nodeX, nodeY, nodeW, nodeH, 0xFF1E2025);
        drawList.AddRectFilled(nodeX, nodeY, nodeW, headerH, accentColor);
        drawList.AddRectFilled(nodeX + 8.0f, nodeY + 8.0f, 10.0f, 10.0f, 0x22000000);
        drawList.AddText(fontManager, FitTextToWidth(fontManager, node.title, nodeW - 30.0f),
                         nodeX + 24.0f, nodeY + 17.0f, 0xFFFFFFFF);
        drawList.AddText(fontManager, FitTextToWidth(fontManager, node.subtitle, nodeW - 20.0f),
                         nodeX + 10.0f, nodeY + headerH + 18.0f, 0xFFCCCCCC);

        if (node.visual == NodeVisualKind::Event) {
            DrawPinRow(drawList, fontManager, "Then", nodeX + 8.0f * m_zoom, nodeY + headerH + 42.0f, nodeW - 16.0f * m_zoom, true, 0xFFEFEFEF, m_zoom);
            drawList.AddText(fontManager,
                             IsWidgetConstructNodeType(node.nodeTypeId) ? "Widget lifetime entry node" : "Event entry node",
                             nodeX + 10.0f, nodeY + headerH + 66.0f, 0xFF9FA8B3);
            return;
        }

        if (node.visual == NodeVisualKind::Function) {
            const std::string inputLabel = IsCreateWidgetNodeType(node.nodeTypeId) || IsAddToViewportNodeType(node.nodeTypeId)
                ? "Widget"
                : IsSetTextColorNodeType(node.nodeTypeId)
                ? "Target"
                : "Target";
            const std::string outputLabel = IsCreateWidgetNodeType(node.nodeTypeId)
                ? "Created"
                : IsAddToViewportNodeType(node.nodeTypeId)
                ? "Viewport"
                : IsSetTextColorNodeType(node.nodeTypeId)
                ? "Updated"
                : "Updated";
            DrawPinRow(drawList, fontManager, "Exec", nodeX + 8.0f * m_zoom, nodeY + headerH + 40.0f, nodeW - 16.0f * m_zoom, false, 0xFFEFEFEF, m_zoom);
            DrawPinRow(drawList, fontManager, "Then", nodeX + 8.0f * m_zoom, nodeY + headerH + 40.0f, nodeW - 16.0f * m_zoom, true, 0xFFEFEFEF, m_zoom);
            DrawPinRow(drawList, fontManager, inputLabel, nodeX + 8.0f * m_zoom, nodeY + headerH + 64.0f, nodeW - 16.0f * m_zoom, false, 0xFF5A86D6, m_zoom);
            DrawPinRow(drawList, fontManager, outputLabel, nodeX + 8.0f * m_zoom, nodeY + headerH + 64.0f, nodeW - 16.0f * m_zoom, true, 0xFF5A86D6, m_zoom);
            const std::string functionSummary = node.nodeTypeId == BlueprintNodes::kPlayerCharacterControllerNodeId
                ? "Turns the owning Blueprint into a playable WASD character in Play mode."
                : node.nodeTypeId == BlueprintNodes::kPlayerSpaceJumpNodeId
                ? "Lets the Player Character jump when Space is pressed in Play mode."
                : IsCreateWidgetNodeType(node.nodeTypeId)
                ? (node.assetPath.empty()
                       ? "Create a UI widget, then drag a UI Blueprint asset onto the Widget pin."
                       : ("Creates " + fs::path(node.assetPath).stem().string() + " so it can be added to the viewport."))
                : IsAddToViewportNodeType(node.nodeTypeId)
                ? "Adds the created widget instance to the play viewport overlay."
                : IsSetTextColorNodeType(node.nodeTypeId)
                ? "Changes a TextBlock node's runtime tint when its exec pin is triggered."
                : node.title == "Refresh Preview Data"
                ? "Refreshes editor-side Blueprint previews."
                : "Applies the currently exposed physics field set.";
            drawList.AddText(fontManager,
                             FitTextToWidth(fontManager, functionSummary, nodeW - 20.0f),
                             nodeX + 10.0f, nodeY + headerH + 92.0f, 0xFF9FA8B3);
            return;
        }

        if (node.visual == NodeVisualKind::UIElement) {
            const uint32_t elementColor = UIElementAccentColor(node.uiElementKind);
            if (node.uiElementKind == UIElementKind::Button) {
                DrawPinRow(drawList, fontManager, "OnClicked", nodeX + 8.0f * m_zoom, nodeY + headerH + 44.0f,
                           nodeW - 16.0f * m_zoom, true, 0xFFEFEFEF, m_zoom);
            }
            DrawPinRow(drawList, fontManager, "Self", nodeX + 8.0f * m_zoom, nodeY + headerH + 70.0f,
                       nodeW - 16.0f * m_zoom, true, elementColor, m_zoom);
            const std::string sizeLabel =
                std::to_string(static_cast<int>(std::round(node.canvasWidth))) + " x " +
                std::to_string(static_cast<int>(std::round(node.canvasHeight)));
            const std::string uiSummary = node.uiElementKind == UIElementKind::TextBlock
                ? ("Text: " + node.displayText)
                : node.uiElementKind == UIElementKind::Button
                ? ("Label: " + node.displayText)
                : node.uiElementKind == UIElementKind::Image
                ? "Tinted image slot"
                : "Canvas panel";
            drawList.AddText(fontManager,
                             FitTextToWidth(fontManager, uiSummary, nodeW - 20.0f),
                             nodeX + 10.0f, nodeY + headerH + 96.0f, 0xFFCBD2DC);
            drawList.AddText(fontManager,
                             "Layout: " + sizeLabel,
                             nodeX + 10.0f, nodeY + headerH + 118.0f, 0xFF9FA8B3);
            return;
        }

        if (node.visual == NodeVisualKind::Component) {
            const uint32_t componentColor = ComponentAccentColor(node.componentKind);
            DrawPinRow(drawList, fontManager, "Reference", nodeX + 8.0f * m_zoom, nodeY + headerH + 48.0f,
                       nodeW - 16.0f * m_zoom, true, componentColor, m_zoom);
            drawList.AddText(fontManager, "Blueprint component reference.", nodeX + 10.0f, nodeY + headerH + 78.0f,
                             0xFF9FA8B3);
            return;
        }

        const uint32_t valueColor = node.field != nullptr ? FieldTypeColor(node.field->type) : 0xFF7B7B7B;
        DrawPinRow(drawList, fontManager, "Exec", nodeX + 8.0f * m_zoom, nodeY + headerH + 38.0f, nodeW - 16.0f * m_zoom, false, 0xFFEFEFEF, m_zoom);
        DrawPinRow(drawList, fontManager, "Then", nodeX + 8.0f * m_zoom, nodeY + headerH + 38.0f, nodeW - 16.0f * m_zoom, true, 0xFFEFEFEF, m_zoom);
        DrawPinRow(drawList, fontManager, "Value", nodeX + 8.0f * m_zoom, nodeY + headerH + 62.0f, nodeW - 16.0f * m_zoom, false, valueColor, m_zoom);
        const std::string previewLabel = node.field != nullptr
            ? ("Preview: " + BuildFieldPreview(selectedObject, *node.field))
            : ("Preview: Missing descriptor for " + (!node.fieldName.empty() ? node.fieldName : std::string("field")));
        drawList.AddText(fontManager, FitTextToWidth(fontManager, previewLabel, nodeW - 20.0f),
                         nodeX + 10.0f, nodeY + headerH + 88.0f,
                         node.field != nullptr ? 0xFFACACAC : 0xFFE0C36F);
        drawList.AddRectFilled(nodeX + nodeW - 78.0f, nodeY + nodeH - 22.0f, 68.0f, 14.0f, valueColor);
        drawList.AddText(fontManager, node.field != nullptr ? BuildFieldTypeLabel(node.field->type) : "Field",
                         nodeX + nodeW - 70.0f, nodeY + nodeH - 10.0f, 0xFFFFFFFF);
    };

    auto DrawLinkPath = [&](float startX, float startY, float endX, float endY, float thickness, uint32_t color) {
        const float midX = (startX + endX) * 0.5f;
        drawList.AddLine(startX, startY, midX, startY, thickness, color);
        drawList.AddLine(midX, startY, midX, endY, thickness, color);
        drawList.AddLine(midX, endY, endX, endY, thickness, color);
        const float capSize = (std::max)(4.0f, 6.0f * m_zoom);
        drawList.AddRectFilled(startX - capSize * 0.5f, startY - capSize * 0.5f, capSize, capSize, color);
        drawList.AddRectFilled(endX - capSize * 0.5f, endY - capSize * 0.5f, capSize, capSize, color);
    };

    auto DrawPinHighlight = [&](const PinReference& pin, uint32_t haloColor) {
        if (pin.nodeId < 0) {
            return;
        }

        const Node* node = FindNode(pin.nodeId);
        if (node == nullptr) {
            return;
        }

        float pinX = 0.0f;
        float pinY = 0.0f;
        uint32_t pinColor = 0xFFFFFFFF;
        if (!GetPinScreenPosition(*node, pin.kind, pin.direction, canvasX, canvasY, pinX, pinY, &pinColor)) {
            return;
        }

        const float haloSize = (std::max)(10.0f, 14.0f * m_zoom);
        const float coreSize = haloSize * 0.58f;
        drawList.AddRectFilled(pinX - haloSize * 0.5f, pinY - haloSize * 0.5f, haloSize, haloSize, haloColor);
        drawList.AddRectFilled(pinX - coreSize * 0.5f, pinY - coreSize * 0.5f, coreSize, coreSize, pinColor);
    };

    for (const Node& node : m_nodes) {
        if (node.visual == NodeVisualKind::Comment) {
            DrawGraphNode(node);
        }
    }

    for (const Link& link : m_links) {
        const Node* fromNode = FindNode(link.fromNodeId);
        const Node* toNode = FindNode(link.toNodeId);
        if (fromNode == nullptr || toNode == nullptr) {
            continue;
        }

        float startX = 0.0f;
        float startY = 0.0f;
        float endX = 0.0f;
        float endY = 0.0f;
        if (!GetPinScreenPosition(*fromNode, link.fromPinKind, PinDirection::Output, canvasX, canvasY, startX, startY, nullptr) ||
            !GetPinScreenPosition(*toNode, link.toPinKind, PinDirection::Input, canvasX, canvasY, endX, endY, nullptr)) {
            continue;
        }

        const float thickness = (std::max)(2.0f, 3.0f * m_zoom);
        DrawLinkPath(startX, startY, endX, endY, thickness, link.color);
    }

    if (m_isDraggingLink && m_dragLinkStartPin.nodeId >= 0) {
        const Node* startNode = FindNode(m_dragLinkStartPin.nodeId);
        if (startNode != nullptr) {
            float startX = 0.0f;
            float startY = 0.0f;
            uint32_t linkColor = 0xFFEFEFEF;
            if (GetPinScreenPosition(*startNode, m_dragLinkStartPin.kind, m_dragLinkStartPin.direction,
                                     canvasX, canvasY, startX, startY, &linkColor)) {
                float endX = static_cast<float>(inputManager.GetMouseX());
                float endY = static_cast<float>(inputManager.GetMouseY());

                if (m_dragLinkHoverPin.nodeId >= 0) {
                    const Node* hoverNode = FindNode(m_dragLinkHoverPin.nodeId);
                    if (hoverNode != nullptr) {
                        GetPinScreenPosition(*hoverNode, m_dragLinkHoverPin.kind, m_dragLinkHoverPin.direction,
                                             canvasX, canvasY, endX, endY, nullptr);
                    }
                }

                const float thickness = (std::max)(2.0f, 3.0f * m_zoom);
                DrawLinkPath(startX, startY, endX, endY, thickness, linkColor);
            }
        }
    }

    for (const Node& node : m_nodes) {
        if (node.visual != NodeVisualKind::Comment) {
            DrawGraphNode(node);
        }
    }

    if (m_isDraggingLink) {
        DrawPinHighlight(m_dragLinkStartPin, 0x665D6A7B);
        DrawPinHighlight(m_dragLinkHoverPin, 0x666AA15A);
    }

    if (m_showCreateMenu) {
        std::vector<CreateActionItem> actions = {
            {IsUIAsset() ? "event:widget_construct" : "event:beginplay",
             IsUIAsset() ? "Event Construct" : "Event BeginPlay",
             IsUIAsset() ? "Add a UserWidget construct event" : "Add an EventGraph entry node",
             NodeVisualKind::Event},
            {"event:physics_tick", "Event PhysicsTick", "Add a simulation event node", NodeVisualKind::Event},
            {"function:apply_exposed_physics", "Apply Exposed Physics", "Add the authoring preview function", NodeVisualKind::Function},
            {"function:refresh_preview", "Refresh Preview Data", "Refresh Blueprint preview values in editor", NodeVisualKind::Function},
            {"comment", "Comment", "Add a large comment box", NodeVisualKind::Comment}
        };

        if (IsUIAsset()) {
            actions.erase(std::remove_if(actions.begin(), actions.end(), [](const CreateActionItem& action) {
                return action.id == "event:physics_tick" || action.id == "function:apply_exposed_physics";
            }), actions.end());
            actions.push_back({"function:set_text_color", "Set Text Color", "Change a TextBlock's runtime tint", NodeVisualKind::Function});
        }

        for (const BlueprintNodes::BlueprintNodeTemplate& gameplayNode : BlueprintNodes::GetGameplayNodeTemplates()) {
            actions.push_back({gameplayNode.typeId, gameplayNode.title, gameplayNode.subtitle, NodeVisualKind::Function});
        }

        for (const BlueprintComponent& component : m_components) {
            if (!component.exposeToGraph) {
                continue;
            }
            actions.push_back({"component:" + std::to_string(component.id),
                               component.name,
                               BuildComponentTypeLabel(component.kind) + " reference",
                               NodeVisualKind::Component});
        }

        for (const BlueprintFieldDescriptor& field : GetRigidBodyBlueprintFields()) {
            actions.push_back({"field:" + std::string(field.category) + ":" + field.name,
                               std::string("Set ") + field.name,
                               std::string(ShortCategoryName(field.category)) + " variable",
                               NodeVisualKind::Field});
        }
        for (const BlueprintFieldDescriptor& field : GetColliderBlueprintFields()) {
            actions.push_back({"field:" + std::string(field.category) + ":" + field.name,
                               std::string("Set ") + field.name,
                               std::string(ShortCategoryName(field.category)) + " variable",
                               NodeVisualKind::Field});
        }

        std::vector<const CreateActionItem*> filtered;
        filtered.reserve(actions.size());
        for (const CreateActionItem& action : actions) {
            if (ContainsCaseInsensitive(action.label, m_createSearch) || ContainsCaseInsensitive(action.subtitle, m_createSearch)) {
                filtered.push_back(&action);
            }
        }

        const int visibleCount = static_cast<int>((std::min)(filtered.size(), static_cast<size_t>(8)));
        const float menuW = 360.0f;
        const float menuItemH = 44.0f;
        const float menuItemSpacing = 6.0f;
        const float menuFooterH = filtered.size() > static_cast<size_t>(visibleCount) ? 28.0f : 0.0f;
        const float menuH = 92.0f + (visibleCount > 0 ? visibleCount * (menuItemH + menuItemSpacing) : 36.0f) + menuFooterH;
        const float menuX = (std::max)(canvasX + 10.0f, (std::min)(m_createMenuScreenX, canvasX + canvasW - menuW - 10.0f));
        const float menuY = (std::max)(canvasY + 28.0f, (std::min)(m_createMenuScreenY, canvasY + canvasH - menuH - 10.0f));
        const float menuMouseX = static_cast<float>(inputManager.GetMouseX());
        const float menuMouseY = static_cast<float>(inputManager.GetMouseY());

        if (!interactionsBlocked &&
            inputManager.IsMouseButtonPressed(0) &&
            !IsPointInRect(menuMouseX, menuMouseY, menuX, menuY, menuW, menuH)) {
            m_showCreateMenu = false;
            m_createSearchActive = false;
        }

        drawList.AddRectFilled(menuX + 4.0f, menuY + 4.0f, menuW, menuH, 0x66000000);
        drawList.AddRectFilled(menuX, menuY, menuW, menuH, 0xFF17181C);
        drawList.AddRectFilled(menuX, menuY, menuW, 28.0f, 0xFF23252B);
        drawList.AddText(fontManager, "All Actions", menuX + 12.0f, menuY + 18.0f, 0xFFFFFFFF);
        drawList.AddText(fontManager, "Search and add graph nodes.",
                         menuX + 12.0f, menuY + 42.0f, 0xFF9FA8B3, menuW - 24.0f);

        DrawInlineTextInput(drawList, fontManager, inputManager, "Search All Actions",
                            m_createSearch, m_createSearchActive, menuX + 12.0f, menuY + 50.0f, menuW - 24.0f, 28.0f,
                            !interactionsBlocked);

        float itemY = menuY + 90.0f;
        if (filtered.empty()) {
            drawList.AddText(fontManager, "No actions match that search.", menuX + 12.0f, itemY + 12.0f, 0xFF8A9098);
        } else {
            for (int actionIndex = 0; actionIndex < visibleCount; ++actionIndex) {
                const CreateActionItem& action = *filtered[static_cast<size_t>(actionIndex)];
                const bool hovered = IsPointInRect(menuMouseX, menuMouseY, menuX + 10.0f, itemY, menuW - 20.0f, menuItemH);
                drawList.AddRectFilled(menuX + 10.0f, itemY, menuW - 20.0f, menuItemH,
                                       hovered ? 0xFF2A2E35 : 0xFF1C1E23);
                drawList.AddRectFilled(menuX + 10.0f, itemY, 4.0f, menuItemH,
                                       action.visual == NodeVisualKind::Component ? 0xFF2E6F66 : NodeAccentColor(action.visual));
                drawList.AddText(fontManager, FitTextToWidth(fontManager, action.label, menuW - 46.0f),
                                 menuX + 22.0f, itemY + 12.0f, 0xFFFFFFFF);
                drawList.AddText(fontManager, FitTextToWidth(fontManager, action.subtitle, menuW - 46.0f),
                                 menuX + 22.0f, itemY + 30.0f, 0xFF9FA8B3);
                if (!interactionsBlocked &&
                    hovered &&
                    inputManager.IsMouseButtonPressed(0) &&
                    SpawnNodeFromCreateMenu(action.id)) {
                    m_showCreateMenu = false;
                    m_createSearch.clear();
                    m_createSearchActive = false;
                    break;
                }
                itemY += menuItemH + menuItemSpacing;
            }

            if (filtered.size() > static_cast<size_t>(visibleCount)) {
                drawList.AddText(fontManager, "Showing first " + std::to_string(visibleCount) + " results.",
                                 menuX + 12.0f, itemY + 10.0f, 0xFF8A9098);
            }
        }
    }

        drawList.PopClipRect();

        if (m_draggingAssetBrowserIndex >= 0 &&
            m_draggingAssetBrowserIndex < static_cast<int>(m_assetBrowserItems.size())) {
            const AssetBrowserItem& draggedItem = m_assetBrowserItems[static_cast<size_t>(m_draggingAssetBrowserIndex)];
            const float mouseX = static_cast<float>(inputManager.GetMouseX());
            const float mouseY = static_cast<float>(inputManager.GetMouseY());
            if (interactionsBlocked) {
                m_draggingAssetBrowserIndex = -1;
            } else if (inputManager.IsMouseButtonDown(0)) {
                drawList.AddRectFilled(mouseX - 74.0f, mouseY - 18.0f, 148.0f, 28.0f, 0xAA1E2530);
                drawList.AddRectFilled(mouseX - 74.0f, mouseY - 18.0f, 4.0f, 28.0f, 0xFF86D7C7);
                drawList.AddText(fontManager, FitTextToWidth(fontManager, draggedItem.label, 126.0f),
                                 mouseX - 62.0f, mouseY - 1.0f, 0xFFFFFFFF, 126.0f);
            } else {
                if (!TryAssignDraggedAsset(mouseX, mouseY, canvasX, canvasY) &&
                    IsPointInRect(mouseX, mouseY, canvasX, canvasY, canvasW, canvasH)) {
                    SetStatus("Drop UI Blueprint assets onto the Widget pin of a Create Widget node.",
                              0xFFE0C36F, 2600);
                }
                m_draggingAssetBrowserIndex = -1;
            }
        }

    }

    drawList.AddRectFilled(canvasX - 1.0f, workspaceTop, 1.0f, canvasH, 0xFF000000);
    drawList.AddRectFilled(inspectorX - 1.0f, workspaceTop, 1.0f, canvasH, 0xFF000000);
}

void BlueprintEditor::EnsureSeeded(const GameObject* selectedObject) {
    if (!m_graphDirty) {
        return;
    }

    if (HasOpenAsset()) {
        if (LoadAsset()) {
            return;
        }

        RebuildGraph(selectedObject);
        m_savedAssetDocument = BuildAssetDocument();
        m_hasSavedAssetDocument = true;
        SetStatus("This Blueprint asset could not be parsed. Showing a default EventGraph instead.",
                  0xFFE0C36F, 4200);
        return;
    }

    RebuildGraph(selectedObject);
}

void BlueprintEditor::RebuildGraph(const GameObject* selectedObject) {
    m_targetLabel = BuildTargetLabel(selectedObject);
    m_nodes.clear();
    m_components.clear();
    m_links.clear();
    m_nextNodeId = 1;
    m_nextComponentId = 1;
    m_selectedNodeId = -1;
    m_selectedComponentId = -1;
    m_draggingNodeId = -1;
    m_draggingDesignerNodeId = -1;
    m_isResizingDesignerNode = false;
    ClearLinkDrag();
    m_isPanning = false;
    m_showCreateMenu = false;
    m_createSearchActive = false;
    m_pan = {24.0f, 42.0f};
    m_zoom = 1.0f;

    Node entryNode;
    entryNode.id = NextNodeId();
    entryNode.title = IsUIAsset() ? "Event Construct" : "Event BeginPlay";
    entryNode.subtitle = IsUIAsset() ? "Widget Event" : "Scene Event";
    entryNode.x = 180.0f;
    entryNode.y = 140.0f;
    entryNode.width = 220.0f;
    entryNode.height = 120.0f;
    entryNode.visual = NodeVisualKind::Event;
    entryNode.nodeTypeId = IsUIAsset() ? BlueprintNodes::kWidgetConstructNodeId : "Event.BeginPlay";
    entryNode.canDelete = true;
    m_nodes.push_back(entryNode);

    if (IsUIAsset()) {
        Node rootCanvasNode;
        rootCanvasNode.id = NextNodeId();
        rootCanvasNode.title = "RootCanvas";
        rootCanvasNode.subtitle = "Canvas";
        rootCanvasNode.x = 480.0f;
        rootCanvasNode.y = 140.0f;
        rootCanvasNode.width = 240.0f;
        rootCanvasNode.height = 144.0f;
        rootCanvasNode.visual = NodeVisualKind::UIElement;
        rootCanvasNode.nodeTypeId = BlueprintNodes::kCanvasElementNodeId;
        rootCanvasNode.uiElementKind = UIElementKind::Canvas;
        rootCanvasNode.canvasX = 28.0f;
        rootCanvasNode.canvasY = 28.0f;
        rootCanvasNode.canvasWidth = 540.0f;
        rootCanvasNode.canvasHeight = 320.0f;
        rootCanvasNode.displayText = "RootCanvas";
        rootCanvasNode.tint = UIntColorToFloat4(0xFF243444);
        rootCanvasNode.canDelete = false;
        m_nodes.push_back(rootCanvasNode);
    }

    m_selectedNodeId = entryNode.id;
    m_graphDirty = false;
}

bool BlueprintEditor::LoadAsset() {
    if (!HasOpenAsset()) {
        return false;
    }

    std::ifstream file(fs::path(m_assetPath), std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    JsonValue rootValue;
    JsonParser parser(content);
    if (!parser.Parse(rootValue) || rootValue.type != JsonValueType::Object) {
        return false;
    }

    const std::string assetType = GetJsonString(rootValue, "type", "CatalystBlueprint");
    m_assetKind = assetType == "CatalystUIBlueprint" ? AssetKind::UI : AssetKind::Actor;
    const bool isUIAsset = IsUIAsset();
    const JsonValue* graphRoot = &rootValue;
    if (isUIAsset) {
        const JsonValue* graphValue = FindJsonField(rootValue, "graph");
        if (graphValue != nullptr && graphValue->type == JsonValueType::Object) {
            graphRoot = graphValue;
        }
    }

    if ((!isUIAsset && GetJsonBool(rootValue, "seedDefaultGraph", false)) ||
        (isUIAsset && GetJsonBool(*graphRoot, "seedDefaultGraph", false))) {
        RebuildGraph(nullptr);
        m_graphDirty = false;
        m_savedAssetDocument = BuildAssetDocument();
        m_hasSavedAssetDocument = true;
        return true;
    }

    m_targetLabel = fs::path(m_assetPath).stem().string();
    m_components.clear();
    m_nodes.clear();
    m_links.clear();
    m_nextNodeId = 1;
    m_nextComponentId = 1;
    m_selectedNodeId = -1;
    m_selectedComponentId = -1;
    m_draggingNodeId = -1;
    m_draggingDesignerNodeId = -1;
    m_isResizingDesignerNode = false;
    ClearLinkDrag();
    m_isPanning = false;
    m_showCreateMenu = false;
    m_createSearchActive = false;
    m_assetBrowserDirty = true;
    m_draggingAssetBrowserIndex = -1;

    const JsonValue* viewValue = FindJsonField(*graphRoot, "view");
    if (viewValue != nullptr && viewValue->type == JsonValueType::Object) {
        m_pan.x = GetJsonNumber(*viewValue, "panX", 24.0f);
        m_pan.y = GetJsonNumber(*viewValue, "panY", 42.0f);
        m_zoom = GetJsonNumber(*viewValue, "zoom", 1.0f);
    } else {
        m_pan = {24.0f, 42.0f};
        m_zoom = 1.0f;
    }
    m_zoom = (std::max)(0.75f, (std::min)(m_zoom, 1.45f));

    int highestComponentId = 0;
    const JsonValue* componentsValue = isUIAsset ? nullptr : FindJsonField(rootValue, "components");
    if (!isUIAsset && componentsValue != nullptr && componentsValue->type == JsonValueType::Array) {
        for (const JsonValue& componentValue : componentsValue->arrayValue) {
            if (componentValue.type != JsonValueType::Object) {
                continue;
            }

            BlueprintComponent component;
            component.id = GetJsonInt(componentValue, "id", highestComponentId + 1);
            component.name = GetJsonString(componentValue, "name", "Component");
            component.kind = ComponentKindFromString(GetJsonString(componentValue, "kind", "StaticMesh"));
            component.assetPath = ResolveBlueprintReferencePath(m_assetPath, GetJsonString(componentValue, "assetPath"));
            component.location = GetJsonFloat3(componentValue, "location", {0.0f, 0.0f, 0.0f});
            component.rotation = GetJsonFloat3(componentValue, "rotation", {0.0f, 0.0f, 0.0f});
            component.scale = GetJsonFloat3(componentValue, "scale", {1.0f, 1.0f, 1.0f});
            component.possessOnPlay = GetJsonBool(componentValue, "possessOnPlay", false);
            component.triggerShape =
                PhysicsColliderShapeFromString(GetJsonString(componentValue, "triggerShape", "Box"));
            component.triggerExtents = GetJsonFloat3(componentValue, "triggerExtents", {0.75f, 0.75f, 0.75f});
            component.triggerRadius = GetJsonNumber(componentValue, "triggerRadius", 0.75f);
            component.exposeToGraph = GetJsonBool(componentValue, "exposeToGraph", true);
            m_components.push_back(component);
            highestComponentId = (std::max)(highestComponentId, component.id);
        }
    }

    int highestNodeId = 0;
    const JsonValue* nodesValue = FindJsonField(*graphRoot, "nodes");
    if (nodesValue != nullptr && nodesValue->type == JsonValueType::Array) {
        for (const JsonValue& nodeValue : nodesValue->arrayValue) {
            if (nodeValue.type != JsonValueType::Object) {
                continue;
            }

            Node node;
            node.id = GetJsonInt(nodeValue, "id", highestNodeId + 1);
            node.title = GetJsonString(nodeValue, "title", "Node");
            node.subtitle = GetJsonString(nodeValue, "subtitle", "");
            node.x = GetJsonNumber(nodeValue, "x", 0.0f);
            node.y = GetJsonNumber(nodeValue, "y", 0.0f);
            node.width = GetJsonNumber(nodeValue, "width", 210.0f);
            node.height = GetJsonNumber(nodeValue, "height", 92.0f);
            node.visual = NodeVisualFromString(GetJsonString(nodeValue, "visual", "Function"));
            node.nodeTypeId = GetJsonString(nodeValue, "nodeTypeId");
            node.fieldCategory = GetJsonString(nodeValue, "fieldCategory");
            node.fieldName = GetJsonString(nodeValue, "fieldName");
            node.componentId = GetJsonInt(nodeValue, "componentId", -1);
            node.componentName = GetJsonString(nodeValue, "componentName");
            node.componentKind = ComponentKindFromString(GetJsonString(nodeValue, "componentKind", "StaticMesh"));
            node.uiElementKind = UIElementKindFromString(GetJsonString(nodeValue, "uiElementKind"));
            if (node.uiElementKind == UIElementKind::None) {
                node.uiElementKind = UIElementKindFromNodeType(node.nodeTypeId);
            }
            node.canvasX = GetJsonNumber(nodeValue, "canvasX", 32.0f);
            node.canvasY = GetJsonNumber(nodeValue, "canvasY", 32.0f);
            node.canvasWidth = GetJsonNumber(nodeValue, "canvasWidth", 180.0f);
            node.canvasHeight = GetJsonNumber(nodeValue, "canvasHeight", 56.0f);
            node.displayText = GetJsonString(nodeValue, "displayText");
            node.tint = GetJsonFloat4(nodeValue, "tint", UIntColorToFloat4(UIElementAccentColor(node.uiElementKind)));
            node.assetPath = ResolveBlueprintReferencePath(m_assetPath, GetJsonString(nodeValue, "assetPath"));
            node.canDelete = GetJsonBool(nodeValue, "canDelete", true);
            node.field = ResolveFieldDescriptor(node.fieldCategory, node.fieldName);
            if (const BlueprintComponent* component = FindComponent(node.componentId)) {
                node.componentName = component->name;
                node.componentKind = component->kind;
            }
            if (node.displayText.empty()) {
                if (node.uiElementKind == UIElementKind::Button) {
                    node.displayText = "Button";
                } else if (node.uiElementKind == UIElementKind::TextBlock) {
                    node.displayText = "Text";
                } else if (node.uiElementKind == UIElementKind::Canvas) {
                    node.displayText = "Canvas";
                } else if (node.uiElementKind == UIElementKind::Image) {
                    node.displayText = "Image";
                }
            }
            if (node.uiElementKind != UIElementKind::None) {
                node.visual = NodeVisualKind::UIElement;
            }

            m_nodes.push_back(node);
            highestNodeId = (std::max)(highestNodeId, node.id);
        }
    }

    const JsonValue* linksValue = FindJsonField(*graphRoot, "links");
    if (linksValue != nullptr && linksValue->type == JsonValueType::Array) {
        for (const JsonValue& linkValue : linksValue->arrayValue) {
            if (linkValue.type != JsonValueType::Object) {
                continue;
            }

            PinReference outputPin;
            outputPin.nodeId = GetJsonInt(linkValue, "fromNodeId", 0);
            outputPin.kind = PinKindFromString(GetJsonString(linkValue, "fromPinKind", "Exec"));
            outputPin.direction = PinDirection::Output;

            PinReference inputPin;
            inputPin.nodeId = GetJsonInt(linkValue, "toNodeId", 0);
            inputPin.kind = PinKindFromString(GetJsonString(linkValue, "toPinKind", "Exec"));
            inputPin.direction = PinDirection::Input;

            AddLink(outputPin, inputPin, GetJsonUInt(linkValue, "color", 0xFF4B4B4B));
        }
    }

    if (m_nodes.empty() && m_links.empty()) {
        RebuildGraph(nullptr);
        m_graphDirty = false;
        m_savedAssetDocument = BuildAssetDocument();
        m_hasSavedAssetDocument = true;
        return true;
    }

    m_nextNodeId = highestNodeId + 1;
    m_nextComponentId = highestComponentId + 1;
    const int savedSelection = GetJsonInt(*graphRoot, "selectedNodeId", -1);
    m_selectedNodeId = (FindNode(savedSelection) != nullptr) ? savedSelection : (m_nodes.empty() ? -1 : m_nodes.front().id);
    const int savedComponentSelection = isUIAsset ? -1 : GetJsonInt(rootValue, "selectedComponentId", -1);
    m_selectedComponentId = (FindComponent(savedComponentSelection) != nullptr)
        ? savedComponentSelection
        : (m_components.empty() ? -1 : m_components.front().id);
    m_graphDirty = false;
    m_savedAssetDocument = BuildAssetDocument();
    m_hasSavedAssetDocument = true;
    return true;
}

std::string BlueprintEditor::BuildAssetDocument() const {
    if (!HasOpenAsset()) {
        return "";
    }

    std::ostringstream file;
    auto WriteNodes = [&]() {
        file << "      \"nodes\": [\n";
        for (size_t nodeIndex = 0; nodeIndex < m_nodes.size(); ++nodeIndex) {
            const Node& node = m_nodes[nodeIndex];
            const std::string fieldCategory = !node.fieldCategory.empty()
                ? node.fieldCategory
                : (node.field != nullptr ? node.field->category : std::string());
            const std::string fieldName = !node.fieldName.empty()
                ? node.fieldName
                : (node.field != nullptr ? node.field->name : std::string());
            const std::wstring storedNodeAssetPath = MakeBlueprintReferencePath(m_assetPath, node.assetPath);
            file << "        {\n";
            file << "          \"id\": " << node.id << ",\n";
            file << "          \"title\": \"" << EscapeJsonString(node.title) << "\",\n";
            file << "          \"subtitle\": \"" << EscapeJsonString(node.subtitle) << "\",\n";
            file << "          \"x\": " << node.x << ",\n";
            file << "          \"y\": " << node.y << ",\n";
            file << "          \"width\": " << node.width << ",\n";
            file << "          \"height\": " << node.height << ",\n";
            file << "          \"visual\": \"" << NodeVisualToString(node.visual) << "\",\n";
            file << "          \"nodeTypeId\": \"" << EscapeJsonString(node.nodeTypeId) << "\",\n";
            file << "          \"fieldCategory\": \"" << EscapeJsonString(fieldCategory) << "\",\n";
            file << "          \"fieldName\": \"" << EscapeJsonString(fieldName) << "\",\n";
            file << "          \"componentId\": " << node.componentId << ",\n";
            file << "          \"componentName\": \"" << EscapeJsonString(node.componentName) << "\",\n";
            file << "          \"componentKind\": \"" << ComponentKindToString(node.componentKind) << "\",\n";
            file << "          \"uiElementKind\": \"" << UIElementKindToString(node.uiElementKind) << "\",\n";
            file << "          \"canvasX\": " << node.canvasX << ",\n";
            file << "          \"canvasY\": " << node.canvasY << ",\n";
            file << "          \"canvasWidth\": " << node.canvasWidth << ",\n";
            file << "          \"canvasHeight\": " << node.canvasHeight << ",\n";
            file << "          \"displayText\": \"" << EscapeJsonString(node.displayText) << "\",\n";
            file << "          \"tint\": ";
            WriteJsonFloat4(file, node.tint);
            file << ",\n";
            file << "          \"assetPath\": \"" << EscapeJsonString(WideToUtf8(storedNodeAssetPath)) << "\",\n";
            file << "          \"canDelete\": " << (node.canDelete ? "true" : "false") << "\n";
            file << "        }" << (nodeIndex + 1 < m_nodes.size() ? "," : "") << "\n";
        }
        file << "      ],\n";
        file << "      \"links\": [\n";
        for (size_t linkIndex = 0; linkIndex < m_links.size(); ++linkIndex) {
            const Link& link = m_links[linkIndex];
            file << "        {\n";
            file << "          \"fromNodeId\": " << link.fromNodeId << ",\n";
            file << "          \"toNodeId\": " << link.toNodeId << ",\n";
            file << "          \"fromPinKind\": \"" << PinKindToString(link.fromPinKind) << "\",\n";
            file << "          \"toPinKind\": \"" << PinKindToString(link.toPinKind) << "\",\n";
            file << "          \"color\": " << link.color << "\n";
            file << "        }" << (linkIndex + 1 < m_links.size() ? "," : "") << "\n";
        }
        file << "      ]\n";
    };

    if (IsUIAsset()) {
        file << "{\n";
        file << "  \"type\": \"CatalystUIBlueprint\",\n";
        file << "  \"version\": 2,\n";
        file << "  \"parent\": \"UserWidget\",\n";
        file << "  \"designer\": {\n";
        file << "    \"root\": {\n";
        file << "      \"type\": \"CanvasPanel\",\n";
        file << "      \"name\": \"RootCanvas\",\n";
        file << "      \"children\": []\n";
        file << "    }\n";
        file << "  },\n";
        file << "  \"graph\": {\n";
        file << "      \"seedDefaultGraph\": false,\n";
        file << "      \"selectedNodeId\": " << m_selectedNodeId << ",\n";
        file << "      \"view\": {\n";
        file << "        \"panX\": " << m_pan.x << ",\n";
        file << "        \"panY\": " << m_pan.y << ",\n";
        file << "        \"zoom\": " << m_zoom << "\n";
        file << "      },\n";
        WriteNodes();
        file << "  }\n";
        file << "}\n";
        return file.str();
    }

    file << "{\n";
    file << "  \"type\": \"CatalystBlueprint\",\n";
    file << "  \"version\": 2,\n";
    file << "  \"parent\": \"Actor\",\n";
    file << "  \"seedDefaultGraph\": false,\n";
    file << "  \"selectedNodeId\": " << m_selectedNodeId << ",\n";
    file << "  \"selectedComponentId\": " << m_selectedComponentId << ",\n";
    file << "  \"view\": {\n";
    file << "    \"panX\": " << m_pan.x << ",\n";
    file << "    \"panY\": " << m_pan.y << ",\n";
    file << "    \"zoom\": " << m_zoom << "\n";
    file << "  },\n";
    file << "  \"components\": [\n";
    for (size_t componentIndex = 0; componentIndex < m_components.size(); ++componentIndex) {
        const BlueprintComponent& component = m_components[componentIndex];
        const std::wstring storedAssetPath = MakeBlueprintReferencePath(m_assetPath, component.assetPath);
        file << "    {\n";
        file << "      \"id\": " << component.id << ",\n";
        file << "      \"name\": \"" << EscapeJsonString(component.name) << "\",\n";
        file << "      \"kind\": \"" << ComponentKindToString(component.kind) << "\",\n";
        file << "      \"assetPath\": \"" << EscapeJsonString(WideToUtf8(storedAssetPath)) << "\",\n";
        file << "      \"location\": ";
        WriteJsonFloat3(file, component.location);
        file << ",\n";
        file << "      \"rotation\": ";
        WriteJsonFloat3(file, component.rotation);
        file << ",\n";
        file << "      \"scale\": ";
        WriteJsonFloat3(file, component.scale);
        file << ",\n";
        file << "      \"possessOnPlay\": " << (component.possessOnPlay ? "true" : "false") << ",\n";
        file << "      \"triggerShape\": \"" << PhysicsColliderShapeToString(component.triggerShape) << "\",\n";
        file << "      \"triggerExtents\": ";
        WriteJsonFloat3(file, component.triggerExtents);
        file << ",\n";
        file << "      \"triggerRadius\": " << component.triggerRadius << ",\n";
        file << "      \"exposeToGraph\": " << (component.exposeToGraph ? "true" : "false") << "\n";
        file << "    }" << (componentIndex + 1 < m_components.size() ? "," : "") << "\n";
    }
    file << "  ],\n";
    file << "  \"nodes\": [\n";
    for (size_t nodeIndex = 0; nodeIndex < m_nodes.size(); ++nodeIndex) {
        const Node& node = m_nodes[nodeIndex];
        const std::string fieldCategory = !node.fieldCategory.empty()
            ? node.fieldCategory
            : (node.field != nullptr ? node.field->category : std::string());
        const std::string fieldName = !node.fieldName.empty()
            ? node.fieldName
            : (node.field != nullptr ? node.field->name : std::string());
        const std::wstring storedNodeAssetPath = MakeBlueprintReferencePath(m_assetPath, node.assetPath);
        file << "    {\n";
        file << "      \"id\": " << node.id << ",\n";
        file << "      \"title\": \"" << EscapeJsonString(node.title) << "\",\n";
        file << "      \"subtitle\": \"" << EscapeJsonString(node.subtitle) << "\",\n";
        file << "      \"x\": " << node.x << ",\n";
        file << "      \"y\": " << node.y << ",\n";
        file << "      \"width\": " << node.width << ",\n";
        file << "      \"height\": " << node.height << ",\n";
        file << "      \"visual\": \"" << NodeVisualToString(node.visual) << "\",\n";
        file << "      \"nodeTypeId\": \"" << EscapeJsonString(node.nodeTypeId) << "\",\n";
        file << "      \"fieldCategory\": \"" << EscapeJsonString(fieldCategory) << "\",\n";
        file << "      \"fieldName\": \"" << EscapeJsonString(fieldName) << "\",\n";
        file << "      \"componentId\": " << node.componentId << ",\n";
        file << "      \"componentName\": \"" << EscapeJsonString(node.componentName) << "\",\n";
        file << "      \"componentKind\": \"" << ComponentKindToString(node.componentKind) << "\",\n";
        file << "      \"uiElementKind\": \"" << UIElementKindToString(node.uiElementKind) << "\",\n";
        file << "      \"canvasX\": " << node.canvasX << ",\n";
        file << "      \"canvasY\": " << node.canvasY << ",\n";
        file << "      \"canvasWidth\": " << node.canvasWidth << ",\n";
        file << "      \"canvasHeight\": " << node.canvasHeight << ",\n";
        file << "      \"displayText\": \"" << EscapeJsonString(node.displayText) << "\",\n";
        file << "      \"tint\": ";
        WriteJsonFloat4(file, node.tint);
        file << ",\n";
        file << "      \"assetPath\": \"" << EscapeJsonString(WideToUtf8(storedNodeAssetPath)) << "\",\n";
        file << "      \"canDelete\": " << (node.canDelete ? "true" : "false") << "\n";
        file << "    }" << (nodeIndex + 1 < m_nodes.size() ? "," : "") << "\n";
    }
    file << "  ],\n";
    file << "  \"links\": [\n";
    for (size_t linkIndex = 0; linkIndex < m_links.size(); ++linkIndex) {
        const Link& link = m_links[linkIndex];
        file << "    {\n";
        file << "      \"fromNodeId\": " << link.fromNodeId << ",\n";
        file << "      \"toNodeId\": " << link.toNodeId << ",\n";
        file << "      \"fromPinKind\": \"" << PinKindToString(link.fromPinKind) << "\",\n";
        file << "      \"toPinKind\": \"" << PinKindToString(link.toPinKind) << "\",\n";
        file << "      \"color\": " << link.color << "\n";
        file << "    }" << (linkIndex + 1 < m_links.size() ? "," : "") << "\n";
    }
    file << "  ]\n";
    file << "}\n";
    return file.str();
}

bool BlueprintEditor::SaveAsset() {
    if (!HasOpenAsset()) {
        return false;
    }

    try {
        std::error_code ec;
        fs::create_directories(fs::path(m_assetPath).parent_path(), ec);

        const std::string assetDocument = BuildAssetDocument();
        std::ofstream file(fs::path(m_assetPath), std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return false;
        }
        file << assetDocument;
        file.close();
        m_savedAssetDocument = assetDocument;
        m_hasSavedAssetDocument = true;
        return true;
    } catch (...) {
        return false;
    }
}

int BlueprintEditor::AddFieldColumn(std::span<const BlueprintFieldDescriptor> fields, float startX, float startY, float rowSpacing) {
    int firstNodeId = -1;
    int previousNodeId = -1;
    float cursorY = startY;

    for (const BlueprintFieldDescriptor& field : fields) {
        const int nodeId = AddFieldNode(field, startX, cursorY);
        if (firstNodeId == -1) {
            firstNodeId = nodeId;
        }
        if (previousNodeId != -1) {
            AddLink(previousNodeId, nodeId, 0xFF394E69);
        }
        previousNodeId = nodeId;
        cursorY += rowSpacing;
    }

    return firstNodeId;
}

int BlueprintEditor::AddFieldNode(const BlueprintFieldDescriptor& field, float x, float y, const std::string& customTitle) {
    Node node;
    node.id = NextNodeId();
    node.title = customTitle.empty() ? ("Set " + std::string(field.name)) : customTitle;
    node.subtitle = std::string(ShortCategoryName(field.category));
    node.x = x;
    node.y = y;
    node.width = 250.0f;
    node.height = 142.0f;
    node.visual = NodeVisualKind::Field;
    node.nodeTypeId = "Field." + std::string(field.category) + "." + std::string(field.name);
    node.fieldName = field.name;
    node.fieldCategory = field.category;
    node.field = &field;
    node.canDelete = true;
    m_nodes.push_back(node);
    return node.id;
}

int BlueprintEditor::AddComponentNode(const BlueprintComponent& component, float x, float y) {
    Node node;
    node.id = NextNodeId();
    node.title = component.name;
    node.subtitle = BuildComponentTypeLabel(component.kind);
    node.x = x;
    node.y = y;
    node.width = component.kind == ComponentKind::Trigger ? 260.0f : 244.0f;
    node.height = component.kind == ComponentKind::Trigger ? 132.0f : 124.0f;
    node.visual = NodeVisualKind::Component;
    node.nodeTypeId = std::string("Component.") + ComponentKindToString(component.kind);
    node.componentId = component.id;
    node.componentName = component.name;
    node.componentKind = component.kind;
    node.canDelete = true;
    m_nodes.push_back(node);
    return node.id;
}

int BlueprintEditor::AddUIElementNode(UIElementKind kind, float x, float y) {
    Node node;
    node.id = NextNodeId();
    node.x = x;
    node.y = y;
    node.visual = NodeVisualKind::UIElement;
    node.uiElementKind = kind;
    node.canDelete = true;
    node.tint = UIntColorToFloat4(UIElementAccentColor(kind));

    switch (kind) {
    case UIElementKind::Canvas:
        node.title = "Canvas";
        node.subtitle = "UI Element";
        node.nodeTypeId = BlueprintNodes::kCanvasElementNodeId;
        node.width = 238.0f;
        node.height = 144.0f;
        node.canvasWidth = 360.0f;
        node.canvasHeight = 220.0f;
        node.displayText = "Canvas";
        node.tint = UIntColorToFloat4(0xFF243444);
        break;
    case UIElementKind::Button:
        node.title = "Button";
        node.subtitle = "Clickable UI";
        node.nodeTypeId = BlueprintNodes::kButtonElementNodeId;
        node.width = 246.0f;
        node.height = 144.0f;
        node.canvasWidth = 180.0f;
        node.canvasHeight = 52.0f;
        node.displayText = "Click Me";
        node.tint = UIntColorToFloat4(0xFF4A68A8);
        break;
    case UIElementKind::Image:
        node.title = "Image";
        node.subtitle = "Visual UI";
        node.nodeTypeId = BlueprintNodes::kImageElementNodeId;
        node.width = 238.0f;
        node.height = 144.0f;
        node.canvasWidth = 140.0f;
        node.canvasHeight = 140.0f;
        node.displayText = "Image";
        node.tint = UIntColorToFloat4(0xFF8969A4);
        break;
    case UIElementKind::TextBlock:
        node.title = "TextBlock";
        node.subtitle = "Text UI";
        node.nodeTypeId = BlueprintNodes::kTextBlockElementNodeId;
        node.width = 246.0f;
        node.height = 136.0f;
        node.canvasWidth = 220.0f;
        node.canvasHeight = 34.0f;
        node.displayText = "Sample Text";
        node.tint = UIntColorToFloat4(0xFFFFFFFF);
        break;
    case UIElementKind::None:
    default:
        node.title = "UI Element";
        node.subtitle = "Widget Node";
        node.nodeTypeId = "UIElement";
        break;
    }

    const size_t existingElementCount = static_cast<size_t>(std::count_if(m_nodes.begin(), m_nodes.end(), [](const Node& existingNode) {
        return existingNode.visual == NodeVisualKind::UIElement;
    }));
    node.canvasX = 36.0f + static_cast<float>((existingElementCount % 3) * 34);
    node.canvasY = 42.0f + static_cast<float>(existingElementCount * 36);

    m_nodes.push_back(node);
    return node.id;
}

int BlueprintEditor::AddCommentNode(const std::string& title, float x, float y, float width, float height) {
    Node node;
    node.id = NextNodeId();
    node.title = title.empty() ? "Comment" : title;
    node.subtitle = "Comment Box";
    node.x = x;
    node.y = y;
    node.width = width;
    node.height = height;
    node.visual = NodeVisualKind::Comment;
    node.nodeTypeId = "Comment";
    node.canDelete = true;
    m_nodes.push_back(node);
    return node.id;
}

BlueprintEditor::BlueprintComponent* BlueprintEditor::FindComponent(int componentId) {
    for (BlueprintComponent& component : m_components) {
        if (component.id == componentId) {
            return &component;
        }
    }
    return nullptr;
}

const BlueprintEditor::BlueprintComponent* BlueprintEditor::FindComponent(int componentId) const {
    for (const BlueprintComponent& component : m_components) {
        if (component.id == componentId) {
            return &component;
        }
    }
    return nullptr;
}

int BlueprintEditor::AddComponent(ComponentKind kind) {
    BlueprintComponent component;
    component.id = NextComponentId();
    component.kind = kind;

    int existingCount = 0;
    for (const BlueprintComponent& existingComponent : m_components) {
        if (existingComponent.kind == kind) {
            ++existingCount;
        }
    }

    switch (kind) {
    case ComponentKind::SkeletalMesh:
        component.name = "SkeletalMesh" + std::to_string(existingCount + 1);
        component.location = {0.0f, 0.0f, 0.0f};
        break;
    case ComponentKind::Camera:
        component.name = "Camera" + std::to_string(existingCount + 1);
        component.location = {0.0f, 2.0f, -5.0f};
        component.possessOnPlay = std::none_of(m_components.begin(), m_components.end(), [](const BlueprintComponent& item) {
            return item.kind == ComponentKind::Camera && item.possessOnPlay;
        });
        break;
    case ComponentKind::Trigger:
        component.name = "Trigger" + std::to_string(existingCount + 1);
        component.location = {0.0f, 0.5f, 0.0f};
        component.triggerExtents = {0.75f, 0.75f, 0.75f};
        component.triggerRadius = 0.75f;
        break;
    case ComponentKind::StaticMesh:
    default:
        component.name = "StaticMesh" + std::to_string(existingCount + 1);
        component.location = {0.0f, 0.0f, 0.0f};
        break;
    }

    m_components.push_back(component);
    return component.id;
}

void BlueprintEditor::RemoveComponent(int componentId) {
    const auto componentIt = std::remove_if(m_components.begin(), m_components.end(), [componentId](const BlueprintComponent& component) {
        return component.id == componentId;
    });
    if (componentIt == m_components.end()) {
        return;
    }

    std::vector<int> affectedNodeIds;
    for (const Node& node : m_nodes) {
        if (node.componentId == componentId) {
            affectedNodeIds.push_back(node.id);
        }
    }

    m_components.erase(componentIt, m_components.end());
    m_nodes.erase(std::remove_if(m_nodes.begin(), m_nodes.end(), [componentId](const Node& node) {
        return node.componentId == componentId;
    }), m_nodes.end());
    m_links.erase(std::remove_if(m_links.begin(), m_links.end(), [&](const Link& link) {
        return std::find(affectedNodeIds.begin(), affectedNodeIds.end(), link.fromNodeId) != affectedNodeIds.end() ||
               std::find(affectedNodeIds.begin(), affectedNodeIds.end(), link.toNodeId) != affectedNodeIds.end();
    }), m_links.end());

    if (m_selectedComponentId == componentId) {
        m_selectedComponentId = -1;
    }
    if (const Node* selectedNode = FindNode(m_selectedNodeId)) {
        if (selectedNode->componentId == componentId) {
            m_selectedNodeId = -1;
        }
    }
}

bool BlueprintEditor::CanNodeUsePin(const Node& node, PinKind kind, PinDirection direction) const {
    switch (node.visual) {
    case NodeVisualKind::Event:
        return kind == PinKind::Exec && direction == PinDirection::Output;
    case NodeVisualKind::Function:
        return true;
    case NodeVisualKind::Field:
        return kind == PinKind::Exec || (kind == PinKind::Data && direction == PinDirection::Input);
    case NodeVisualKind::Component:
        return kind == PinKind::Data && direction == PinDirection::Output;
    case NodeVisualKind::UIElement:
        if (kind == PinKind::Data) {
            return direction == PinDirection::Output;
        }
        return node.uiElementKind == UIElementKind::Button && direction == PinDirection::Output;
    case NodeVisualKind::Comment:
    default:
        return false;
    }
}

uint32_t BlueprintEditor::GetPinColor(const Node& node, PinKind kind) const {
    if (kind == PinKind::Exec) {
        return 0xFFEFEFEF;
    }

    if (node.visual == NodeVisualKind::Component) {
        return ComponentAccentColor(node.componentKind);
    }

    if (node.visual == NodeVisualKind::UIElement) {
        return UIElementAccentColor(node.uiElementKind);
    }

    if (node.visual == NodeVisualKind::Field) {
        return node.field != nullptr ? FieldTypeColor(node.field->type) : 0xFF7B7B7B;
    }

    return 0xFF5A86D6;
}

bool BlueprintEditor::GetPinScreenPosition(const Node& node, PinKind kind, PinDirection direction,
                                           float canvasX, float canvasY,
                                           float& outX, float& outY, uint32_t* outColor) const {
    if (!CanNodeUsePin(node, kind, direction)) {
        return false;
    }

    const float nodeX = canvasX + m_pan.x + node.x * m_zoom;
    const float nodeY = canvasY + m_pan.y + node.y * m_zoom;
    const float nodeW = node.width * m_zoom;
    const float headerH = (std::max)(22.0f, 26.0f * m_zoom);
    const float rowX = nodeX + 8.0f * m_zoom;
    const float rowW = nodeW - 16.0f * m_zoom;
    float rowY = 0.0f;

    switch (node.visual) {
    case NodeVisualKind::Event:
        rowY = nodeY + headerH + 42.0f;
        break;
    case NodeVisualKind::Function:
        rowY = nodeY + headerH + (kind == PinKind::Exec ? 40.0f : 64.0f);
        break;
    case NodeVisualKind::Field:
        rowY = nodeY + headerH + (kind == PinKind::Exec ? 38.0f : 62.0f);
        break;
    case NodeVisualKind::Component:
        rowY = nodeY + headerH + 48.0f;
        break;
    case NodeVisualKind::UIElement:
        rowY = nodeY + headerH + (kind == PinKind::Exec ? 44.0f : 70.0f);
        break;
    case NodeVisualKind::Comment:
    default:
        return false;
    }

    const float pinSize = (std::max)(4.0f, 6.0f * m_zoom);
    const float pinX = direction == PinDirection::Output ? (rowX + rowW - 10.0f * m_zoom)
                                                         : (rowX + 4.0f * m_zoom);
    const float pinY = rowY - pinSize - 1.0f;
    outX = pinX + pinSize * 0.5f;
    outY = pinY + pinSize * 0.5f;
    if (outColor != nullptr) {
        *outColor = GetPinColor(node, kind);
    }
    return true;
}

BlueprintEditor::PinReference BlueprintEditor::HitTestPin(float screenX, float screenY, float canvasX, float canvasY) const {
    for (auto it = m_nodes.rbegin(); it != m_nodes.rend(); ++it) {
        for (PinDirection direction : {PinDirection::Output, PinDirection::Input}) {
            for (PinKind kind : {PinKind::Exec, PinKind::Data}) {
                float pinX = 0.0f;
                float pinY = 0.0f;
                if (!GetPinScreenPosition(*it, kind, direction, canvasX, canvasY, pinX, pinY, nullptr)) {
                    continue;
                }

                const float hitSize = (std::max)(12.0f, 16.0f * m_zoom);
                if (IsPointInRect(screenX, screenY, pinX - hitSize * 0.5f, pinY - hitSize * 0.5f, hitSize, hitSize)) {
                    return {it->id, kind, direction};
                }
            }
        }
    }

    return {};
}

BlueprintEditor::PinReference BlueprintEditor::FindCompatibleDropPin(const PinReference& dragStartPin, float screenX, float screenY,
                                                                     float canvasX, float canvasY) {
    const PinReference directPin = HitTestPin(screenX, screenY, canvasX, canvasY);
    if (CanConnectPins(dragStartPin, directPin)) {
        return directPin;
    }

    Node* hitNode = HitTestNode(screenX, screenY, canvasX, canvasY);
    if (hitNode == nullptr || hitNode->id == dragStartPin.nodeId) {
        return {};
    }

    const PinDirection targetDirection = dragStartPin.direction == PinDirection::Output
        ? PinDirection::Input
        : PinDirection::Output;
    const PinReference nodePin = {hitNode->id, dragStartPin.kind, targetDirection};
    return CanConnectPins(dragStartPin, nodePin) ? nodePin : PinReference{};
}

bool BlueprintEditor::NormalizePins(const PinReference& firstPin, const PinReference& secondPin,
                                    PinReference& outOutputPin, PinReference& outInputPin) const {
    if (firstPin.nodeId < 0 || secondPin.nodeId < 0) {
        return false;
    }

    if (firstPin.direction == PinDirection::Output && secondPin.direction == PinDirection::Input) {
        outOutputPin = firstPin;
        outInputPin = secondPin;
        return true;
    }
    if (firstPin.direction == PinDirection::Input && secondPin.direction == PinDirection::Output) {
        outOutputPin = secondPin;
        outInputPin = firstPin;
        return true;
    }

    return false;
}

bool BlueprintEditor::CanConnectPins(const PinReference& firstPin, const PinReference& secondPin) const {
    PinReference outputPin;
    PinReference inputPin;
    if (!NormalizePins(firstPin, secondPin, outputPin, inputPin)) {
        return false;
    }
    if (outputPin.nodeId == inputPin.nodeId || outputPin.kind != inputPin.kind) {
        return false;
    }

    const Node* outputNode = FindNode(outputPin.nodeId);
    const Node* inputNode = FindNode(inputPin.nodeId);
    if (outputNode == nullptr || inputNode == nullptr) {
        return false;
    }

    return CanNodeUsePin(*outputNode, outputPin.kind, PinDirection::Output) &&
           CanNodeUsePin(*inputNode, inputPin.kind, PinDirection::Input);
}

void BlueprintEditor::ClearLinkDrag() {
    m_isDraggingLink = false;
    m_dragLinkStartPin = {};
    m_dragLinkHoverPin = {};
}

bool BlueprintEditor::AddLink(const PinReference& firstPin, const PinReference& secondPin, uint32_t color) {
    PinReference outputPin;
    PinReference inputPin;
    if (!NormalizePins(firstPin, secondPin, outputPin, inputPin) || !CanConnectPins(outputPin, inputPin)) {
        return false;
    }

    const auto samePin = [](const PinReference& a, const PinReference& b) {
        return a.nodeId == b.nodeId && a.kind == b.kind && a.direction == b.direction;
    };

    for (const Link& link : m_links) {
        const PinReference existingOutput = {link.fromNodeId, link.fromPinKind, PinDirection::Output};
        const PinReference existingInput = {link.toNodeId, link.toPinKind, PinDirection::Input};
        if (samePin(existingOutput, outputPin) && samePin(existingInput, inputPin)) {
            return false;
        }
    }

    m_links.erase(std::remove_if(m_links.begin(), m_links.end(), [&](const Link& link) {
        return link.toNodeId == inputPin.nodeId && link.toPinKind == inputPin.kind;
    }), m_links.end());

    if (color == 0) {
        const Node* outputNode = FindNode(outputPin.nodeId);
        color = outputNode != nullptr ? GetPinColor(*outputNode, outputPin.kind) : 0xFF4B4B4B;
    }

    m_links.push_back({outputPin.nodeId, inputPin.nodeId, outputPin.kind, inputPin.kind, color});
    return true;
}

void BlueprintEditor::AddLink(int fromNodeId, int toNodeId, uint32_t color) {
    AddLink({fromNodeId, PinKind::Exec, PinDirection::Output},
            {toNodeId, PinKind::Exec, PinDirection::Input},
            color);
}

void BlueprintEditor::RemoveNode(int nodeId) {
    const auto nodeIt = std::remove_if(m_nodes.begin(), m_nodes.end(), [nodeId](const Node& node) {
        return node.id == nodeId;
    });
    if (nodeIt == m_nodes.end()) {
        return;
    }

    m_nodes.erase(nodeIt, m_nodes.end());
    m_links.erase(std::remove_if(m_links.begin(), m_links.end(), [nodeId](const Link& link) {
        return link.fromNodeId == nodeId || link.toNodeId == nodeId;
    }), m_links.end());

    if (m_selectedNodeId == nodeId) {
        m_selectedNodeId = -1;
    }
    if (m_draggingNodeId == nodeId) {
        m_draggingNodeId = -1;
    }
    if (m_dragLinkStartPin.nodeId == nodeId || m_dragLinkHoverPin.nodeId == nodeId) {
        ClearLinkDrag();
    }
}

BlueprintEditor::Node* BlueprintEditor::FindNode(int nodeId) {
    for (Node& node : m_nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

const BlueprintEditor::Node* BlueprintEditor::FindNode(int nodeId) const {
    for (const Node& node : m_nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

BlueprintEditor::Node* BlueprintEditor::HitTestNode(float screenX, float screenY, float canvasX, float canvasY) {
    for (auto it = m_nodes.rbegin(); it != m_nodes.rend(); ++it) {
        const float nodeX = canvasX + m_pan.x + it->x * m_zoom;
        const float nodeY = canvasY + m_pan.y + it->y * m_zoom;
        if (IsPointInRect(screenX, screenY, nodeX, nodeY, it->width * m_zoom, it->height * m_zoom)) {
            return &(*it);
        }
    }
    return nullptr;
}

void BlueprintEditor::UpdateInteraction(InputManager& inputManager, float canvasX, float canvasY, float canvasW, float canvasH) {
    const float mouseX = static_cast<float>(inputManager.GetMouseX());
    const float mouseY = static_cast<float>(inputManager.GetMouseY());
    const bool isInsideCanvas = IsPointInRect(mouseX, mouseY, canvasX, canvasY, canvasW, canvasH);

    if (isInsideCanvas) {
        const int wheelDelta = inputManager.GetMouseWheelDelta();
        if (wheelDelta != 0) {
            const float oldZoom = m_zoom;
            const float graphMouseX = ScreenToGraph(mouseX, canvasX, m_pan.x, oldZoom);
            const float graphMouseY = ScreenToGraph(mouseY, canvasY, m_pan.y, oldZoom);
            const float zoomFactor = std::pow(1.12f, static_cast<float>(wheelDelta) / 120.0f);
            m_zoom = (std::max)(0.75f, (std::min)(oldZoom * zoomFactor, 1.45f));
            m_pan.x = (mouseX - canvasX) - graphMouseX * m_zoom;
            m_pan.y = (mouseY - canvasY) - graphMouseY * m_zoom;
        }
    }

    if (m_showCreateMenu) {
        const bool deleteIsDown = (GetAsyncKeyState(VK_DELETE) & 0x8000) != 0;
        m_deleteWasDown = deleteIsDown;
        if (!inputManager.IsMouseButtonDown(1)) {
            m_isPanning = false;
            m_contextClickPending = false;
        }
        return;
    }

    if (m_isDraggingLink) {
        m_dragLinkHoverPin = isInsideCanvas
            ? FindCompatibleDropPin(m_dragLinkStartPin, mouseX, mouseY, canvasX, canvasY)
            : PinReference{};

        if (!inputManager.IsMouseButtonDown(0)) {
            PinReference outputPin;
            PinReference inputPin;
            if (CanConnectPins(m_dragLinkStartPin, m_dragLinkHoverPin) &&
                NormalizePins(m_dragLinkStartPin, m_dragLinkHoverPin, outputPin, inputPin)) {
                const Node* outputNode = FindNode(outputPin.nodeId);
                const Node* inputNode = FindNode(inputPin.nodeId);
                const bool didAddLink = AddLink(outputPin, inputPin);
                SetStatus(didAddLink
                              ? ("Connected " +
                                 (outputNode != nullptr ? outputNode->title : std::string("node")) +
                                 " to " +
                                 (inputNode != nullptr ? inputNode->title : std::string("node")) +
                                 ".")
                              : "Those pins are already connected.",
                          didAddLink ? 0xFF89D185 : 0xFFE0C36F,
                          didAddLink ? 2400 : 2200);
            }
            ClearLinkDrag();
        }

        const bool deleteIsDown = (GetAsyncKeyState(VK_DELETE) & 0x8000) != 0;
        m_deleteWasDown = deleteIsDown;
        return;
    }

    if (inputManager.IsMouseButtonPressed(1) && isInsideCanvas) {
        m_isPanning = false;
        m_contextClickPending = true;
        m_contextPressMouseX = inputManager.GetMouseX();
        m_contextPressMouseY = inputManager.GetMouseY();
        m_lastMouseX = inputManager.GetMouseX();
        m_lastMouseY = inputManager.GetMouseY();
    }
    if (m_contextClickPending && inputManager.IsMouseButtonDown(1)) {
        const int movedX = std::abs(inputManager.GetMouseX() - m_contextPressMouseX);
        const int movedY = std::abs(inputManager.GetMouseY() - m_contextPressMouseY);
        if (movedX > 5 || movedY > 5) {
            m_isPanning = true;
            m_contextClickPending = false;
            m_lastMouseX = inputManager.GetMouseX();
            m_lastMouseY = inputManager.GetMouseY();
        }
    }
    if (m_isPanning) {
        if (!inputManager.IsMouseButtonDown(1)) {
            m_isPanning = false;
        } else {
            const int dx = inputManager.GetMouseX() - m_lastMouseX;
            const int dy = inputManager.GetMouseY() - m_lastMouseY;
            m_pan.x += static_cast<float>(dx);
            m_pan.y += static_cast<float>(dy);
            m_lastMouseX = inputManager.GetMouseX();
            m_lastMouseY = inputManager.GetMouseY();
        }
    }
    if (m_contextClickPending && !inputManager.IsMouseButtonDown(1)) {
        OpenCreateMenu(mouseX, mouseY, canvasX, canvasY);
        m_contextClickPending = false;
    }

    if (inputManager.IsMouseButtonPressed(0) && isInsideCanvas) {
        const PinReference hitPin = HitTestPin(mouseX, mouseY, canvasX, canvasY);
        if (hitPin.nodeId != -1) {
            m_selectedNodeId = hitPin.nodeId;
            m_draggingNodeId = -1;
            m_isDraggingLink = true;
            m_dragLinkStartPin = hitPin;
            m_dragLinkHoverPin = FindCompatibleDropPin(hitPin, mouseX, mouseY, canvasX, canvasY);
            const bool deleteIsDown = (GetAsyncKeyState(VK_DELETE) & 0x8000) != 0;
            m_deleteWasDown = deleteIsDown;
            return;
        }

        Node* hitNode = HitTestNode(mouseX, mouseY, canvasX, canvasY);
        if (hitNode != nullptr) {
            m_selectedNodeId = hitNode->id;
            m_draggingNodeId = hitNode->id;
            m_lastMouseX = inputManager.GetMouseX();
            m_lastMouseY = inputManager.GetMouseY();
        } else {
            m_selectedNodeId = -1;
        }
    }

    if (m_draggingNodeId != -1) {
        if (!inputManager.IsMouseButtonDown(0)) {
            m_draggingNodeId = -1;
        } else {
            Node* draggedNode = FindNode(m_draggingNodeId);
            if (draggedNode != nullptr) {
                const int dx = inputManager.GetMouseX() - m_lastMouseX;
                const int dy = inputManager.GetMouseY() - m_lastMouseY;
                draggedNode->x += static_cast<float>(dx) / m_zoom;
                draggedNode->y += static_cast<float>(dy) / m_zoom;
            }
            m_lastMouseX = inputManager.GetMouseX();
            m_lastMouseY = inputManager.GetMouseY();
        }
    }

    const bool deleteIsDown = (GetAsyncKeyState(VK_DELETE) & 0x8000) != 0;
    if (deleteIsDown && !m_deleteWasDown && m_selectedNodeId != -1) {
        Node* selectedNode = FindNode(m_selectedNodeId);
        if (selectedNode != nullptr) {
            if (selectedNode->canDelete) {
                const std::string nodeTitle = selectedNode->title;
                RemoveNode(selectedNode->id);
                SetStatus("Deleted " + nodeTitle + ".", 0xFFB7BEC8, 2200);
            } else {
                SetStatus("That node is locked and cannot be deleted.", 0xFFE0C36F, 2600);
            }
        }
    }
    m_deleteWasDown = deleteIsDown;
}

void BlueprintEditor::FrameNode(int nodeId, float canvasW, float canvasH) {
    if (m_nodes.empty()) {
        return;
    }

    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    bool hasBounds = false;

    auto ExpandBounds = [&](const Node& node) {
        if (!hasBounds) {
            minX = node.x;
            minY = node.y;
            maxX = node.x + node.width;
            maxY = node.y + node.height;
            hasBounds = true;
            return;
        }

        minX = (std::min)(minX, node.x);
        minY = (std::min)(minY, node.y);
        maxX = (std::max)(maxX, node.x + node.width);
        maxY = (std::max)(maxY, node.y + node.height);
    };

    if (const Node* node = FindNode(nodeId)) {
        ExpandBounds(*node);
    } else {
        for (const Node& node : m_nodes) {
            ExpandBounds(node);
        }
    }

    if (!hasBounds) {
        return;
    }

    const float boundsW = (std::max)(120.0f, maxX - minX);
    const float boundsH = (std::max)(80.0f, maxY - minY);
    const float padding = 120.0f;
    const float fitZoomX = (canvasW - padding) / boundsW;
    const float fitZoomY = (canvasH - padding) / boundsH;
    m_zoom = (std::max)(0.75f, (std::min)((std::min)(fitZoomX, fitZoomY), 1.45f));
    m_pan.x = (canvasW * 0.5f) - ((minX + boundsW * 0.5f) * m_zoom);
    m_pan.y = (canvasH * 0.5f) - ((minY + boundsH * 0.5f) * m_zoom);
}

void BlueprintEditor::OpenCreateMenu(float screenX, float screenY, float canvasX, float canvasY) {
    m_showCreateMenu = true;
    m_createMenuScreenX = screenX;
    m_createMenuScreenY = screenY;
    m_createMenuGraphX = ScreenToGraph(screenX, canvasX, m_pan.x, m_zoom);
    m_createMenuGraphY = ScreenToGraph(screenY, canvasY, m_pan.y, m_zoom);
    m_createSearch.clear();
    m_createSearchActive = true;
}

bool BlueprintEditor::SpawnNodeFromCreateMenu(const std::string& itemId) {
    int createdNodeId = -1;

    if (itemId == "comment") {
        createdNodeId = AddCommentNode("Comment", m_createMenuGraphX - 160.0f, m_createMenuGraphY - 90.0f);
    } else if (itemId == "event:widget_construct") {
        Node node;
        node.id = NextNodeId();
        node.title = "Event Construct";
        node.subtitle = "Widget Event";
        node.x = m_createMenuGraphX - 110.0f;
        node.y = m_createMenuGraphY - 60.0f;
        node.width = 220.0f;
        node.height = 120.0f;
        node.visual = NodeVisualKind::Event;
        node.nodeTypeId = BlueprintNodes::kWidgetConstructNodeId;
        node.canDelete = true;
        createdNodeId = node.id;
        m_nodes.push_back(node);
    } else if (itemId == "event:beginplay") {
        Node node;
        node.id = NextNodeId();
        node.title = "Event BeginPlay";
        node.subtitle = "Scene Event";
        node.x = m_createMenuGraphX - 110.0f;
        node.y = m_createMenuGraphY - 60.0f;
        node.width = 220.0f;
        node.height = 120.0f;
        node.visual = NodeVisualKind::Event;
        node.nodeTypeId = "Event.BeginPlay";
        node.canDelete = true;
        createdNodeId = node.id;
        m_nodes.push_back(node);
    } else if (itemId == "event:physics_tick") {
        Node node;
        node.id = NextNodeId();
        node.title = "Event PhysicsTick";
        node.subtitle = "Simulation Event";
        node.x = m_createMenuGraphX - 110.0f;
        node.y = m_createMenuGraphY - 60.0f;
        node.width = 220.0f;
        node.height = 120.0f;
        node.visual = NodeVisualKind::Event;
        node.nodeTypeId = "Event.PhysicsTick";
        node.canDelete = true;
        createdNodeId = node.id;
        m_nodes.push_back(node);
    } else if (itemId == "function:apply_exposed_physics") {
        Node node;
        node.id = NextNodeId();
        node.title = "Apply Exposed Physics";
        node.subtitle = "Authoring Preview";
        node.x = m_createMenuGraphX - 126.0f;
        node.y = m_createMenuGraphY - 75.0f;
        node.width = 252.0f;
        node.height = 150.0f;
        node.visual = NodeVisualKind::Function;
        node.nodeTypeId = "Function.ApplyExposedPhysics";
        node.canDelete = true;
        createdNodeId = node.id;
        m_nodes.push_back(node);
    } else if (itemId == "function:refresh_preview") {
        Node node;
        node.id = NextNodeId();
        node.title = "Refresh Preview Data";
        node.subtitle = "Editor Utility";
        node.x = m_createMenuGraphX - 126.0f;
        node.y = m_createMenuGraphY - 75.0f;
        node.width = 252.0f;
        node.height = 150.0f;
        node.visual = NodeVisualKind::Function;
        node.nodeTypeId = "Function.RefreshPreviewData";
        node.canDelete = true;
        createdNodeId = node.id;
        m_nodes.push_back(node);
    } else if (itemId == "function:set_text_color") {
        Node node;
        node.id = NextNodeId();
        node.title = "Set Text Color";
        node.subtitle = "UI Function";
        node.x = m_createMenuGraphX - 126.0f;
        node.y = m_createMenuGraphY - 75.0f;
        node.width = 252.0f;
        node.height = 152.0f;
        node.visual = NodeVisualKind::Function;
        node.nodeTypeId = BlueprintNodes::kSetTextColorNodeId;
        node.tint = UIntColorToFloat4(0xFFFFC857);
        node.canDelete = true;
        createdNodeId = node.id;
        m_nodes.push_back(node);
    } else if (const BlueprintNodes::BlueprintNodeTemplate* gameplayNode = BlueprintNodes::FindBlueprintNodeTemplate(itemId)) {
        Node node;
        node.id = NextNodeId();
        node.title = gameplayNode->title;
        node.subtitle = gameplayNode->subtitle;
        node.x = m_createMenuGraphX - gameplayNode->width * 0.5f;
        node.y = m_createMenuGraphY - gameplayNode->height * 0.5f;
        node.width = gameplayNode->width;
        node.height = gameplayNode->height;
        node.visual = NodeVisualKind::Function;
        node.nodeTypeId = gameplayNode->typeId;
        node.canDelete = true;
        createdNodeId = node.id;
        m_nodes.push_back(node);
    } else if (itemId.rfind("component:", 0) == 0) {
        const int componentId = std::atoi(itemId.c_str() + 10);
        if (const BlueprintComponent* component = FindComponent(componentId)) {
            createdNodeId = AddComponentNode(*component, m_createMenuGraphX - 122.0f, m_createMenuGraphY - 62.0f);
        }
    } else if (itemId.rfind("field:", 0) == 0) {
        const size_t firstSeparator = itemId.find(':', 6);
        if (firstSeparator != std::string::npos) {
            const std::string category = itemId.substr(6, firstSeparator - 6);
            const std::string fieldName = itemId.substr(firstSeparator + 1);
            const BlueprintFieldDescriptor* descriptor = ResolveFieldDescriptor(category, fieldName);
            if (descriptor != nullptr) {
                createdNodeId = AddFieldNode(*descriptor, m_createMenuGraphX - 125.0f, m_createMenuGraphY - 70.0f);
            }
        }
    }

    if (createdNodeId == -1) {
        return false;
    }

    m_selectedNodeId = createdNodeId;
    const Node* createdNode = FindNode(createdNodeId);
    SetStatus(createdNode != nullptr ? ("Added " + createdNode->title + ".")
                                     : "Added a new graph node.",
              0xFFB7BEC8, 2200);
    return true;
}

const BlueprintFieldDescriptor* BlueprintEditor::ResolveFieldDescriptor(const std::string& category, const std::string& name) const {
    for (const BlueprintFieldDescriptor& field : GetRigidBodyBlueprintFields()) {
        if (category == field.category && name == field.name) {
            return &field;
        }
    }
    for (const BlueprintFieldDescriptor& field : GetColliderBlueprintFields()) {
        if (category == field.category && name == field.name) {
            return &field;
        }
    }
    return nullptr;
}

void BlueprintEditor::RefreshAssetBrowserItems() {
    m_assetBrowserItems.clear();
    m_assetBrowserDirty = false;

    if (IsUIAsset()) {
        return;
    }

    const std::wstring assetsRoot = GetProjectAssetsRoot();
    if (assetsRoot.empty()) {
        return;
    }

    std::error_code ec;
    if (!fs::exists(assetsRoot, ec) || ec) {
        return;
    }

    fs::recursive_directory_iterator iter(assetsRoot, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator endIter;
    while (!ec && iter != endIter) {
        const fs::directory_entry& entry = *iter;
        std::error_code entryError;
        if (entry.is_regular_file(entryError) && !entryError && IsUIBlueprintPath(entry.path().wstring())) {
            AssetBrowserItem item;
            item.path = entry.path().lexically_normal().wstring();
            item.label = entry.path().stem().string();
            item.subtitle = entry.path().parent_path().filename().string();
            m_assetBrowserItems.push_back(item);
        }
        iter.increment(ec);
    }

    std::sort(m_assetBrowserItems.begin(), m_assetBrowserItems.end(), [](const AssetBrowserItem& left, const AssetBrowserItem& right) {
        return ToLowerCopy(left.label) < ToLowerCopy(right.label);
    });
}

bool BlueprintEditor::TryAssignDraggedAsset(float screenX, float screenY, float canvasX, float canvasY) {
    if (m_draggingAssetBrowserIndex < 0 ||
        m_draggingAssetBrowserIndex >= static_cast<int>(m_assetBrowserItems.size())) {
        return false;
    }

    const std::wstring assetPath = m_assetBrowserItems[static_cast<size_t>(m_draggingAssetBrowserIndex)].path;
    PinReference hitPin = HitTestPin(screenX, screenY, canvasX, canvasY);
    if (hitPin.nodeId >= 0 && hitPin.kind == PinKind::Data && hitPin.direction == PinDirection::Input) {
        Node* hitNode = FindNode(hitPin.nodeId);
        if (hitNode != nullptr && NodeAcceptsAssetDrop(*hitNode)) {
            return AssignAssetToNode(*hitNode, assetPath);
        }
    }

    Node* hitNode = HitTestNode(screenX, screenY, canvasX, canvasY);
    if (hitNode != nullptr && NodeAcceptsAssetDrop(*hitNode)) {
        return AssignAssetToNode(*hitNode, assetPath);
    }

    return false;
}

bool BlueprintEditor::AssignAssetToNode(Node& node, const std::wstring& assetPath) {
    if (!NodeAcceptsAssetDrop(node)) {
        return false;
    }

    node.assetPath = NormalizeAssetPath(assetPath);
    if (node.assetPath.empty()) {
        return false;
    }

    SetStatus("Assigned " + fs::path(node.assetPath).filename().string() + " to " + node.title + ".",
              0xFF89D185, 2600);
    return true;
}

bool BlueprintEditor::NodeAcceptsAssetDrop(const Node& node) const {
    return IsCreateWidgetNodeType(node.nodeTypeId);
}

std::wstring BlueprintEditor::GetProjectAssetsRoot() const {
    std::wstring sourcePath = m_assetPath;
    if (sourcePath.empty()) {
        return L"";
    }

    fs::path current = fs::path(sourcePath).parent_path();
    while (!current.empty()) {
        std::wstring folderName = current.filename().wstring();
        std::transform(folderName.begin(), folderName.end(), folderName.begin(), towlower);
        if (folderName == L"assets") {
            return current.wstring();
        }

        fs::path parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }

    return fs::path(sourcePath).parent_path().wstring();
}

std::string BlueprintEditor::BuildFieldPreview(const GameObject* selectedObject, const BlueprintFieldDescriptor& field) const {
    if (selectedObject == nullptr) {
        return HasOpenAsset() ? "Asset default" : "No actor selected";
    }

    const char* rawFieldData = nullptr;
    if (std::strcmp(field.category, "Physics.RigidBody") == 0) {
        rawFieldData = reinterpret_cast<const char*>(&selectedObject->physics.rigidBody) + field.offset;
    } else if (std::strcmp(field.category, "Physics.Collider") == 0) {
        rawFieldData = reinterpret_cast<const char*>(&selectedObject->physics.collider) + field.offset;
    }

    if (rawFieldData == nullptr) {
        return "Unknown binding";
    }

    char buffer[96];
    switch (field.type) {
    case BlueprintFieldType::Bool:
        return (*reinterpret_cast<const bool*>(rawFieldData)) ? "True" : "False";
    case BlueprintFieldType::Float:
        std::snprintf(buffer, sizeof(buffer), "%.2f", *reinterpret_cast<const float*>(rawFieldData));
        return buffer;
    case BlueprintFieldType::Float3: {
        const DirectX::XMFLOAT3& value = *reinterpret_cast<const DirectX::XMFLOAT3*>(rawFieldData);
        std::snprintf(buffer, sizeof(buffer), "%.2f, %.2f, %.2f", value.x, value.y, value.z);
        return buffer;
    }
    case BlueprintFieldType::Enum:
    default:
        if (std::strcmp(field.category, "Physics.RigidBody") == 0) {
            return PhysicsBodyTypeToString(*reinterpret_cast<const PhysicsBodyType*>(rawFieldData));
        }
        if (std::strcmp(field.category, "Physics.Collider") == 0) {
            return PhysicsColliderShapeToString(*reinterpret_cast<const PhysicsColliderShape*>(rawFieldData));
        }
        return "Enum";
    }
}

std::string BlueprintEditor::BuildFieldTypeLabel(BlueprintFieldType type) const {
    switch (type) {
    case BlueprintFieldType::Bool:
        return "Bool";
    case BlueprintFieldType::Float3:
        return "Vector3";
    case BlueprintFieldType::Enum:
        return "Enum";
    case BlueprintFieldType::Float:
    default:
        return "Float";
    }
}

std::string BlueprintEditor::BuildComponentTypeLabel(ComponentKind kind) const {
    switch (kind) {
    case ComponentKind::SkeletalMesh:
        return "Skeletal Mesh";
    case ComponentKind::Camera:
        return "Camera";
    case ComponentKind::Trigger:
        return "Trigger";
    case ComponentKind::StaticMesh:
    default:
        return "Static Mesh";
    }
}

std::string BlueprintEditor::BuildComponentSummary(const BlueprintComponent& component) const {
    switch (component.kind) {
    case ComponentKind::Camera:
        return component.possessOnPlay ? "Owns the play camera" : "Preview camera";
    case ComponentKind::Trigger:
        return std::string(PhysicsColliderShapeToString(component.triggerShape)) + " trigger";
    case ComponentKind::SkeletalMesh:
    case ComponentKind::StaticMesh:
    default:
        return component.assetPath.empty() ? "No actor asset assigned" : fs::path(component.assetPath).stem().string();
    }
}

std::string BlueprintEditor::BuildUIElementTypeLabel(UIElementKind kind) const {
    switch (kind) {
    case UIElementKind::Canvas:
        return "Canvas";
    case UIElementKind::Button:
        return "Button";
    case UIElementKind::Image:
        return "Image";
    case UIElementKind::TextBlock:
        return "TextBlock";
    case UIElementKind::None:
    default:
        return "UI Element";
    }
}

std::string BlueprintEditor::BuildTargetLabel(const GameObject* targetObject) const {
    if (!m_assetPath.empty()) {
        return fs::path(m_assetPath).stem().string();
    }
    if (targetObject == nullptr || targetObject->name.empty()) {
        return "Level Blueprint";
    }
    return targetObject->name;
}

std::string BlueprintEditor::BuildVisualTypeLabel(NodeVisualKind visual) const {
    switch (visual) {
    case NodeVisualKind::Event:
        return "Events";
    case NodeVisualKind::Field:
        return "Variables";
    case NodeVisualKind::Component:
        return "Components";
    case NodeVisualKind::UIElement:
        return "UI Elements";
    case NodeVisualKind::Comment:
        return "Comments";
    case NodeVisualKind::Function:
    default:
        return "Functions";
    }
}

void BlueprintEditor::SetStatus(const std::string& message, uint32_t color, DWORD durationMs) {
    m_statusMessage = message;
    m_statusColor = color;
    m_statusUntil = GetTickCount() + durationMs;
}

int BlueprintEditor::NextNodeId() {
    return m_nextNodeId++;
}

int BlueprintEditor::NextComponentId() {
    return m_nextComponentId++;
}
