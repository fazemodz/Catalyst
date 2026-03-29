#include "DXRenderer.h"
#include <d3dcompiler.h>
#include <iostream>
#include <DirectXCollision.h> 
#include <algorithm>
#include <cfloat>
#include <cwctype>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include "PrimitiveGenerator.h"
#include "ModelLoader.h"
#include "../Blueprint/Nodes/BlueprintNodeLibrary.h"
#include "../Launcher.h" 

using namespace DirectX;
namespace fs = std::filesystem;

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

extern InputManager* g_InputManager;
static uint32_t g_skyboxSrvIndex = static_cast<uint32_t>(-1); 

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

    fs::path normalized = absolutePath.lexically_normal();
    return normalized.wstring();
}

std::wstring FindProjectRootFromAssetPath(const std::wstring& assetPath) {
    fs::path current = fs::path(assetPath).parent_path();
    while (!current.empty()) {
        std::wstring folderName = current.filename().wstring();
        std::transform(folderName.begin(), folderName.end(), folderName.begin(), towlower);
        if (folderName == L"assets") {
            return current.parent_path().wstring();
        }

        fs::path parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }

    return fs::path(assetPath).parent_path().wstring();
}

bool RayIntersectsSphere(const XMFLOAT3& origin, const XMFLOAT3& direction,
                         const XMFLOAT3& center, float radius, float& outDistance) {
    const XMVECTOR rayOrigin = XMLoadFloat3(&origin);
    const XMVECTOR rayDirection = XMLoadFloat3(&direction);
    const XMVECTOR sphereCenter = XMLoadFloat3(&center);

    const XMVECTOR oc = XMVectorSubtract(rayOrigin, sphereCenter);
    const float b = 2.0f * XMVectorGetX(XMVector3Dot(oc, rayDirection));
    const float c = XMVectorGetX(XMVector3Dot(oc, oc)) - radius * radius;
    const float discriminant = b * b - 4.0f * c;
    if (discriminant < 0.0f) {
        return false;
    }

    const float sqrtDisc = sqrtf(discriminant);
    const float t0 = (-b - sqrtDisc) * 0.5f;
    const float t1 = (-b + sqrtDisc) * 0.5f;
    const float hitDistance = (t0 > 0.0f) ? t0 : t1;
    if (hitDistance <= 0.0f) {
        return false;
    }

    outDistance = hitDistance;
    return true;
}

bool IsPointInRect(float px, float py, float x, float y, float width, float height) {
    return px >= x && px <= (x + width) && py >= y && py <= (y + height);
}

MeshData ParseActorAssetForPreview(const std::wstring& filepath) {
    try {
        return ModelLoader::LoadMeshData(filepath);
    } catch (...) {
        return {};
    }
}

float ComputePreviewRadius(const DirectX::XMFLOAT3& extents) {
    return (std::max)(0.35f, sqrtf(extents.x * extents.x + extents.y * extents.y + extents.z * extents.z));
}

void SetCursorVisibleState(bool visible) {
    if (visible) {
        while (ShowCursor(TRUE) < 0) {
        }
    } else {
        while (ShowCursor(FALSE) >= 0) {
        }
    }
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

std::string EscapeJsonString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());

    for (char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += ch;
            break;
        }
    }

    return escaped;
}

void DebugLog(const std::string& message) {
    OutputDebugStringA((message + "\n").c_str());
}

std::string NormalizeLinkedMaterialPath(const std::string& path) {
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    return normalized;
}

std::wstring JoinHumanList(const std::vector<std::wstring>& items) {
    if (items.empty()) {
        return L"";
    }
    if (items.size() == 1) {
        return items.front();
    }
    if (items.size() == 2) {
        return items[0] + L" and " + items[1];
    }

    std::wstring joined;
    for (size_t index = 0; index < items.size(); ++index) {
        if (index > 0) {
            joined += (index + 1 == items.size()) ? L", and " : L", ";
        }
        joined += items[index];
    }
    return joined;
}

std::wstring MakeProjectRelativePath(const std::wstring& projectRoot, const std::wstring& path) {
    if (path.empty()) {
        return L"";
    }

    std::error_code ec;
    fs::path absolutePath = fs::path(path);
    if (!absolutePath.is_absolute()) {
        absolutePath = fs::absolute(absolutePath, ec);
        if (ec) {
            absolutePath = fs::path(path);
        }
    }

    if (projectRoot.empty()) {
        return absolutePath.lexically_normal().generic_wstring();
    }

    fs::path relativePath = fs::relative(absolutePath, fs::path(projectRoot), ec);
    return (ec ? absolutePath.lexically_normal() : relativePath.lexically_normal()).generic_wstring();
}

std::wstring ResolveSceneReferencePath(const std::wstring& projectRoot, const std::string& storedPath) {
    if (storedPath.empty()) {
        return L"";
    }

    fs::path resolved(DecodeStoredPath(storedPath));
    if (!resolved.is_absolute() && !projectRoot.empty()) {
        resolved = fs::path(projectRoot) / resolved;
    }

    return resolved.lexically_normal().wstring();
}

const char* ObjectTypeToString(ObjectType type) {
    switch (type) {
    case ObjectType::Light:
        return "Light";
    case ObjectType::Skybox:
        return "Skybox";
    case ObjectType::PostProcessVolume:
        return "PostProcessVolume";
    case ObjectType::Mesh:
    default:
        return "Mesh";
    }
}

ObjectType ObjectTypeFromString(const std::string& type) {
    if (type == "Light") {
        return ObjectType::Light;
    }
    if (type == "Skybox") {
        return ObjectType::Skybox;
    }
    if (type == "PostProcessVolume") {
        return ObjectType::PostProcessVolume;
    }
    return ObjectType::Mesh;
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
    return (field && field->type == JsonValueType::String) ? field->stringValue : fallback;
}

float GetJsonNumber(const JsonValue& objectValue, const char* key, float fallback) {
    const JsonValue* field = FindJsonField(objectValue, key);
    return (field && field->type == JsonValueType::Number) ? static_cast<float>(field->numberValue) : fallback;
}

int GetJsonInt(const JsonValue& objectValue, const char* key, int fallback) {
    const JsonValue* field = FindJsonField(objectValue, key);
    return (field && field->type == JsonValueType::Number) ? static_cast<int>(field->numberValue) : fallback;
}

bool GetJsonBool(const JsonValue& objectValue, const char* key, bool fallback) {
    const JsonValue* field = FindJsonField(objectValue, key);
    return (field && field->type == JsonValueType::Bool) ? field->boolValue : fallback;
}

DirectX::XMFLOAT3 GetJsonFloat3(const JsonValue& objectValue, const char* key, const DirectX::XMFLOAT3& fallback) {
    const JsonValue* field = FindJsonField(objectValue, key);
    if (!field || field->type != JsonValueType::Array || field->arrayValue.size() != 3) {
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
    if (!field || field->type != JsonValueType::Array || field->arrayValue.size() != 4) {
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

std::wstring ResolveBlueprintReferencePath(const std::wstring& blueprintAssetPath, const std::string& storedPath) {
    const std::wstring decodedPath = DecodeStoredPath(storedPath);
    if (decodedPath.empty()) {
        return L"";
    }

    fs::path resolvedPath(decodedPath);
    if (resolvedPath.is_relative() && !blueprintAssetPath.empty()) {
        resolvedPath = fs::path(blueprintAssetPath).parent_path() / resolvedPath;
    }

    return NormalizeAssetPath(resolvedPath.wstring());
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

bool IsUIButtonNodeType(const std::string& nodeTypeId) {
    return nodeTypeId == BlueprintNodes::kButtonElementNodeId;
}

bool IsUICanvasNodeType(const std::string& nodeTypeId) {
    return nodeTypeId == BlueprintNodes::kCanvasElementNodeId;
}

bool IsUIImageNodeType(const std::string& nodeTypeId) {
    return nodeTypeId == BlueprintNodes::kImageElementNodeId;
}

bool IsUITextBlockNodeType(const std::string& nodeTypeId) {
    return nodeTypeId == BlueprintNodes::kTextBlockElementNodeId;
}

bool IsUIElementNodeType(const std::string& nodeTypeId) {
    return IsUICanvasNodeType(nodeTypeId) ||
           IsUIButtonNodeType(nodeTypeId) ||
           IsUIImageNodeType(nodeTypeId) ||
           IsUITextBlockNodeType(nodeTypeId);
}

struct BlueprintRuntimeComponent {
    std::string kind = "StaticMesh";
    std::wstring assetPath;
    DirectX::XMFLOAT3 location = {0.0f, 0.0f, 0.0f};
    bool possessOnPlay = false;
    PhysicsColliderShape triggerShape = PhysicsColliderShape::Box;
    DirectX::XMFLOAT3 triggerExtents = {0.75f, 0.75f, 0.75f};
    float triggerRadius = 0.75f;
};

struct BlueprintRuntimeNode {
    std::string nodeTypeId;
    std::wstring assetPath;
};

struct BlueprintRuntimeData {
    bool playerCharacterController = false;
    bool playerSpaceJump = false;
    float playerMoveSpeed = 6.0f;
    float playerJumpImpulse = 5.0f;
    std::vector<BlueprintRuntimeComponent> components;
    std::vector<std::wstring> viewportWidgetAssetPaths;
};

bool LoadBlueprintRuntimeData(const std::wstring& assetPath, BlueprintRuntimeData& outRuntimeData) {
    outRuntimeData = {};
    if (assetPath.empty()) {
        return false;
    }

    std::ifstream inputFile(fs::path(assetPath), std::ios::binary);
    if (!inputFile.is_open()) {
        return false;
    }

    const std::string content((std::istreambuf_iterator<char>(inputFile)), std::istreambuf_iterator<char>());
    JsonValue rootValue;
    JsonParser parser(content);
    if (!parser.Parse(rootValue) || rootValue.type != JsonValueType::Object) {
        return false;
    }

    std::map<int, BlueprintRuntimeNode> runtimeNodes;
    const JsonValue* nodesValue = FindJsonField(rootValue, "nodes");
    if (nodesValue != nullptr && nodesValue->type == JsonValueType::Array) {
        for (const JsonValue& nodeValue : nodesValue->arrayValue) {
            if (nodeValue.type != JsonValueType::Object) {
                continue;
            }

            const int nodeId = GetJsonInt(nodeValue, "id", 0);
            const std::string nodeTypeId = GetJsonString(nodeValue, "nodeTypeId");
            runtimeNodes[nodeId] = {
                nodeTypeId,
                ResolveBlueprintReferencePath(assetPath, GetJsonString(nodeValue, "assetPath"))
            };
            if (nodeTypeId == BlueprintNodes::kPlayerCharacterControllerNodeId) {
                outRuntimeData.playerCharacterController = true;
            } else if (nodeTypeId == BlueprintNodes::kPlayerSpaceJumpNodeId) {
                outRuntimeData.playerSpaceJump = true;
            }
        }
    }

    const JsonValue* linksValue = FindJsonField(rootValue, "links");
    if (linksValue != nullptr && linksValue->type == JsonValueType::Array) {
        std::map<std::pair<int, int>, bool> createWidgetExecLinks;
        std::map<std::pair<int, int>, bool> createWidgetDataLinks;

        for (const JsonValue& linkValue : linksValue->arrayValue) {
            if (linkValue.type != JsonValueType::Object) {
                continue;
            }

            const int fromNodeId = GetJsonInt(linkValue, "fromNodeId", 0);
            const int toNodeId = GetJsonInt(linkValue, "toNodeId", 0);
            const auto fromNode = runtimeNodes.find(fromNodeId);
            const auto toNode = runtimeNodes.find(toNodeId);
            if (fromNode == runtimeNodes.end() || toNode == runtimeNodes.end()) {
                continue;
            }

            if (fromNode->second.nodeTypeId != BlueprintNodes::kCreateWidgetNodeId ||
                toNode->second.nodeTypeId != BlueprintNodes::kAddToViewportNodeId) {
                continue;
            }

            const std::pair<int, int> linkKey = {fromNodeId, toNodeId};
            const std::string fromPinKind = GetJsonString(linkValue, "fromPinKind", "Exec");
            const std::string toPinKind = GetJsonString(linkValue, "toPinKind", "Exec");
            if (fromPinKind == "Exec" && toPinKind == "Exec") {
                createWidgetExecLinks[linkKey] = true;
            } else if (fromPinKind == "Data" && toPinKind == "Data") {
                createWidgetDataLinks[linkKey] = true;
            }
        }

        for (const auto& createWidgetDataLink : createWidgetDataLinks) {
            if (createWidgetExecLinks.find(createWidgetDataLink.first) == createWidgetExecLinks.end()) {
                continue;
            }

            const auto createNode = runtimeNodes.find(createWidgetDataLink.first.first);
            if (createNode == runtimeNodes.end() || createNode->second.assetPath.empty()) {
                continue;
            }

            if (std::find(outRuntimeData.viewportWidgetAssetPaths.begin(),
                          outRuntimeData.viewportWidgetAssetPaths.end(),
                          createNode->second.assetPath) == outRuntimeData.viewportWidgetAssetPaths.end()) {
                outRuntimeData.viewportWidgetAssetPaths.push_back(createNode->second.assetPath);
            }
        }
    }

    const JsonValue* componentsValue = FindJsonField(rootValue, "components");
    if (componentsValue != nullptr && componentsValue->type == JsonValueType::Array) {
        outRuntimeData.components.reserve(componentsValue->arrayValue.size());
        for (const JsonValue& componentValue : componentsValue->arrayValue) {
            if (componentValue.type != JsonValueType::Object) {
                continue;
            }

            BlueprintRuntimeComponent component;
            component.kind = GetJsonString(componentValue, "kind", "StaticMesh");
            component.assetPath = ResolveBlueprintReferencePath(assetPath, GetJsonString(componentValue, "assetPath"));
            component.location = GetJsonFloat3(componentValue, "location", {0.0f, 0.0f, 0.0f});
            component.possessOnPlay = GetJsonBool(componentValue, "possessOnPlay", false);
            component.triggerShape = PhysicsColliderShapeFromString(GetJsonString(componentValue, "triggerShape", "Box"));
            component.triggerExtents = GetJsonFloat3(componentValue, "triggerExtents", {0.75f, 0.75f, 0.75f});
            component.triggerRadius = GetJsonNumber(componentValue, "triggerRadius", 0.75f);
            outRuntimeData.components.push_back(component);
        }
    }

    return true;
}
}

std::vector<ProjectInfo> DXRenderer::GetRecentProjectsInfo() {
    return ::GetRecentProjectsInfo();
}

int DXRenderer::GetNextAvailableAssetId() const {
    int maxAssetId = -1;
    for (const auto& asset : m_assets) {
        if (asset) {
            maxAssetId = (std::max)(maxAssetId, asset->id);
        }
    }
    return maxAssetId + 1;
}

std::wstring DXRenderer::ResolveActiveProjectFilePath() const {
    if (!m_editorUI.State.currentProjectFile.empty()) {
        return NormalizeAssetPath(m_editorUI.State.currentProjectFile);
    }

    if (m_editorUI.State.currentProjectFolder.empty() || !fs::exists(m_editorUI.State.currentProjectFolder)) {
        return L"";
    }

    for (const auto& entry : fs::directory_iterator(m_editorUI.State.currentProjectFolder)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        std::wstring extension = entry.path().extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
        if (extension == L".catalystproj") {
            return entry.path().lexically_normal().wstring();
        }
    }

    return L"";
}

std::wstring DXRenderer::ResolveActiveProjectDisplayName() const {
    if (!m_editorUI.State.editorSwapProjName.empty()) {
        return m_editorUI.State.editorSwapProjName;
    }

    const std::wstring projectFilePath = ResolveActiveProjectFilePath();
    if (!projectFilePath.empty()) {
        return fs::path(projectFilePath).stem().wstring();
    }

    return L"Untitled Project";
}

std::wstring DXRenderer::ResolveActiveMapDisplayName() const {
    if (!m_editorUI.State.currentMapPath.empty()) {
        return fs::path(m_editorUI.State.currentMapPath).stem().wstring();
    }

    const std::wstring projectFilePath = ResolveActiveProjectFilePath();
    if (!projectFilePath.empty()) {
        const std::wstring startupScenePath = ResolveProjectStartupScenePath(projectFilePath);
        if (!startupScenePath.empty()) {
            return fs::path(startupScenePath).stem().wstring();
        }
    }

    return L"Untitled Map";
}

std::string DXRenderer::BuildSceneDocument(const std::vector<GameObject>& objectsToSave, const std::wstring& projectRoot) const {
    std::ostringstream outFile;
    outFile << std::fixed << std::setprecision(6);
    outFile << "{\n";
    outFile << "  \"type\": \"CatalystScene\",\n";
    outFile << "  \"version\": 2,\n";
    outFile << "  \"objects\": [\n";

    for (size_t objectIndex = 0; objectIndex < objectsToSave.size(); ++objectIndex) {
        const GameObject& object = objectsToSave[objectIndex];

        const std::wstring assetSourcePath =
            (object.asset && !object.asset->sourcePath.empty())
            ? MakeProjectRelativePath(projectRoot, DecodeStoredPath(object.asset->sourcePath))
            : L"";
        const std::wstring blueprintAssetPath =
            object.blueprintAssetPath.empty()
            ? L""
            : MakeProjectRelativePath(projectRoot, object.blueprintAssetPath);
        const std::wstring assignedMaterialPath =
            object.assignedMaterial ? MakeProjectRelativePath(projectRoot, GetCachedMaterialPath(object.assignedMaterial)) : L"";

        outFile << "    {\n";
        outFile << "      \"name\": \"" << EscapeJsonString(object.name) << "\",\n";
        outFile << "      \"type\": \"" << ObjectTypeToString(object.type) << "\",\n";
        outFile << "      \"assetId\": " << (object.asset ? object.asset->id : -1) << ",\n";
        outFile << "      \"assetName\": \"" << EscapeJsonString(object.asset ? object.asset->name : "") << "\",\n";
        outFile << "      \"assetSource\": \"" << EscapeJsonString(WideToUtf8(assetSourcePath)) << "\",\n";
        outFile << "      \"blueprintAsset\": \"" << EscapeJsonString(WideToUtf8(blueprintAssetPath)) << "\",\n";
        outFile << "      \"position\": ";
        WriteJsonFloat3(outFile, object.position);
        outFile << ",\n";
        outFile << "      \"rotation\": ";
        WriteJsonFloat3(outFile, object.rotation);
        outFile << ",\n";
        outFile << "      \"scale\": ";
        WriteJsonFloat3(outFile, object.scale);
        outFile << ",\n";
        outFile << "      \"color\": ";
        WriteJsonFloat4(outFile, object.color);
        outFile << ",\n";
        outFile << "      \"skyHorizonColor\": ";
        WriteJsonFloat4(outFile, object.skyHorizonColor);
        outFile << ",\n";
        outFile << "      \"assignedMaterial\": \"" << EscapeJsonString(WideToUtf8(assignedMaterialPath)) << "\",\n";
        outFile << "      \"overrideTextures\": {\n";
        outFile << "        \"albedo\": \"" << EscapeJsonString(WideToUtf8(MakeProjectRelativePath(projectRoot, GetCachedTexturePath(object.overrideAlbedo)))) << "\",\n";
        outFile << "        \"normal\": \"" << EscapeJsonString(WideToUtf8(MakeProjectRelativePath(projectRoot, GetCachedTexturePath(object.overrideNormal)))) << "\",\n";
        outFile << "        \"metallic\": \"" << EscapeJsonString(WideToUtf8(MakeProjectRelativePath(projectRoot, GetCachedTexturePath(object.overrideMetallic)))) << "\",\n";
        outFile << "        \"roughness\": \"" << EscapeJsonString(WideToUtf8(MakeProjectRelativePath(projectRoot, GetCachedTexturePath(object.overrideRoughness)))) << "\",\n";
        outFile << "        \"ao\": \"" << EscapeJsonString(WideToUtf8(MakeProjectRelativePath(projectRoot, GetCachedTexturePath(object.overrideAO)))) << "\"\n";
        outFile << "      },\n";
        outFile << "      \"lightIntensity\": " << object.lightIntensity << ",\n";
        outFile << "      \"postProcess\": {\n";
        outFile << "        \"exposure\": " << object.ppSettings.exposure << ",\n";
        outFile << "        \"colorTint\": ";
        WriteJsonFloat3(outFile, object.ppSettings.colorTint);
        outFile << ",\n";
        outFile << "        \"bloomThreshold\": " << object.ppSettings.bloomThreshold << ",\n";
        outFile << "        \"bloomIntensity\": " << object.ppSettings.bloomIntensity << ",\n";
        outFile << "        \"blendRadius\": " << object.ppSettings.blendRadius << "\n";
        outFile << "      },\n";
        outFile << "      \"physics\": {\n";
        outFile << "        \"rigidBody\": {\n";
        outFile << "          \"enabled\": " << (object.physics.rigidBody.enabled ? "true" : "false") << ",\n";
        outFile << "          \"bodyType\": \"" << PhysicsBodyTypeToString(object.physics.rigidBody.bodyType) << "\",\n";
        outFile << "          \"useGravity\": " << (object.physics.rigidBody.useGravity ? "true" : "false") << ",\n";
        outFile << "          \"mass\": " << object.physics.rigidBody.mass << ",\n";
        outFile << "          \"linearDamping\": " << object.physics.rigidBody.linearDamping << ",\n";
        outFile << "          \"restitution\": " << object.physics.rigidBody.restitution << ",\n";
        outFile << "          \"velocity\": ";
        WriteJsonFloat3(outFile, object.physics.rigidBody.velocity);
        outFile << "\n";
        outFile << "        },\n";
        outFile << "        \"collider\": {\n";
        outFile << "          \"enabled\": " << (object.physics.collider.enabled ? "true" : "false") << ",\n";
        outFile << "          \"shape\": \"" << PhysicsColliderShapeToString(object.physics.collider.shape) << "\",\n";
        outFile << "          \"isTrigger\": " << (object.physics.collider.isTrigger ? "true" : "false") << ",\n";
        outFile << "          \"centerOffset\": ";
        WriteJsonFloat3(outFile, object.physics.collider.centerOffset);
        outFile << ",\n";
        outFile << "          \"boxExtents\": ";
        WriteJsonFloat3(outFile, object.physics.collider.boxExtents);
        outFile << ",\n";
        outFile << "          \"sphereRadius\": " << object.physics.collider.sphereRadius << "\n";
        outFile << "        }\n";
        outFile << "      }\n";
        outFile << "    " << (objectIndex + 1 < objectsToSave.size() ? "}," : "}");
        outFile << "\n";
    }

    outFile << "  ]\n";
    outFile << "}\n";
    return outFile.str();
}

std::string DXRenderer::BuildCurrentSceneDocument() const {
    const std::wstring projectFilePath = ResolveActiveProjectFilePath();
    if (projectFilePath.empty()) {
        return "";
    }

    const std::wstring projectRoot = fs::path(projectFilePath).parent_path().wstring();
    const std::vector<GameObject>& objectsToSave =
        (m_editorUI.State.isPlaying && !m_playModeSnapshot.empty()) ? m_playModeSnapshot : m_gameObjects;
    return BuildSceneDocument(objectsToSave, projectRoot);
}

std::string DXRenderer::BuildMaterialDocument(const Material& material) const {
    std::ostringstream file;
    file << "{\n";
    file << "  \"type\": \"CatalystMaterial\",\n";
    file << "  \"version\": 1,\n";
    file << "  \"textures\": {\n";
    file << "    \"albedo\": \"" << EscapeJsonString(NormalizeLinkedMaterialPath(material.albedoPath)) << "\",\n";
    file << "    \"normal\": \"" << EscapeJsonString(NormalizeLinkedMaterialPath(material.normalPath)) << "\",\n";
    file << "    \"roughness\": \"" << EscapeJsonString(NormalizeLinkedMaterialPath(material.roughnessPath)) << "\"\n";
    file << "  }\n";
    file << "}\n";
    return file.str();
}

bool DXRenderer::HasUnsavedSceneChanges() const {
    if (m_engineState != EngineState::Editor ||
        m_standaloneActorViewerWindow ||
        m_standaloneMaterialEditorWindow ||
        m_standaloneBlueprintEditorWindow) {
        return false;
    }

    const std::wstring projectFilePath = ResolveActiveProjectFilePath();
    if (projectFilePath.empty()) {
        return false;
    }

    return BuildCurrentSceneDocument() != m_savedSceneDocument;
}

bool DXRenderer::HasUnsavedMaterialEditorChanges() const {
    if (!m_editorUI.State.showMaterialAssetViewer || m_materialEditorMaterial == nullptr || m_editorUI.State.materialEditorPath.empty()) {
        return false;
    }

    return BuildMaterialDocument(*m_materialEditorMaterial) != m_savedMaterialDocument;
}

void DXRenderer::RefreshSceneSavedDocument() {
    m_savedSceneDocument = BuildCurrentSceneDocument();
}

void DXRenderer::RefreshMaterialEditorSavedDocument() {
    if (m_materialEditorMaterial == nullptr || m_editorUI.State.materialEditorPath.empty()) {
        m_savedMaterialDocument.clear();
        return;
    }

    m_savedMaterialDocument = BuildMaterialDocument(*m_materialEditorMaterial);
}

bool DXRenderer::SaveMaterialAssetEditor() {
    if (m_materialEditorMaterial == nullptr || m_editorUI.State.materialEditorPath.empty()) {
        return false;
    }

    if (!m_materialEditorMaterial->SaveToFile(m_editorUI.State.materialEditorPath)) {
        return false;
    }

    SyncMaterialTextures(*m_materialEditorMaterial);
    std::error_code ec;
    m_materialEditorMaterial->lastWriteTime = fs::last_write_time(m_editorUI.State.materialEditorPath, ec);
    RefreshMaterialEditorSavedDocument();
    return true;
}

bool DXRenderer::HasPendingUnsavedChanges() const {
    return HasUnsavedSceneChanges() || m_editorUI.HasUnsavedBlueprintChanges() || HasUnsavedMaterialEditorChanges();
}

std::wstring DXRenderer::GetUnsavedChangesDescription() const {
    return DescribeUnsavedChanges();
}

void DXRenderer::OpenClosePrompt(bool closeAllWindows, const std::wstring& summaryOverride) {
    m_showClosePrompt = true;
    m_closePromptCloseAllWindows = closeAllWindows;
    m_closePromptSummary = summaryOverride.empty() ? DescribeUnsavedChanges() : summaryOverride;
    m_pendingWindowCommand = WindowCommand::None;
    if (m_closePromptSummary.empty()) {
        m_closePromptSummary = closeAllWindows ? L"all open work" : L"this window";
    }
    m_closePromptError.clear();
}

bool DXRenderer::IsClosePromptOpen() const {
    return m_showClosePrompt;
}

void DXRenderer::SetClosePromptError(const std::wstring& errorMessage) {
    if (m_showClosePrompt) {
        m_closePromptError = errorMessage;
    }
}

bool DXRenderer::SavePendingUnsavedChanges() {
    return SavePendingOpenDocuments();
}

DXRenderer::WindowCommand DXRenderer::ConsumePendingWindowCommand() {
    const WindowCommand pendingCommand = m_pendingWindowCommand;
    m_pendingWindowCommand = WindowCommand::None;
    return pendingCommand;
}

std::wstring DXRenderer::DescribeUnsavedChanges() const {
    std::vector<std::wstring> items;
    if (HasUnsavedSceneChanges()) {
        items.push_back(L"map \"" + ResolveActiveMapDisplayName() + L"\"");
    }
    if (m_editorUI.HasUnsavedBlueprintChanges()) {
        items.push_back(L"Blueprint \"" + m_editorUI.GetOpenBlueprintDisplayName() + L"\"");
    }
    if (HasUnsavedMaterialEditorChanges()) {
        items.push_back(L"material \"" + Utf8ToWide(m_editorUI.State.materialEditorTitle) + L"\"");
    }

    return JoinHumanList(items);
}

bool DXRenderer::SavePendingOpenDocuments() {
    if (HasUnsavedSceneChanges() && !SaveCurrentScene()) {
        return false;
    }
    if (m_editorUI.HasUnsavedBlueprintChanges() && !m_editorUI.SaveBlueprintChanges()) {
        return false;
    }
    if (HasUnsavedMaterialEditorChanges() && !SaveMaterialAssetEditor()) {
        return false;
    }
    return true;
}

void DXRenderer::ClearClosePrompt() {
    m_showClosePrompt = false;
    m_closePromptCloseAllWindows = false;
    m_closePromptSummary.clear();
    m_closePromptError.clear();
}

void DXRenderer::DrawClosePrompt(float width, float height) {
    if (!m_showClosePrompt) {
        return;
    }

    const float popupW = std::clamp(width * 0.34f, 460.0f, 580.0f);
    const float popupH = m_closePromptError.empty() ? 250.0f : 290.0f;
    const float popupX = (width - popupW) * 0.5f;
    const float popupY = (height - popupH) * 0.5f;
    const float bodyX = popupX + 22.0f;
    const float bodyW = popupW - 44.0f;
    const float footerY = popupY + popupH - 68.0f;
    const float buttonY = popupY + popupH - 48.0f;
    const std::string title = m_closePromptCloseAllWindows
        ? "Close All Windows?"
        : "Close Window?";
    const std::string summary = WideToUtf8(m_closePromptSummary);
    const std::string intro = m_closePromptCloseAllWindows
        ? "Choose what to do with your unsaved work before Catalyst closes every open window."
        : "Choose what to do with your unsaved work before this window closes.";
    const std::string actionHint = "Save writes the current work to disk. Discard closes without saving.";
    const std::string errorText = WideToUtf8(m_closePromptError);

    m_uiDrawList.AddRectFilled(0.0f, 0.0f, width, height, 0xAA000000);
    m_uiDrawList.AddRectFilled(popupX, popupY, popupW, popupH, 0xFF1A1A1A);
    m_uiDrawList.AddRectFilled(popupX, popupY, popupW, 34.0f, 0xFF121212);
    m_uiDrawList.AddRectFilled(popupX, popupY, popupW, 3.0f, 0xFFD77800);
    m_uiDrawList.AddText(m_fontManager, title, popupX + 16.0f, popupY + 22.0f, 0xFFFFFFFF);
    m_uiDrawList.AddText(m_fontManager, intro, bodyX, popupY + 72.0f, 0xFFB7BEC8, bodyW);
    m_uiDrawList.AddText(m_fontManager, "Unsaved:", bodyX, popupY + 112.0f, 0xFFFFFFFF, bodyW);
    m_uiDrawList.AddText(m_fontManager, summary, bodyX, popupY + 140.0f, 0xFFE6E6E6, bodyW);
    m_uiDrawList.AddText(m_fontManager, actionHint, bodyX, popupY + 184.0f, 0xFF9FA8B3, bodyW);
    if (!errorText.empty()) {
        m_uiDrawList.AddText(m_fontManager, errorText, bodyX, popupY + 214.0f, 0xFFE07A7A, bodyW);
    }
    m_uiDrawList.AddRectFilled(popupX, footerY, popupW, 1.0f, 0xFF2B2B2B);

    if (m_uiContext.Button("Cancel", popupX + 22.0f, buttonY, 110.0f, 32.0f,
                           0xFF333333, 0xFF444444, 0xFF222222)) {
        ClearClosePrompt();
        return;
    }

    if (m_uiContext.Button("Discard", popupX + popupW - 266.0f, buttonY, 110.0f, 32.0f,
                           0xFF5E2B2B, 0xFF7A3434, 0xFF8C3A3A)) {
        ClearClosePrompt();
        m_pendingWindowCommand = m_closePromptCloseAllWindows
            ? WindowCommand::CloseAllDiscard
            : WindowCommand::CloseThisWindow;
        return;
    }

    if (m_uiContext.Button("Save", popupX + popupW - 140.0f, buttonY, 110.0f, 32.0f,
                           0xFF29573B, 0xFF317146, 0xFF20462F)) {
        if (m_closePromptCloseAllWindows) {
            m_pendingWindowCommand = WindowCommand::CloseAllSave;
        } else if (SavePendingOpenDocuments()) {
            ClearClosePrompt();
            m_pendingWindowCommand = WindowCommand::CloseThisWindow;
        } else {
            m_closePromptError = L"One or more files could not be saved. Fix that first, or discard the changes.";
        }
    }
}

void DXRenderer::RefreshWindowTitle() {
    if (m_hwnd == nullptr || m_standaloneActorViewerWindow || m_standaloneMaterialEditorWindow) {
        return;
    }

    std::wstring title;
    if (m_engineState == EngineState::ProjectLoading) {
        title = L"Catalyst Loading - " + ResolveActiveProjectDisplayName();
    } else if (m_engineState == EngineState::Editor) {
        title = L"Catalyst Editor - " + ResolveActiveProjectDisplayName() + L" (" + ResolveActiveMapDisplayName() + L")";
    } else {
        title = L"Catalyst Launcher";
    }

    if (title != m_lastWindowTitle) {
        SetWindowTextW(m_hwnd, title.c_str());
        m_lastWindowTitle = title;
    }
}

void DXRenderer::RefreshObjectBlueprintRuntime(GameObject& object) {
    object.blueprintPlayerControlled = false;
    object.blueprintSpaceJumpEnabled = false;
    object.blueprintMoveSpeed = 6.0f;
    object.blueprintJumpImpulse = 5.0f;
    object.blueprintJumpVelocity = 0.0f;
    object.blueprintGroundHeight = object.position.y;
    object.blueprintPossessCamera = false;
    object.blueprintCameraOffset = {0.0f, 2.0f, -5.0f};
    object.blueprintHasTrigger = false;
    object.blueprintTriggerShape = PhysicsColliderShape::Box;
    object.blueprintTriggerOffset = {0.0f, 0.0f, 0.0f};
    object.blueprintTriggerExtents = {0.75f, 0.75f, 0.75f};
    object.blueprintTriggerRadius = 0.75f;
    object.blueprintViewportWidgetAssetPaths.clear();

    if (object.blueprintAssetPath.empty()) {
        return;
    }

    BlueprintRuntimeData runtimeData;
    if (!LoadBlueprintRuntimeData(object.blueprintAssetPath, runtimeData)) {
        return;
    }

    object.blueprintPlayerControlled = runtimeData.playerCharacterController;
    object.blueprintSpaceJumpEnabled = runtimeData.playerSpaceJump;
    object.blueprintMoveSpeed = runtimeData.playerMoveSpeed;
    object.blueprintJumpImpulse = runtimeData.playerJumpImpulse;
    object.blueprintJumpVelocity = 0.0f;
    object.blueprintGroundHeight = object.position.y;
    object.blueprintViewportWidgetAssetPaths = runtimeData.viewportWidgetAssetPaths;
    object.asset = nullptr;

    bool appliedMeshComponent = false;
    for (const BlueprintRuntimeComponent& component : runtimeData.components) {
        if (!appliedMeshComponent &&
            (component.kind == "StaticMesh" || component.kind == "SkeletalMesh") &&
            !component.assetPath.empty()) {
            Asset* resolvedAsset = ResolveSceneAsset(-1, fs::path(component.assetPath).stem().string(), component.assetPath);
            if (resolvedAsset != nullptr) {
                object.asset = resolvedAsset;
                appliedMeshComponent = true;
            }
        }

        if (!object.blueprintPossessCamera && component.kind == "Camera" && component.possessOnPlay) {
            object.blueprintPossessCamera = true;
            object.blueprintCameraOffset = component.location;
        }

        if (!object.blueprintHasTrigger && component.kind == "Trigger") {
            object.blueprintHasTrigger = true;
            object.blueprintTriggerShape = component.triggerShape;
            object.blueprintTriggerOffset = component.location;
            object.blueprintTriggerExtents = component.triggerExtents;
            object.blueprintTriggerRadius = component.triggerRadius;
        }
    }
}

void DXRenderer::RefreshSceneBlueprintRuntime() {
    for (GameObject& object : m_gameObjects) {
        RefreshObjectBlueprintRuntime(object);
    }
}

void DXRenderer::SetPlayerMouseLookLocked(bool locked, float viewportTop, float viewportWidth, float viewportHeight) {
    if (m_hwnd == nullptr) {
        m_playerMouseLookLocked = false;
        return;
    }

    if (!locked) {
        if (!m_playerMouseLookLocked) {
            return;
        }

        m_playerMouseLookLocked = false;
        ClipCursor(nullptr);
        SetCursorVisibleState(true);
        return;
    }

    m_playerMouseLookLocked = true;
    SetCursorVisibleState(false);

    const int left = 0;
    const int top = static_cast<int>(std::lround(viewportTop));
    const int right = static_cast<int>(std::lround(viewportWidth));
    const int bottom = static_cast<int>(std::lround(viewportTop + viewportHeight));
    RECT clipRect = {left, top, right, bottom};
    POINT topLeft = {clipRect.left, clipRect.top};
    POINT bottomRight = {clipRect.right, clipRect.bottom};
    ClientToScreen(m_hwnd, &topLeft);
    ClientToScreen(m_hwnd, &bottomRight);
    RECT screenClipRect = {topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    ClipCursor(&screenClipRect);
}

void DXRenderer::ApplyBlueprintGameplayNodes(float deltaTime, float viewportTop, float viewportWidth, float viewportHeight, bool mouseInViewport) {
    constexpr float kBlueprintJumpGravity = 18.0f;
    constexpr float kMouseLookSensitivity = 0.005f;
    constexpr float kMinMouseLookPitch = -1.05f;
    constexpr float kMaxMouseLookPitch = 0.7f;

    if (deltaTime <= 0.0f) {
        return;
    }

    for (GameObject& object : m_gameObjects) {
        if (!object.blueprintHasTrigger) {
            continue;
        }

        object.physics.collider.enabled = true;
        object.physics.collider.isTrigger = true;
        object.physics.collider.shape = object.blueprintTriggerShape;
        object.physics.collider.centerOffset = object.blueprintTriggerOffset;
        object.physics.collider.boxExtents = object.blueprintTriggerExtents;
        object.physics.collider.sphereRadius = (std::max)(0.05f, object.blueprintTriggerRadius);
    }

    const bool moveForward = (GetAsyncKeyState('W') & 0x8000) != 0;
    const bool moveBackward = (GetAsyncKeyState('S') & 0x8000) != 0;
    const bool moveLeft = (GetAsyncKeyState('A') & 0x8000) != 0;
    const bool moveRight = (GetAsyncKeyState('D') & 0x8000) != 0;
    const bool jumpPressed = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    const bool jumpTriggered = jumpPressed && !m_jumpKeyWasDown;
    const bool escapeDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    const bool escapeTriggered = escapeDown && !m_escapeKeyWasDown;

    float inputX = 0.0f;
    float inputZ = 0.0f;
    if (moveLeft) {
        inputX -= 1.0f;
    }
    if (moveRight) {
        inputX += 1.0f;
    }
    if (moveBackward) {
        inputZ -= 1.0f;
    }
    if (moveForward) {
        inputZ += 1.0f;
    }

    const float inputLength = std::sqrt(inputX * inputX + inputZ * inputZ);
    if (inputLength > 0.001f) {
        inputX /= inputLength;
        inputZ /= inputLength;
    }

    GameObject* controlledObject = nullptr;
    for (GameObject& object : m_gameObjects) {
        if (object.blueprintPlayerControlled) {
            controlledObject = &object;
            break;
        }
    }

    for (GameObject& object : m_gameObjects) {
        if (&object == controlledObject || !object.blueprintPlayerControlled) {
            continue;
        }

        object.physics.rigidBody.enabled = false;
        object.physics.rigidBody.velocity.x = 0.0f;
        object.physics.rigidBody.velocity.y = 0.0f;
        object.physics.rigidBody.velocity.z = 0.0f;
    }

    if (controlledObject != nullptr) {
        const bool canCaptureMouse =
            g_InputManager != nullptr &&
            m_hwnd != nullptr &&
            GetForegroundWindow() == m_hwnd &&
            viewportWidth > 1.0f &&
            viewportHeight > 1.0f;

        if (m_playerMouseLookLocked && escapeTriggered) {
            SetPlayerMouseLookLocked(false);
            m_playerMouseLookSuppressed = true;
        } else if (!canCaptureMouse && m_playerMouseLookLocked) {
            SetPlayerMouseLookLocked(false);
        } else if (canCaptureMouse && !m_playerMouseLookLocked && !m_playerMouseLookSuppressed) {
            SetPlayerMouseLookLocked(true, viewportTop, viewportWidth, viewportHeight);
        } else if (canCaptureMouse && !m_playerMouseLookLocked && m_playerMouseLookSuppressed &&
                   mouseInViewport && g_InputManager->IsMouseButtonPressed(0)) {
            m_playerMouseLookSuppressed = false;
            SetPlayerMouseLookLocked(true, viewportTop, viewportWidth, viewportHeight);
        }

        if (m_playerMouseLookLocked && canCaptureMouse) {
            const int centerClientX = static_cast<int>(std::lround(viewportWidth * 0.5f));
            const int centerClientY = static_cast<int>(std::lround(viewportTop + viewportHeight * 0.5f));
            const float deltaX = static_cast<float>(g_InputManager->GetMouseX() - centerClientX);
            const float deltaY = static_cast<float>(g_InputManager->GetMouseY() - centerClientY);

            m_playerControllerYaw += deltaX * kMouseLookSensitivity;
            m_playerControllerPitch = std::clamp(m_playerControllerPitch + deltaY * kMouseLookSensitivity,
                                                 kMinMouseLookPitch, kMaxMouseLookPitch);

            POINT centerScreenPoint = {centerClientX, centerClientY};
            ClientToScreen(m_hwnd, &centerScreenPoint);
            SetCursorPos(centerScreenPoint.x, centerScreenPoint.y);
        }

        controlledObject->rotation.y = m_playerControllerYaw;
        const float moveSpeed = (std::max)(0.1f, controlledObject->blueprintMoveSpeed);
        if (controlledObject->physics.rigidBody.enabled || controlledObject->physics.collider.enabled) {
            controlledObject->physics.collider.enabled = true;
            controlledObject->physics.rigidBody.enabled = false;
            controlledObject->physics.rigidBody.bodyType = PhysicsBodyType::Static;
            controlledObject->physics.rigidBody.useGravity = false;
            controlledObject->physics.rigidBody.linearDamping = 0.0f;
            controlledObject->physics.rigidBody.velocity.x = 0.0f;
            controlledObject->physics.rigidBody.velocity.y = 0.0f;
        }

        const float forwardX = std::sin(m_playerControllerYaw);
        const float forwardZ = std::cos(m_playerControllerYaw);
        const float rightX = std::cos(m_playerControllerYaw);
        const float rightZ = -std::sin(m_playerControllerYaw);

        controlledObject->position.x += (rightX * inputX + forwardX * inputZ) * moveSpeed * deltaTime;
        controlledObject->position.z += (rightZ * inputX + forwardZ * inputZ) * moveSpeed * deltaTime;

        if (controlledObject->blueprintSpaceJumpEnabled) {
            const float groundHeight = controlledObject->blueprintGroundHeight;
            const bool grounded = controlledObject->position.y <= (groundHeight + 0.001f) &&
                                  controlledObject->blueprintJumpVelocity <= 0.0f;
            if (grounded) {
                controlledObject->position.y = groundHeight;
                controlledObject->blueprintJumpVelocity = 0.0f;
                if (jumpTriggered) {
                    controlledObject->blueprintJumpVelocity = (std::max)(1.5f, controlledObject->blueprintJumpImpulse);
                }
            }

            if (controlledObject->blueprintJumpVelocity != 0.0f || controlledObject->position.y > groundHeight) {
                controlledObject->position.y += controlledObject->blueprintJumpVelocity * deltaTime;
                controlledObject->blueprintJumpVelocity -= kBlueprintJumpGravity * deltaTime;
                if (controlledObject->position.y <= groundHeight) {
                    controlledObject->position.y = groundHeight;
                    controlledObject->blueprintJumpVelocity = 0.0f;
                }
            }
        } else {
            controlledObject->blueprintJumpVelocity = 0.0f;
            controlledObject->position.y = controlledObject->blueprintGroundHeight;
        }

        const XMMATRIX cameraRotation = XMMatrixRotationRollPitchYaw(m_playerControllerPitch, m_playerControllerYaw, 0.0f);
        XMFLOAT3 rotatedCameraOffset = controlledObject->blueprintCameraOffset;
        XMStoreFloat3(&rotatedCameraOffset, XMVector3TransformCoord(XMLoadFloat3(&controlledObject->blueprintCameraOffset), cameraRotation));
        const XMFLOAT3 cameraPosition = {
            controlledObject->position.x + rotatedCameraOffset.x,
            controlledObject->position.y + rotatedCameraOffset.y,
            controlledObject->position.z + rotatedCameraOffset.z
        };
        const XMFLOAT3 cameraTarget = {
            controlledObject->position.x,
            controlledObject->position.y + 1.0f,
            controlledObject->position.z
        };
        m_camera.SetLookAt(cameraPosition, cameraTarget);
    } else {
        SetPlayerMouseLookLocked(false);
        m_playerMouseLookSuppressed = false;
    }

    for (const GameObject& object : m_gameObjects) {
        if (controlledObject != nullptr) {
            break;
        }
        if (!object.blueprintPossessCamera) {
            continue;
        }

        DirectX::XMFLOAT3 cameraPosition = {
            object.position.x + object.blueprintCameraOffset.x,
            object.position.y + object.blueprintCameraOffset.y,
            object.position.z + object.blueprintCameraOffset.z
        };
        DirectX::XMFLOAT3 cameraTarget = {
            object.position.x,
            object.position.y + 1.0f,
            object.position.z
        };
        m_camera.SetLookAt(cameraPosition, cameraTarget);
        break;
    }

    m_jumpKeyWasDown = jumpPressed;
    m_escapeKeyWasDown = escapeDown;
}

bool DXRenderer::LoadRuntimeWidgetInstance(const std::wstring& assetPath, RuntimeWidgetInstance& outInstance) {
    outInstance = {};
    outInstance.assetPath = assetPath;
    if (assetPath.empty()) {
        return false;
    }

    std::ifstream inputFile(fs::path(assetPath), std::ios::binary);
    if (!inputFile.is_open()) {
        return false;
    }

    const std::string content((std::istreambuf_iterator<char>(inputFile)), std::istreambuf_iterator<char>());
    JsonValue rootValue;
    JsonParser parser(content);
    if (!parser.Parse(rootValue) || rootValue.type != JsonValueType::Object) {
        return false;
    }

    if (GetJsonString(rootValue, "type") != "CatalystUIBlueprint") {
        return false;
    }

    const JsonValue* graphValue = FindJsonField(rootValue, "graph");
    if (graphValue == nullptr || graphValue->type != JsonValueType::Object) {
        return false;
    }

    const JsonValue* nodesValue = FindJsonField(*graphValue, "nodes");
    if (nodesValue != nullptr && nodesValue->type == JsonValueType::Array) {
        outInstance.nodes.reserve(nodesValue->arrayValue.size());
        for (const JsonValue& nodeValue : nodesValue->arrayValue) {
            if (nodeValue.type != JsonValueType::Object) {
                continue;
            }

            RuntimeWidgetNode node;
            node.id = GetJsonInt(nodeValue, "id", 0);
            node.nodeTypeId = GetJsonString(nodeValue, "nodeTypeId");
            node.displayText = GetJsonString(nodeValue, "displayText");
            node.canvasX = GetJsonNumber(nodeValue, "canvasX", 0.0f);
            node.canvasY = GetJsonNumber(nodeValue, "canvasY", 0.0f);
            node.canvasWidth = GetJsonNumber(nodeValue, "canvasWidth", 120.0f);
            node.canvasHeight = GetJsonNumber(nodeValue, "canvasHeight", 40.0f);
            node.tint = GetJsonFloat4(nodeValue, "tint", {1.0f, 1.0f, 1.0f, 1.0f});

            if (node.displayText.empty()) {
                if (IsUIButtonNodeType(node.nodeTypeId)) {
                    node.displayText = "Button";
                } else if (IsUITextBlockNodeType(node.nodeTypeId)) {
                    node.displayText = "Text";
                } else if (IsUICanvasNodeType(node.nodeTypeId)) {
                    node.displayText = "Canvas";
                } else if (IsUIImageNodeType(node.nodeTypeId)) {
                    node.displayText = "Image";
                }
            }

            outInstance.nodes.push_back(node);
        }
    }

    const JsonValue* linksValue = FindJsonField(*graphValue, "links");
    if (linksValue != nullptr && linksValue->type == JsonValueType::Array) {
        outInstance.links.reserve(linksValue->arrayValue.size());
        for (const JsonValue& linkValue : linksValue->arrayValue) {
            if (linkValue.type != JsonValueType::Object) {
                continue;
            }

            RuntimeWidgetLink link;
            link.fromNodeId = GetJsonInt(linkValue, "fromNodeId", 0);
            link.toNodeId = GetJsonInt(linkValue, "toNodeId", 0);
            link.fromPinKind = GetJsonString(linkValue, "fromPinKind", "Exec");
            link.toPinKind = GetJsonString(linkValue, "toPinKind", "Exec");
            outInstance.links.push_back(link);
        }
    }

    return true;
}

DXRenderer::RuntimeWidgetNode* DXRenderer::FindRuntimeWidgetNode(RuntimeWidgetInstance& instance, int nodeId) {
    for (RuntimeWidgetNode& node : instance.nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

const DXRenderer::RuntimeWidgetNode* DXRenderer::FindRuntimeWidgetNode(const RuntimeWidgetInstance& instance, int nodeId) const {
    for (const RuntimeWidgetNode& node : instance.nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

void DXRenderer::ExecuteRuntimeWidgetNode(RuntimeWidgetInstance& instance, int nodeId) {
    std::vector<int> pendingNodes = {nodeId};
    std::vector<int> visitedNodes;

    while (!pendingNodes.empty()) {
        const int currentNodeId = pendingNodes.back();
        pendingNodes.pop_back();
        if (std::find(visitedNodes.begin(), visitedNodes.end(), currentNodeId) != visitedNodes.end()) {
            continue;
        }
        visitedNodes.push_back(currentNodeId);

        RuntimeWidgetNode* currentNode = FindRuntimeWidgetNode(instance, currentNodeId);
        if (currentNode == nullptr) {
            continue;
        }

        if (currentNode->nodeTypeId == BlueprintNodes::kSetTextColorNodeId) {
            for (const RuntimeWidgetLink& link : instance.links) {
                if (link.toNodeId != currentNodeId || link.fromPinKind != "Data" || link.toPinKind != "Data") {
                    continue;
                }

                RuntimeWidgetNode* targetNode = FindRuntimeWidgetNode(instance, link.fromNodeId);
                if (targetNode != nullptr && IsUITextBlockNodeType(targetNode->nodeTypeId)) {
                    targetNode->tint = currentNode->tint;
                }
            }
        }

        for (const RuntimeWidgetLink& link : instance.links) {
            if (link.fromNodeId == currentNodeId && link.fromPinKind == "Exec" && link.toPinKind == "Exec") {
                pendingNodes.push_back(link.toNodeId);
            }
        }
    }
}

void DXRenderer::DrawRuntimeBlueprintWidgets(float viewportLeft, float viewportTop, float viewportWidth, float viewportHeight) {
    struct ActiveWidgetRequest {
        std::string key;
        std::wstring assetPath;
        std::string ownerName;
    };

    std::vector<ActiveWidgetRequest> activeWidgets;
    for (size_t objectIndex = 0; objectIndex < m_gameObjects.size(); ++objectIndex) {
        const GameObject& object = m_gameObjects[objectIndex];
        for (size_t widgetIndex = 0; widgetIndex < object.blueprintViewportWidgetAssetPaths.size(); ++widgetIndex) {
            const std::wstring& widgetAssetPath = object.blueprintViewportWidgetAssetPaths[widgetIndex];
            if (widgetAssetPath.empty()) {
                continue;
            }

            activeWidgets.push_back({
                std::to_string(objectIndex) + ":" + std::to_string(widgetIndex) + ":" + WideToUtf8(widgetAssetPath),
                widgetAssetPath,
                object.name.empty() ? "Blueprint Actor" : object.name
            });
        }
    }

    for (auto iter = m_runtimeWidgetInstances.begin(); iter != m_runtimeWidgetInstances.end();) {
        const bool isStillActive = std::find_if(activeWidgets.begin(), activeWidgets.end(), [&](const ActiveWidgetRequest& request) {
            return request.key == iter->first;
        }) != activeWidgets.end();
        if (!isStillActive) {
            iter = m_runtimeWidgetInstances.erase(iter);
        } else {
            ++iter;
        }
    }

    const float safeLeft = viewportLeft + 22.0f;
    const float safeTop = viewportTop + 22.0f;
    const float maxRight = viewportLeft + viewportWidth - 22.0f;
    const float maxBottom = viewportTop + viewportHeight - 22.0f;
    float cursorX = safeLeft;
    float cursorY = safeTop;
    float columnWidth = 0.0f;

    for (const ActiveWidgetRequest& request : activeWidgets) {
        RuntimeWidgetInstance* instance = nullptr;
        auto foundInstance = m_runtimeWidgetInstances.find(request.key);
        if (foundInstance == m_runtimeWidgetInstances.end()) {
            RuntimeWidgetInstance loadedInstance;
            if (LoadRuntimeWidgetInstance(request.assetPath, loadedInstance)) {
                foundInstance = m_runtimeWidgetInstances.emplace(request.key, std::move(loadedInstance)).first;
            }
        }
        if (foundInstance != m_runtimeWidgetInstances.end()) {
            instance = &foundInstance->second;
        }

        std::error_code existsError;
        const bool widgetAssetExists = fs::exists(request.assetPath, existsError) && !existsError;
        if (instance == nullptr) {
            const float fallbackW = 320.0f;
            const float fallbackH = 88.0f;
            if (cursorY + fallbackH > maxBottom) {
                cursorY = safeTop;
                cursorX += columnWidth + 20.0f;
                columnWidth = 0.0f;
            }
            if (cursorX + fallbackW > maxRight) {
                return;
            }

            const uint32_t accentColor = widgetAssetExists ? 0xFF86D7C7 : 0xFFE08A7A;
            m_uiDrawList.AddRectFilled(cursorX, cursorY, fallbackW, fallbackH, 0xE020232A);
            m_uiDrawList.AddRectFilled(cursorX, cursorY, fallbackW, 4.0f, accentColor);
            m_uiDrawList.AddText(m_fontManager, fs::path(request.assetPath).stem().string(),
                                 cursorX + 14.0f, cursorY + 24.0f, 0xFFFFFFFF, fallbackW - 28.0f);
            m_uiDrawList.AddText(m_fontManager,
                                 widgetAssetExists ? "Widget asset could not be parsed." : "Missing widget asset.",
                                 cursorX + 14.0f, cursorY + 50.0f, 0xFFBEC6D0, fallbackW - 28.0f);
            cursorY += fallbackH + 18.0f;
            columnWidth = (std::max)(columnWidth, fallbackW);
            continue;
        }

        float widgetContentWidth = 220.0f;
        float widgetContentHeight = 120.0f;
        for (const RuntimeWidgetNode& node : instance->nodes) {
            if (!IsUIElementNodeType(node.nodeTypeId)) {
                continue;
            }
            widgetContentWidth = (std::max)(widgetContentWidth, node.canvasX + node.canvasWidth + 18.0f);
            widgetContentHeight = (std::max)(widgetContentHeight, node.canvasY + node.canvasHeight + 18.0f);
        }

        const float widgetW = widgetContentWidth + 24.0f;
        const float widgetH = widgetContentHeight + 48.0f;
        if (cursorY + widgetH > maxBottom) {
            cursorY = safeTop;
            cursorX += columnWidth + 20.0f;
            columnWidth = 0.0f;
        }
        if (cursorX + widgetW > maxRight) {
            return;
        }

        const float baseX = cursorX;
        const float baseY = cursorY;
        const float contentX = baseX + 12.0f;
        const float contentY = baseY + 34.0f;
        const std::string widgetTitle = fs::path(request.assetPath).stem().string();
        const uint32_t panelColor = 0xD91D232A;
        m_uiDrawList.AddRectFilled(baseX, baseY, widgetW, widgetH, panelColor);
        m_uiDrawList.AddRectFilled(baseX, baseY, widgetW, 4.0f, 0xFF86D7C7);
        m_uiDrawList.AddText(m_fontManager, widgetTitle, baseX + 12.0f, baseY + 20.0f, 0xFFFFFFFF, widgetW - 24.0f);
        m_uiDrawList.AddText(m_fontManager, request.ownerName, baseX + widgetW - 136.0f, baseY + 20.0f, 0xFF9FA8B3, 124.0f);

        auto GetElementRect = [&](const RuntimeWidgetNode& node, float& outX, float& outY, float& outW, float& outH) {
            outX = contentX + node.canvasX;
            outY = contentY + node.canvasY;
            outW = (std::max)(20.0f, node.canvasWidth);
            outH = (std::max)(20.0f, node.canvasHeight);
        };

        if (g_InputManager != nullptr && !m_playerMouseLookLocked) {
            for (const RuntimeWidgetNode& node : instance->nodes) {
                if (!IsUIButtonNodeType(node.nodeTypeId)) {
                    continue;
                }

                float buttonX = 0.0f;
                float buttonY = 0.0f;
                float buttonW = 0.0f;
                float buttonH = 0.0f;
                GetElementRect(node, buttonX, buttonY, buttonW, buttonH);

                if (g_InputManager->IsMouseButtonPressed(0) &&
                    IsPointInRect(static_cast<float>(g_InputManager->GetMouseX()),
                                  static_cast<float>(g_InputManager->GetMouseY()),
                                  buttonX, buttonY, buttonW, buttonH)) {
                    for (const RuntimeWidgetLink& link : instance->links) {
                        if (link.fromNodeId == node.id && link.fromPinKind == "Exec" && link.toPinKind == "Exec") {
                            ExecuteRuntimeWidgetNode(*instance, link.toNodeId);
                        }
                    }
                }
            }
        }

        for (int drawPass = 0; drawPass < 2; ++drawPass) {
            for (const RuntimeWidgetNode& node : instance->nodes) {
                const bool isCanvas = IsUICanvasNodeType(node.nodeTypeId);
                if ((drawPass == 0) != isCanvas) {
                    continue;
                }
                if (!IsUIElementNodeType(node.nodeTypeId)) {
                    continue;
                }

                float elementX = 0.0f;
                float elementY = 0.0f;
                float elementW = 0.0f;
                float elementH = 0.0f;
                GetElementRect(node, elementX, elementY, elementW, elementH);
                const uint32_t tintColor = Float4ToUIntColor(node.tint);

                if (IsUICanvasNodeType(node.nodeTypeId)) {
                    m_uiDrawList.AddRectFilled(elementX, elementY, elementW, elementH, tintColor);
                    m_uiDrawList.AddText(m_fontManager, node.displayText, elementX + 10.0f, elementY + 20.0f, 0xFFFFFFFF, elementW - 20.0f);
                } else if (IsUIButtonNodeType(node.nodeTypeId)) {
                    const bool hovered = g_InputManager != nullptr &&
                        IsPointInRect(static_cast<float>(g_InputManager->GetMouseX()),
                                      static_cast<float>(g_InputManager->GetMouseY()),
                                      elementX, elementY, elementW, elementH);
                    m_uiDrawList.AddRectFilled(elementX, elementY, elementW, elementH,
                                               hovered ? Float4ToUIntColor({(std::min)(1.0f, node.tint.x + 0.08f),
                                                                            (std::min)(1.0f, node.tint.y + 0.08f),
                                                                            (std::min)(1.0f, node.tint.z + 0.08f),
                                                                            node.tint.w})
                                                       : tintColor);
                    m_uiDrawList.AddRectFilled(elementX, elementY, elementW, 3.0f, 0xFFFFFFFF);
                    m_uiDrawList.AddText(m_fontManager, node.displayText,
                                         elementX + 10.0f, elementY + elementH * 0.5f, 0xFFFFFFFF, elementW - 20.0f);
                } else if (IsUITextBlockNodeType(node.nodeTypeId)) {
                    m_uiDrawList.AddText(m_fontManager, node.displayText, elementX + 4.0f, elementY + 20.0f, tintColor, elementW - 8.0f);
                } else if (IsUIImageNodeType(node.nodeTypeId)) {
                    m_uiDrawList.AddRectFilled(elementX, elementY, elementW, elementH, tintColor);
                    m_uiDrawList.AddText(m_fontManager, "Image", elementX + 10.0f, elementY + 20.0f, 0xFFFFFFFF, elementW - 20.0f);
                }
            }
        }

        cursorY += widgetH + 18.0f;
        columnWidth = (std::max)(columnWidth, widgetW);
    }
}

Asset* DXRenderer::FindAssetById(int assetId) const {
    for (const auto& asset : m_assets) {
        if (asset && asset->id == assetId) {
            return asset.get();
        }
    }
    return nullptr;
}

Asset* DXRenderer::FindAssetBySourcePath(const std::wstring& sourcePath) const {
    const std::wstring normalizedPath = NormalizeAssetPath(sourcePath);
    if (normalizedPath.empty()) {
        return nullptr;
    }

    for (const auto& asset : m_assets) {
        if (!asset || asset->sourcePath.empty()) {
            continue;
        }

        const std::wstring candidatePath = NormalizeAssetPath(DecodeStoredPath(asset->sourcePath));
        if (!candidatePath.empty() && candidatePath == normalizedPath) {
            return asset.get();
        }
    }

    return nullptr;
}

Material* DXRenderer::FindMaterialByPath(const std::wstring& materialPath) const {
    const std::wstring normalizedPath = NormalizeAssetPath(materialPath);
    if (normalizedPath.empty()) {
        return nullptr;
    }

    auto found = m_materialCache.find(normalizedPath);
    return found != m_materialCache.end() ? found->second.get() : nullptr;
}

Texture* DXRenderer::FindTextureByPath(const std::wstring& texturePath) const {
    const std::wstring normalizedPath = NormalizeAssetPath(texturePath);
    if (normalizedPath.empty()) {
        return nullptr;
    }

    auto found = m_textureCache.find(normalizedPath);
    return found != m_textureCache.end() ? found->second.get() : nullptr;
}

std::wstring DXRenderer::GetCachedMaterialPath(const Material* material) const {
    if (!material) {
        return L"";
    }

    for (const auto& cachedMaterial : m_materialCache) {
        if (cachedMaterial.second.get() == material) {
            return cachedMaterial.first;
        }
    }

    return material->sourcePath;
}

std::wstring DXRenderer::GetCachedTexturePath(const Texture* texture) const {
    if (!texture) {
        return L"";
    }

    for (const auto& cachedTexture : m_textureCache) {
        if (cachedTexture.second.get() == texture) {
            return cachedTexture.first;
        }
    }

    return L"";
}

void DXRenderer::ResetSceneToDefaults() {
    m_gameObjects.clear();
    m_editorUI.State.selectedObj = -1;
    m_editorUI.State.selectedContentAsset = -1;
    m_editorUI.State.playYOffset = 0.0f;
    m_editorUI.State.isPlaying = false;
    m_lastPlayMode = false;
    m_playModeSnapshot.clear();
    m_physicsSystem.Reset();

    Asset* planeAsset = FindAssetById(2);
    if (planeAsset) {
        GameObject floor;
        floor.name = "Floor";
        floor.position = {0.0f, -3.0f, 15.0f};
        floor.scale = {10.0f, 1.0f, 10.0f};
        floor.color = {0.3f, 0.3f, 0.3f, 1.0f};
        floor.asset = planeAsset;
        PhysicsSystem::InitializeDefaultCollider(floor, false, true);
        m_gameObjects.push_back(floor);
    }

    Asset* cubeAsset = FindAssetById(0);
    if (cubeAsset) {
        GameObject skybox;
        skybox.name = "Sky Atmosphere";
        skybox.position = {0.0f, -9999.0f, 0.0f};
        skybox.scale = {1.0f, 1.0f, 1.0f};
        skybox.color = {1.0f, 1.0f, 1.0f, 1.0f};
        skybox.skyHorizonColor = {1.0f, 1.0f, 1.0f, 1.0f};
        skybox.asset = cubeAsset;
        m_gameObjects.push_back(skybox);
    }

    RefreshSceneSavedDocument();
}

void DXRenderer::ClearProjectRuntimeAssets() {
    m_gameObjects.clear();
    m_editorUI.State.selectedObj = -1;
    m_editorUI.State.selectedContentAsset = -1;
    m_editorUI.State.playYOffset = 0.0f;
    m_editorUI.State.isPlaying = false;
    m_lastPlayMode = false;
    m_playModeSnapshot.clear();
    m_physicsSystem.Reset();

    static const char* kBuiltinPrimitiveNames[] = {"Cube", "Sphere", "Plane", "Cylinder"};
    auto isBuiltinPrimitive = [](const std::string& primitiveName) {
        for (const char* builtinName : kBuiltinPrimitiveNames) {
            if (primitiveName == builtinName) {
                return true;
            }
        }
        return false;
    };

    for (auto it = m_primitives.begin(); it != m_primitives.end();) {
        if (isBuiltinPrimitive(it->first)) {
            ++it;
            continue;
        }

        delete it->second;
        it = m_primitives.erase(it);
    }

    if (m_assets.size() > 4) {
        m_assets.erase(m_assets.begin() + 4, m_assets.end());
    }

    m_materialCache.clear();
    m_textureCache.clear();
    m_savedSceneDocument.clear();
}

void DXRenderer::QueueProjectStartupSceneLoad(const std::wstring& projectFilePath) {
    m_pendingProjectFilePath = NormalizeAssetPath(projectFilePath);
    m_hasPendingProjectSceneLoad = true;
}

void DXRenderer::ProcessPendingProjectSceneLoad() {
    if (!m_hasPendingProjectSceneLoad) {
        return;
    }

    m_hasPendingProjectSceneLoad = false;
    const std::wstring projectFilePath = m_pendingProjectFilePath;
    m_pendingProjectFilePath.clear();

    if (projectFilePath.empty()) {
        ClearProjectRuntimeAssets();
        m_editorUI.State.currentMapPath.clear();
        ResetSceneToDefaults();
        return;
    }

    try {
        LoadStartupSceneForProject(projectFilePath);
    } catch (const std::exception& exception) {
        DebugLog(std::string("Catalyst scene load failed: ") + exception.what());
        ClearProjectRuntimeAssets();
        ResetSceneToDefaults();
    } catch (...) {
        DebugLog("Catalyst scene load failed with an unknown exception.");
        ClearProjectRuntimeAssets();
        ResetSceneToDefaults();
    }
}

bool DXRenderer::LoadStartupSceneForProject(const std::wstring& projectFilePath) {
    const std::wstring normalizedProjectFilePath = NormalizeAssetPath(projectFilePath);
    if (normalizedProjectFilePath.empty()) {
        ClearProjectRuntimeAssets();
        m_editorUI.State.currentMapPath.clear();
        ResetSceneToDefaults();
        return false;
    }

    m_editorUI.State.currentProjectFile = normalizedProjectFilePath;
    const std::wstring projectRoot = fs::path(normalizedProjectFilePath).parent_path().wstring();
    m_editorUI.State.currentProjectFolder = projectRoot;
    const std::wstring scenePath = NormalizeAssetPath(ResolveProjectStartupScenePath(normalizedProjectFilePath));
    m_editorUI.State.currentMapPath = scenePath;

    const fs::path sceneDirectory = fs::path(scenePath).parent_path();
    if (!sceneDirectory.empty()) {
        m_editorUI.State.currentBrowserPath = sceneDirectory.wstring();
    } else {
        m_editorUI.State.currentBrowserPath = (fs::path(projectRoot) / L"Assets").wstring();
    }

    return LoadSceneFromMap(scenePath, projectRoot);
}

bool DXRenderer::OpenSceneMap(const std::wstring& scenePath) {
    const std::wstring projectFilePath = ResolveActiveProjectFilePath();
    if (projectFilePath.empty()) {
        return false;
    }

    const std::wstring normalizedScenePath = NormalizeAssetPath(scenePath);
    if (normalizedScenePath.empty()) {
        return false;
    }

    const std::wstring projectRoot = fs::path(projectFilePath).parent_path().wstring();
    m_editorUI.State.currentMapPath = normalizedScenePath;

    const fs::path sceneDirectory = fs::path(normalizedScenePath).parent_path();
    if (!sceneDirectory.empty()) {
        m_editorUI.State.currentBrowserPath = sceneDirectory.wstring();
    }

    return LoadSceneFromMap(normalizedScenePath, projectRoot);
}

Asset* DXRenderer::ResolveSceneAsset(int assetId, const std::string& assetName, const std::wstring& assetSourcePath) {
    if (!assetSourcePath.empty()) {
        if (Asset* existingAsset = FindAssetBySourcePath(assetSourcePath)) {
            if (assetId >= 0) {
                existingAsset->id = assetId;
            }
            if (!assetName.empty()) {
                existingAsset->name = assetName;
            }
            return existingAsset;
        }

        if (fs::exists(assetSourcePath)) {
            try {
                const MeshData meshData = ParseActorAssetForPreview(assetSourcePath);
                if (!meshData.Vertices.empty() && !meshData.Indices.empty()) {
                    const int resolvedAssetId = assetId >= 0 ? assetId : GetNextAvailableAssetId();
                    const std::string meshKey = "SceneAsset_" + std::to_string(resolvedAssetId) + "_" + fs::path(assetSourcePath).stem().string();

                    Mesh* mesh = new Mesh(m_device.Get(), m_commandList.Get(),
                                          meshData.Vertices.data(), meshData.Vertices.size(),
                                          meshData.Indices.data(), meshData.Indices.size());
                    m_primitives[meshKey] = mesh;

                    auto sceneAsset = std::make_shared<Asset>();
                    sceneAsset->id = resolvedAssetId;
                    sceneAsset->name = !assetName.empty() ? assetName : fs::path(assetSourcePath).stem().string();
                    sceneAsset->sourcePath = WideToUtf8(NormalizeAssetPath(assetSourcePath));
                    sceneAsset->type = AssetType::Mesh;
                    sceneAsset->mesh = mesh;
                    m_assets.push_back(sceneAsset);
                    return sceneAsset.get();
                }
            } catch (const std::exception& exception) {
                DebugLog(std::string("Catalyst asset restore failed for ") + WideToUtf8(assetSourcePath) + ": " + exception.what());
            } catch (...) {
                DebugLog(std::string("Catalyst asset restore failed for ") + WideToUtf8(assetSourcePath) + ": unknown exception.");
            }
        }
    }

    if (assetId >= 0) {
        if (Asset* asset = FindAssetById(assetId)) {
            return asset;
        }
    }

    if (!assetName.empty()) {
        for (const auto& asset : m_assets) {
            if (asset && asset->name == assetName) {
                return asset.get();
            }
        }
    }

    return nullptr;
}

bool DXRenderer::SaveCurrentScene() {
    try {
        const std::wstring projectFilePath = ResolveActiveProjectFilePath();
        if (projectFilePath.empty()) {
            return false;
        }

        const std::wstring projectRoot = fs::path(projectFilePath).parent_path().wstring();
        std::wstring scenePath = !m_editorUI.State.currentMapPath.empty()
            ? NormalizeAssetPath(m_editorUI.State.currentMapPath)
            : NormalizeAssetPath(ResolveProjectStartupSceneSavePath(projectFilePath));
        if (scenePath.empty()) {
            return false;
        }

        std::error_code ec;
        fs::create_directories(fs::path(scenePath).parent_path(), ec);

        const std::vector<GameObject>& objectsToSave =
            (m_editorUI.State.isPlaying && !m_playModeSnapshot.empty()) ? m_playModeSnapshot : m_gameObjects;
        const std::string sceneDocument = BuildSceneDocument(objectsToSave, projectRoot);

        std::ofstream outFile(fs::path(scenePath), std::ios::binary | std::ios::trunc);
        if (!outFile.is_open()) {
            return false;
        }
        outFile << sceneDocument;
        outFile.close();

        m_editorUI.State.currentMapPath = scenePath;

        const std::wstring startupScenePath = NormalizeAssetPath(ResolveProjectStartupSceneSavePath(projectFilePath));
        if (!startupScenePath.empty() && scenePath == startupScenePath) {
            if (!UpdateProjectStartupScene(projectFilePath, scenePath)) {
                return false;
            }
        }

        m_savedSceneDocument = sceneDocument;
        return true;
    } catch (const std::exception& exception) {
        DebugLog(std::string("Catalyst scene save failed: ") + exception.what());
        return false;
    } catch (...) {
        DebugLog("Catalyst scene save failed with an unknown exception.");
        return false;
    }
}

bool DXRenderer::LoadSceneFromMap(const std::wstring& scenePath, const std::wstring& projectRoot) {
    ClearProjectRuntimeAssets();

    if (scenePath.empty() || !fs::exists(scenePath)) {
        ResetSceneToDefaults();
        return false;
    }

    std::ifstream inFile(fs::path(scenePath), std::ios::binary);
    if (!inFile.is_open()) {
        ResetSceneToDefaults();
        return false;
    }

    const std::string content((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    JsonValue rootValue;
    JsonParser parser(content);
    if (!parser.Parse(rootValue) || rootValue.type != JsonValueType::Object) {
        ResetSceneToDefaults();
        return false;
    }

    const JsonValue* objectsValue = FindJsonField(rootValue, "objects");
    if (!objectsValue || objectsValue->type != JsonValueType::Array) {
        ResetSceneToDefaults();
        return false;
    }

    for (const JsonValue& serializedObject : objectsValue->arrayValue) {
        if (serializedObject.type != JsonValueType::Object) {
            continue;
        }

        GameObject sceneObject;
        sceneObject.name = GetJsonString(serializedObject, "name", "GameObject");
        sceneObject.type = ObjectTypeFromString(GetJsonString(serializedObject, "type", "Mesh"));
        sceneObject.position = GetJsonFloat3(serializedObject, "position", {0.0f, 0.0f, 0.0f});
        sceneObject.rotation = GetJsonFloat3(serializedObject, "rotation", {0.0f, 0.0f, 0.0f});
        sceneObject.scale = GetJsonFloat3(serializedObject, "scale", {1.0f, 1.0f, 1.0f});
        sceneObject.color = GetJsonFloat4(serializedObject, "color", {1.0f, 1.0f, 1.0f, 1.0f});
        sceneObject.skyHorizonColor = GetJsonFloat4(serializedObject, "skyHorizonColor", {1.0f, 1.0f, 1.0f, 1.0f});
        sceneObject.lightIntensity = GetJsonNumber(serializedObject, "lightIntensity", 1.0f);

        const JsonValue* postProcessValue = FindJsonField(serializedObject, "postProcess");
        if (postProcessValue && postProcessValue->type == JsonValueType::Object) {
            sceneObject.ppSettings.exposure = GetJsonNumber(*postProcessValue, "exposure", 1.0f);
            sceneObject.ppSettings.colorTint = GetJsonFloat3(*postProcessValue, "colorTint", {1.0f, 1.0f, 1.0f});
            sceneObject.ppSettings.bloomThreshold = GetJsonNumber(*postProcessValue, "bloomThreshold", 1.0f);
            sceneObject.ppSettings.bloomIntensity = GetJsonNumber(*postProcessValue, "bloomIntensity", 0.5f);
            sceneObject.ppSettings.blendRadius = GetJsonNumber(*postProcessValue, "blendRadius", 1.0f);
        }

        const int assetId = GetJsonInt(serializedObject, "assetId", -1);
        const std::wstring assetSourcePath = ResolveSceneReferencePath(projectRoot, GetJsonString(serializedObject, "assetSource"));
        sceneObject.asset = ResolveSceneAsset(assetId, GetJsonString(serializedObject, "assetName"), assetSourcePath);
        sceneObject.blueprintAssetPath = ResolveSceneReferencePath(projectRoot, GetJsonString(serializedObject, "blueprintAsset"));

        const std::wstring materialPath = ResolveSceneReferencePath(projectRoot, GetJsonString(serializedObject, "assignedMaterial"));
        if (!materialPath.empty()) {
            sceneObject.assignedMaterial = LoadMaterialAsset(materialPath);
        }

        const JsonValue* overrideTextures = FindJsonField(serializedObject, "overrideTextures");
        if (overrideTextures && overrideTextures->type == JsonValueType::Object) {
            const std::wstring albedoPath = ResolveSceneReferencePath(projectRoot, GetJsonString(*overrideTextures, "albedo"));
            const std::wstring normalPath = ResolveSceneReferencePath(projectRoot, GetJsonString(*overrideTextures, "normal"));
            const std::wstring metallicPath = ResolveSceneReferencePath(projectRoot, GetJsonString(*overrideTextures, "metallic"));
            const std::wstring roughnessPath = ResolveSceneReferencePath(projectRoot, GetJsonString(*overrideTextures, "roughness"));
            const std::wstring aoPath = ResolveSceneReferencePath(projectRoot, GetJsonString(*overrideTextures, "ao"));

            sceneObject.overrideAlbedo = albedoPath.empty() ? nullptr : LoadTextureAsset(albedoPath);
            sceneObject.overrideNormal = normalPath.empty() ? nullptr : LoadTextureAsset(normalPath);
            sceneObject.overrideMetallic = metallicPath.empty() ? nullptr : LoadTextureAsset(metallicPath);
            sceneObject.overrideRoughness = roughnessPath.empty() ? nullptr : LoadTextureAsset(roughnessPath);
            sceneObject.overrideAO = aoPath.empty() ? nullptr : LoadTextureAsset(aoPath);
        }

        const JsonValue* physicsValue = FindJsonField(serializedObject, "physics");
        if (physicsValue && physicsValue->type == JsonValueType::Object) {
            const JsonValue* rigidBodyValue = FindJsonField(*physicsValue, "rigidBody");
            if (rigidBodyValue && rigidBodyValue->type == JsonValueType::Object) {
                sceneObject.physics.rigidBody.enabled = GetJsonBool(*rigidBodyValue, "enabled", false);
                sceneObject.physics.rigidBody.bodyType =
                    PhysicsBodyTypeFromString(GetJsonString(*rigidBodyValue, "bodyType", "Dynamic"));
                sceneObject.physics.rigidBody.useGravity = GetJsonBool(*rigidBodyValue, "useGravity", true);
                sceneObject.physics.rigidBody.mass = GetJsonNumber(*rigidBodyValue, "mass", 1.0f);
                sceneObject.physics.rigidBody.linearDamping = GetJsonNumber(*rigidBodyValue, "linearDamping", 0.12f);
                sceneObject.physics.rigidBody.restitution = GetJsonNumber(*rigidBodyValue, "restitution", 0.2f);
                sceneObject.physics.rigidBody.velocity = GetJsonFloat3(*rigidBodyValue, "velocity", {0.0f, 0.0f, 0.0f});
            }

            const JsonValue* colliderValue = FindJsonField(*physicsValue, "collider");
            if (colliderValue && colliderValue->type == JsonValueType::Object) {
                sceneObject.physics.collider.enabled = GetJsonBool(*colliderValue, "enabled", false);
                sceneObject.physics.collider.shape =
                    PhysicsColliderShapeFromString(GetJsonString(*colliderValue, "shape", "Box"));
                sceneObject.physics.collider.isTrigger = GetJsonBool(*colliderValue, "isTrigger", false);
                sceneObject.physics.collider.centerOffset =
                    GetJsonFloat3(*colliderValue, "centerOffset", {0.0f, 0.0f, 0.0f});
                sceneObject.physics.collider.boxExtents =
                    GetJsonFloat3(*colliderValue, "boxExtents", {0.5f, 0.5f, 0.5f});
                sceneObject.physics.collider.sphereRadius =
                    GetJsonNumber(*colliderValue, "sphereRadius", 0.5f);
            }
        } else if (sceneObject.name == "Floor") {
            PhysicsSystem::InitializeDefaultCollider(sceneObject, false, true);
        }

        RefreshObjectBlueprintRuntime(sceneObject);

        m_gameObjects.push_back(sceneObject);
    }

    if (m_gameObjects.empty()) {
        ResetSceneToDefaults();
        return false;
    }

    RefreshSceneSavedDocument();
    return true;
}

bool DXRenderer::OpenActorAssetViewer(const std::wstring& path) {
    if (!m_device || !m_commandList || path.empty()) {
        return false;
    }

    m_engineState = EngineState::Editor;
    CloseBlueprintAssetEditor();
    CloseMaterialAssetEditor();

    if (m_editorUI.State.isActorViewerLoading) {
        return false;
    }

    m_actorViewerMaterial = nullptr;
    m_actorViewerAsset.reset();
    m_actorViewerMesh.reset();
    m_actorViewerBoundsCenter = {0.0f, 0.0f, 0.0f};
    m_actorViewerBoundsExtents = {0.5f, 0.5f, 0.5f};
    m_actorViewerBoundsRadius = 1.0f;

    m_editorUI.State.showActorAssetViewer = true;
    m_editorUI.State.actorViewerPath = path;
    m_editorUI.State.actorViewerTitle = fs::path(path).stem().string();
    m_editorUI.State.actorViewerSearch.clear();
    m_editorUI.State.actorViewerSearchActive = false;
    m_editorUI.State.actorViewerYaw = 0.65f;
    m_editorUI.State.actorViewerPitch = -0.28f;
    m_editorUI.State.actorViewerDistance = 4.0f;
    m_editorUI.State.actorViewerFov = 35.0f;
    m_editorUI.State.actorViewerShowFloor = true;
    m_editorUI.State.actorViewerShowSky = true;
    m_editorUI.State.actorViewerShowStats = true;
    m_editorUI.State.actorViewerAutoRotate = false;
    m_editorUI.State.actorViewerIsDragging = false;
    m_editorUI.State.selectedObj = -1;
    m_editorUI.State.isActorViewerLoading = true;
    m_editorUI.State.actorViewerMeshLoad = std::make_shared<ActorViewerMeshLoadJob>();
    if (m_standaloneActorViewerWindow && m_hwnd) {
        SetWindowTextW(m_hwnd, (L"Catalyst Asset Viewer - " + fs::path(path).stem().wstring()).c_str());
    }
    std::shared_ptr<ActorViewerMeshLoadJob> meshLoadJob = m_editorUI.State.actorViewerMeshLoad;
    std::thread([meshLoadJob, path]() {
        try {
            meshLoadJob->meshData = ParseActorAssetForPreview(path);
        } catch (...) {
            meshLoadJob->meshData = {};
        }
        meshLoadJob->completed.store(true, std::memory_order_release);
    }).detach();
    return true;
}

bool DXRenderer::OpenMaterialAssetEditor(const std::wstring& path) {
    if (!m_device || path.empty()) {
        return false;
    }

    m_engineState = EngineState::Editor;
    CloseBlueprintAssetEditor();
    CloseActorAssetViewer();

    m_materialEditorMaterial = LoadMaterialAsset(path);
    if (!m_materialEditorMaterial) {
        return false;
    }

    if (!m_materialEditorPreviewAsset) {
        m_materialEditorPreviewAsset = std::make_shared<Asset>();
        m_materialEditorPreviewAsset->id = -2;
        m_materialEditorPreviewAsset->type = AssetType::Mesh;
    }

    m_materialEditorPreviewAsset->name = "Material Preview";
    m_materialEditorPreviewAsset->sourcePath = WideToUtf8(path);
    m_materialEditorPreviewAsset->mesh = m_primitives["Sphere"];

    m_editorUI.State.showMaterialAssetViewer = true;
    m_editorUI.State.materialEditorPath = path;
    m_editorUI.State.materialEditorTitle = fs::path(path).stem().string();
    m_editorUI.State.materialEditorSearch.clear();
    m_editorUI.State.materialEditorSearchActive = false;
    m_editorUI.State.materialEditorPreviewMesh = 0;
    m_editorUI.State.materialEditorYaw = 0.65f;
    m_editorUI.State.materialEditorPitch = -0.28f;
    m_editorUI.State.materialEditorDistance = 3.2f;
    m_editorUI.State.materialEditorFov = 35.0f;
    m_editorUI.State.materialEditorIsDragging = false;
    m_editorUI.State.materialEditorShowFloor = true;
    m_editorUI.State.materialEditorShowSky = true;
    m_editorUI.State.materialEditorShowStats = true;
    m_editorUI.State.materialEditorAutoRotate = false;
    m_editorUI.State.currentBrowserPath = fs::path(path).parent_path().wstring();
    m_editorUI.State.currentProjectFolder = FindProjectRootFromAssetPath(path);
    RefreshMaterialEditorSavedDocument();
    if (m_standaloneMaterialEditorWindow && m_hwnd) {
        SetWindowTextW(m_hwnd, (L"Catalyst Material Editor - " + fs::path(path).stem().wstring()).c_str());
    }

    return true;
}

bool DXRenderer::OpenBlueprintAssetEditor(const std::wstring& path) {
    if (!m_device || path.empty()) {
        return false;
    }

    m_engineState = EngineState::Editor;
    CloseActorAssetViewer();
    CloseMaterialAssetEditor();

    m_editorUI.OpenBlueprintAssetEditor(path);
    m_editorUI.State.currentBrowserPath = fs::path(path).parent_path().wstring();
    m_editorUI.State.currentProjectFolder = FindProjectRootFromAssetPath(path);
    if (m_standaloneBlueprintEditorWindow && m_hwnd) {
        SetWindowTextW(m_hwnd, (L"Catalyst Blueprint Editor - " + fs::path(path).stem().wstring()).c_str());
    }

    return true;
}

bool DXRenderer::FinalizeActorAssetViewerLoad(const MeshData& meshData, const std::wstring& path) {
    if (!m_device || !m_commandList) {
        return false;
    }

    if (meshData.Vertices.empty() || meshData.Indices.empty()) {
        return false;
    }

    try {
        m_actorViewerMesh = std::make_unique<Mesh>(m_device.Get(), m_commandList.Get(),
                                                   meshData.Vertices.data(), meshData.Vertices.size(),
                                                   meshData.Indices.data(), meshData.Indices.size());
    } catch (const std::exception& exception) {
        DebugLog(std::string("Catalyst actor preview load failed: ") + exception.what());
        return false;
    } catch (...) {
        DebugLog("Catalyst actor preview load failed with an unknown exception.");
        return false;
    }

    m_actorViewerAsset = std::make_shared<Asset>();
    m_actorViewerAsset->id = -1;
    m_actorViewerAsset->name = fs::path(path).stem().string();
    m_actorViewerAsset->sourcePath = WideToUtf8(path);
    m_actorViewerAsset->mesh = m_actorViewerMesh.get();
    m_actorViewerMaterial = nullptr;

    const DirectX::BoundingBox& bounds = m_actorViewerMesh->GetBounds();
    m_actorViewerBoundsCenter = bounds.Center;
    m_actorViewerBoundsExtents = bounds.Extents;
    m_actorViewerBoundsRadius = ComputePreviewRadius(bounds.Extents);

    m_editorUI.State.actorViewerPath = path;
    m_editorUI.State.actorViewerTitle = fs::path(path).stem().string();
    m_editorUI.State.actorViewerYaw = 0.65f;
    m_editorUI.State.actorViewerPitch = -0.28f;
    m_editorUI.State.actorViewerDistance = (std::max)(2.2f, m_actorViewerBoundsRadius * 3.4f);
    m_editorUI.State.actorViewerIsDragging = false;
    return true;
}

void DXRenderer::CloseActorAssetViewer() {
    m_editorUI.State.showActorAssetViewer = false;
    m_editorUI.State.actorViewerPath.clear();
    m_editorUI.State.actorViewerTitle.clear();
    m_editorUI.State.actorViewerIsDragging = false;
    m_editorUI.State.isActorViewerLoading = false;
    m_editorUI.State.actorViewerMeshLoad.reset();
    m_actorViewerMaterial = nullptr;
    m_actorViewerAsset.reset();
    m_actorViewerMesh.reset();
    m_actorViewerBoundsCenter = {0.0f, 0.0f, 0.0f};
    m_actorViewerBoundsExtents = {0.5f, 0.5f, 0.5f};
    m_actorViewerBoundsRadius = 1.0f;
}

void DXRenderer::CloseMaterialAssetEditor() {
    m_editorUI.State.showMaterialAssetViewer = false;
    m_editorUI.State.materialEditorPath.clear();
    m_editorUI.State.materialEditorTitle.clear();
    m_editorUI.State.materialEditorIsDragging = false;
    m_materialEditorMaterial = nullptr;
    m_savedMaterialDocument.clear();
    if (m_materialEditorPreviewAsset) {
        m_materialEditorPreviewAsset->mesh = nullptr;
    }
}

void DXRenderer::CloseBlueprintAssetEditor() {
    m_editorUI.CloseBlueprintAssetEditor();
}

Texture* DXRenderer::LoadTextureAsset(const std::wstring& path) {
    const std::wstring normalizedPath = NormalizeAssetPath(path);
    if (normalizedPath.empty() || !fs::exists(normalizedPath)) {
        return nullptr;
    }

    auto cached = m_textureCache.find(normalizedPath);
    if (cached != m_textureCache.end()) {
        return cached->second.get();
    }

    auto texture = std::make_shared<Texture>();
    try {
        texture->Load(normalizedPath, m_device.Get(), m_commandQueue.Get());
    } catch (...) {
        return nullptr;
    }

    texture->SetBindlessIndex(m_bindlessManager.AddTexture(m_device.Get(), texture->GetResource()));
    if (texture->GetBindlessIndex() < 0) {
        return nullptr;
    }

    m_textureCache[normalizedPath] = texture;
    return texture.get();
}

void DXRenderer::SyncMaterialTextures(Material& material) {
    material.albedoTexture = material.albedoPath.empty() ? nullptr : LoadTextureAsset(material.ResolveLinkedTexturePath(material.albedoPath));
    material.normalTexture = material.normalPath.empty() ? nullptr : LoadTextureAsset(material.ResolveLinkedTexturePath(material.normalPath));
    material.roughnessTexture = material.roughnessPath.empty() ? nullptr : LoadTextureAsset(material.ResolveLinkedTexturePath(material.roughnessPath));
}

Material* DXRenderer::LoadMaterialAsset(const std::wstring& path) {
    const std::wstring normalizedPath = NormalizeAssetPath(path);
    if (normalizedPath.empty() || !fs::exists(normalizedPath)) {
        return nullptr;
    }

    std::error_code ec;
    const auto fileWriteTime = fs::last_write_time(normalizedPath, ec);
    auto cached = m_materialCache.find(normalizedPath);
    if (cached == m_materialCache.end()) {
        auto material = std::make_shared<Material>();
        if (!material->LoadFromFile(normalizedPath)) {
            return nullptr;
        }
        SyncMaterialTextures(*material);
        m_materialCache[normalizedPath] = material;
        return material.get();
    }

    if (!ec && cached->second->lastWriteTime != fileWriteTime) {
        cached->second->LoadFromFile(normalizedPath);
        SyncMaterialTextures(*cached->second);
    }

    return cached->second.get();
}

bool DXRenderer::BuildViewportRay(int mouseX, int mouseY, float topH, float viewW, float viewH,
                                  XMFLOAT3& rayOrigin, XMFLOAT3& rayDirection) const {
    if (viewW <= 0.0f || viewH <= 0.0f) {
        return false;
    }

    const float ndcX = (2.0f * static_cast<float>(mouseX)) / viewW - 1.0f;
    const float ndcY = 1.0f - (2.0f * (static_cast<float>(mouseY) - topH)) / viewH;

    const XMMATRIX viewProj = m_camera.GetViewMatrix() * m_camera.GetProjectionMatrix();
    XMVECTOR det;
    const XMMATRIX invViewProj = XMMatrixInverse(&det, viewProj);

    XMVECTOR nearPoint = XMVectorSet(ndcX, ndcY, 0.0f, 1.0f);
    XMVECTOR farPoint = XMVectorSet(ndcX, ndcY, 1.0f, 1.0f);

    nearPoint = XMVector3TransformCoord(nearPoint, invViewProj);
    farPoint = XMVector3TransformCoord(farPoint, invViewProj);

    XMStoreFloat3(&rayOrigin, nearPoint);
    XMStoreFloat3(&rayDirection, XMVector3Normalize(XMVectorSubtract(farPoint, nearPoint)));
    return true;
}

int DXRenderer::RaycastViewportObject(int mouseX, int mouseY, float topH, float viewW, float viewH) const {
    XMFLOAT3 rayOrigin = {};
    XMFLOAT3 rayDirection = {};
    if (!BuildViewportRay(mouseX, mouseY, topH, viewW, viewH, rayOrigin, rayDirection)) {
        return -1;
    }

    int hitIndex = -1;
    float closestDistance = FLT_MAX;

    for (int i = 0; i < static_cast<int>(m_gameObjects.size()); ++i) {
        const GameObject& obj = m_gameObjects[i];
        if (obj.name.find("Sky") != std::string::npos || obj.type == ObjectType::PostProcessVolume) {
            continue;
        }

        const float radius = (std::max)(0.5f, (std::max)(obj.scale.x, (std::max)(obj.scale.y, obj.scale.z)));
        float hitDistance = 0.0f;
        if (RayIntersectsSphere(rayOrigin, rayDirection, obj.position, radius, hitDistance) && hitDistance < closestDistance) {
            closestDistance = hitDistance;
            hitIndex = i;
        }
    }

    return hitIndex;
}

void DXRenderer::Initialize(HWND hwnd, int width, int height) {
    m_hwnd = hwnd;
    m_width = width; 
    m_height = height;
    m_lastFrameTick = GetTickCount64();

    ComPtr<IDXGIFactory4> factory; ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
    }
#endif

    ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));

    D3D12_COMMAND_QUEUE_DESC qDesc = {}; qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(m_device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&m_commandQueue)));

    DXGI_SWAP_CHAIN_DESC1 scDesc = {}; 
    scDesc.Width = width; 
    scDesc.Height = height; 
    scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; 
    scDesc.SampleDesc = { 1, 0 }; 
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; 
    scDesc.BufferCount = FrameCount; 
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> sc1; 
    ThrowIfFailed(factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &scDesc, nullptr, nullptr, &sc1));
    sc1.As(&m_swapChain); m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FrameCount }; 
    ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap)));
    uint32_t rtvSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < FrameCount; i++) { 
        ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]))); 
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle); 
        rtvHandle.ptr += rtvSize; 
    }

    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList))); 
    m_commandList->Close();

    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))); 
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    CreateDepthBuffer();
    CreateConstantBuffer(); 
    
    m_bindlessManager.Initialize(m_device.Get(), 1024);
    CreateDefaultTextures(); 

    m_shadowPass.Initialize(m_device.Get()); 
    m_quantaMeshPass.Initialize(m_device.Get());
    m_skyboxPass.Initialize(m_device.Get());

    m_uiRenderer.Initialize(m_device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);

    m_fontManager.Initialize(m_device.Get(), m_commandQueue.Get(), "C:\\Windows\\Fonts\\arial.ttf", 20.0f);
    m_fontManager.SetBindlessIndex(m_bindlessManager.AddTexture(m_device.Get(), m_fontManager.GetTextureAtlas()));

    m_uiContext.Initialize(&m_uiDrawList, &m_fontManager, g_InputManager);

    m_commandAllocator->Reset();
    m_commandList->Reset(m_commandAllocator.Get(), nullptr);
    
    MeshData cubeData = PrimitiveGenerator::CreateCube(1.0f, 1.0f, 1.0f);
    m_primitives["Cube"] = new Mesh(m_device.Get(), m_commandList.Get(), cubeData.Vertices.data(), cubeData.Vertices.size(), cubeData.Indices.data(), cubeData.Indices.size());

    MeshData sphereData = PrimitiveGenerator::CreateSphere(0.5f, 32, 32);
    m_primitives["Sphere"] = new Mesh(m_device.Get(), m_commandList.Get(), sphereData.Vertices.data(), sphereData.Vertices.size(), sphereData.Indices.data(), sphereData.Indices.size());

    MeshData planeData = PrimitiveGenerator::CreatePlane(10.0f, 10.0f);
    m_primitives["Plane"] = new Mesh(m_device.Get(), m_commandList.Get(), planeData.Vertices.data(), planeData.Vertices.size(), planeData.Indices.data(), planeData.Indices.size());

    MeshData cylinderData = PrimitiveGenerator::CreateCylinder(0.5f, 1.0f, 32);
    m_primitives["Cylinder"] = new Mesh(m_device.Get(), m_commandList.Get(), cylinderData.Vertices.data(), cylinderData.Vertices.size(), cylinderData.Indices.data(), cylinderData.Indices.size());

    m_skyboxPass.LoadHDR(m_device.Get(), m_commandList.Get(), L"sky.hdr");
    
    m_commandList->Close();
    ID3D12CommandList* uploadLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, uploadLists);
    FlushGPU();
    
    if (m_skyboxPass.GetHDRResource()) {
        const int skyboxIndex = m_bindlessManager.AddTexture(m_device.Get(), m_skyboxPass.GetHDRResource());
        if (skyboxIndex >= 0) {
            g_skyboxSrvIndex = static_cast<uint32_t>(skyboxIndex);
        }
    }

    auto cubeAsset = std::make_shared<Asset>(); cubeAsset->id = 0; cubeAsset->name = "Basic Cube"; cubeAsset->mesh = m_primitives["Cube"]; m_assets.push_back(cubeAsset);
    auto sphereAsset = std::make_shared<Asset>(); sphereAsset->id = 1; sphereAsset->name = "Basic Sphere"; sphereAsset->mesh = m_primitives["Sphere"]; m_assets.push_back(sphereAsset);
    auto planeAsset = std::make_shared<Asset>(); planeAsset->id = 2; planeAsset->name = "Basic Plane"; planeAsset->mesh = m_primitives["Plane"]; m_assets.push_back(planeAsset);
    auto cylinderAsset = std::make_shared<Asset>(); cylinderAsset->id = 3; cylinderAsset->name = "Basic Cylinder"; cylinderAsset->mesh = m_primitives["Cylinder"]; m_assets.push_back(cylinderAsset);

    m_camera.SetProjection(45.0f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 5000.0f);
    ResetSceneToDefaults();
}

void DXRenderer::OnResize(int width, int height) {
    if (width == 0 || height == 0) return;
    FlushGPU();
    m_width = width;
    m_height = height;

    for (int i = 0; i < FrameCount; ++i) m_renderTargets[i].Reset();
    m_depthBuffer.Reset();

    ThrowIfFailed(m_swapChain->ResizeBuffers(FrameCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0));
    m_frameIndex = 0;

    uint32_t rtvSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < FrameCount; i++) {
        ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])));
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += rtvSize;
    }

    CreateDepthBuffer();
}

void DXRenderer::Render() {
    m_commandAllocator->Reset(); 
    m_commandList->Reset(m_commandAllocator.Get(), nullptr);

    float topH = 65.0f;
    float rightW = 350.0f;
    float bottomH = 250.0f;
    float w = (float)m_width;
    float h = (float)m_height;
    float viewW = (std::max)(1.0f, w - rightW);
    float viewH = (std::max)(1.0f, h - topH - bottomH);
    ULONGLONG frameTick = GetTickCount64();
    float deltaTime = 0.0f;
    if (m_lastFrameTick != 0) {
        deltaTime = (std::min)(0.1f, static_cast<float>(frameTick - m_lastFrameTick) / 1000.0f);
    }
    m_lastFrameTick = frameTick;

    m_editorUI.State.mx = g_InputManager ? g_InputManager->GetMouseX() : 0;
    m_editorUI.State.my = g_InputManager ? g_InputManager->GetMouseY() : 0;
    bool sceneMouseInViewport = false;
    if (m_showClosePrompt) {
        const float popupW = std::clamp(w * 0.34f, 460.0f, 580.0f);
        const float popupH = m_closePromptError.empty() ? 250.0f : 290.0f;
        const float popupX = (w - popupW) * 0.5f;
        const float popupY = (h - popupH) * 0.5f;
        m_uiContext.SetModalRegion(popupX, popupY, popupW, popupH);
    } else {
        m_uiContext.ClearModalRegion();
    }

    
    if (m_engineState == EngineState::Launcher) {
        m_editorUI.DrawLauncher(this, w, h);
    } else if (m_engineState == EngineState::ProjectLoading) {
        m_editorUI.DrawProjectLoading(this, w, h);
    } else {
        m_editorUI.DrawEditor(this, w, h, topH, rightW, bottomH);
        if (!m_editorUI.State.showActorAssetViewer &&
            !m_editorUI.State.showMaterialAssetViewer &&
            !m_editorUI.IsBlueprintEditorOpen() &&
            !m_showClosePrompt) {
            const bool playCameraOwnsView =
                m_editorUI.State.isPlaying &&
                std::any_of(m_gameObjects.begin(), m_gameObjects.end(), [](const GameObject& object) {
                    return object.blueprintPlayerControlled || object.blueprintPossessCamera;
                });
            sceneMouseInViewport =
                m_editorUI.State.mx >= 0 && m_editorUI.State.mx <= viewW &&
                m_editorUI.State.my >= topH && m_editorUI.State.my <= topH + viewH;
            m_camera.SetProjection(45.0f, viewW / viewH, 0.1f, 5000.0f);
            if (!playCameraOwnsView) {
                m_camera.Update(deltaTime, sceneMouseInViewport);
            }
            if (!m_editorUI.State.isPlaying) {
                m_editorUI.ProcessDragAndDrop(this, w, h, topH, viewW, viewH);
            }
        }
    }

    if (m_showClosePrompt) {
        DrawClosePrompt(w, h);
    }
    m_uiContext.ClearModalRegion();

    const bool canSimulateScenePhysics =
        m_engineState == EngineState::Editor &&
        !m_editorUI.State.showActorAssetViewer &&
        !m_editorUI.State.showMaterialAssetViewer &&
        !m_editorUI.IsBlueprintEditorOpen();

    if (canSimulateScenePhysics) {
        if (m_editorUI.State.isPlaying && !m_lastPlayMode) {
            m_playModeSnapshot = m_gameObjects;
            RefreshSceneBlueprintRuntime();
            m_runtimeWidgetInstances.clear();
            m_physicsSystem.Reset();
            m_jumpKeyWasDown = false;
            m_escapeKeyWasDown = false;
            m_playerMouseLookSuppressed = false;
            m_playerControllerPitch = 0.0f;
            m_playerControllerYaw = 0.0f;
            SetPlayerMouseLookLocked(false);
            for (const GameObject& object : m_gameObjects) {
                if (object.blueprintPlayerControlled) {
                    m_playerControllerYaw = object.rotation.y;
                    break;
                }
            }
        } else if (!m_editorUI.State.isPlaying && m_lastPlayMode) {
            if (!m_playModeSnapshot.empty()) {
                m_gameObjects = m_playModeSnapshot;
            }
            m_playModeSnapshot.clear();
            m_runtimeWidgetInstances.clear();
            m_physicsSystem.Reset();
            m_jumpKeyWasDown = false;
            m_escapeKeyWasDown = false;
            m_playerMouseLookSuppressed = false;
            SetPlayerMouseLookLocked(false);
        }

        m_lastPlayMode = m_editorUI.State.isPlaying;
        if (m_editorUI.State.isPlaying) {
            ApplyBlueprintGameplayNodes(deltaTime, topH, viewW, viewH, sceneMouseInViewport);
            m_physicsSystem.Step(m_gameObjects, deltaTime);
        }
    } else if (m_lastPlayMode) {
        if (!m_playModeSnapshot.empty()) {
            m_gameObjects = m_playModeSnapshot;
        }
        m_editorUI.State.isPlaying = false;
        m_lastPlayMode = false;
        m_playModeSnapshot.clear();
        m_runtimeWidgetInstances.clear();
        m_physicsSystem.Reset();
        m_jumpKeyWasDown = false;
        m_escapeKeyWasDown = false;
        m_playerMouseLookSuppressed = false;
        SetPlayerMouseLookLocked(false);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += m_frameIndex * m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_commandList->ResourceBarrier(1, &barrier);

    const bool showActorAssetViewer = m_editorUI.State.showActorAssetViewer && m_actorViewerAsset && m_actorViewerAsset->mesh;
    const bool showMaterialAssetViewer = m_editorUI.State.showMaterialAssetViewer && m_materialEditorPreviewAsset && m_materialEditorPreviewAsset->mesh;
    const bool showBlueprintEditor = m_editorUI.IsBlueprintEditorOpen();
    const float clearColor[] = {
        (showActorAssetViewer || showMaterialAssetViewer || showBlueprintEditor) ? 0.075f : 0.05f,
        (showActorAssetViewer || showMaterialAssetViewer || showBlueprintEditor) ? 0.082f : 0.05f,
        (showActorAssetViewer || showMaterialAssetViewer || showBlueprintEditor) ? 0.095f : 0.05f,
        1.0f
    };
    m_commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    ID3D12DescriptorHeap* heaps[] = { m_bindlessManager.GetHeap() }; 
    m_commandList->SetDescriptorHeaps(1, heaps);

    if (m_engineState == EngineState::Editor && !showBlueprintEditor) {
        if (showMaterialAssetViewer) {
            Mesh* previewMesh = m_materialEditorPreviewAsset->mesh;
            const DirectX::BoundingBox& bounds = previewMesh->GetBounds();
            const DirectX::XMFLOAT3 boundsCenter = bounds.Center;
            const DirectX::XMFLOAT3 boundsExtents = bounds.Extents;
            const float boundsRadius = ComputePreviewRadius(bounds.Extents);

            const float previewWidth = (std::max)(1.0f, m_editorUI.State.materialEditorViewportW);
            const float previewHeight = (std::max)(1.0f, m_editorUI.State.materialEditorViewportH);
            D3D12_VIEWPORT sceneViewport = { 0.0f, 0.0f, previewWidth, previewHeight, 0.0f, 1.0f };
            D3D12_RECT sceneScissor = { 0, 0, static_cast<LONG>(previewWidth), static_cast<LONG>(previewHeight) };
            m_commandList->RSSetViewports(1, &sceneViewport);
            m_commandList->RSSetScissorRects(1, &sceneScissor);

            const DirectX::XMFLOAT3 centeredOffset = {
                -boundsCenter.x,
                -boundsCenter.y,
                -boundsCenter.z
            };
            const DirectX::XMFLOAT3 previewTarget = {0.0f, boundsRadius * 0.12f, 0.0f};
            const float yaw = m_editorUI.State.materialEditorYaw;
            const float pitch = m_editorUI.State.materialEditorPitch;
            const float distance = (std::max)(1.0f, m_editorUI.State.materialEditorDistance);

            const DirectX::XMFLOAT3 previewPosition = {
                previewTarget.x + sinf(yaw) * cosf(pitch) * distance,
                previewTarget.y + sinf(pitch) * distance,
                previewTarget.z + cosf(yaw) * cosf(pitch) * distance
            };

            Camera previewCamera;
            previewCamera.SetProjection(m_editorUI.State.materialEditorFov, previewWidth / previewHeight, 0.05f, 5000.0f);
            previewCamera.SetLookAt(previewPosition, previewTarget);

            std::vector<GameObject> previewObjects;
            previewObjects.reserve(2);

            if (m_editorUI.State.materialEditorShowFloor && m_assets.size() > 2) {
                GameObject floor;
                floor.name = "MaterialPreviewFloor";
                floor.position = {0.0f, -boundsExtents.y - 0.02f, 0.0f};
                const float floorScale = (std::max)(1.5f, boundsRadius * 0.7f);
                floor.scale = {floorScale, 1.0f, floorScale};
                floor.color = {0.72f, 0.74f, 0.78f, 1.0f};
                floor.asset = m_assets[2].get();
                previewObjects.push_back(floor);
            }

            GameObject previewObject;
            previewObject.name = "MaterialPreview";
            previewObject.position = centeredOffset;
            previewObject.scale = {1.0f, 1.0f, 1.0f};
            previewObject.color = {1.0f, 1.0f, 1.0f, 1.0f};
            previewObject.asset = m_materialEditorPreviewAsset.get();
            previewObject.assignedMaterial = m_materialEditorMaterial;
            previewObjects.push_back(previewObject);

            const XMMATRIX lightSpace = XMMatrixIdentity();
            const XMFLOAT3 lightDir = {-0.45f, -0.82f, 0.35f};
            m_quantaMeshPass.Render(m_commandList.Get(), m_device.Get(), previewObjects, previewCamera,
                                    static_cast<int>(previewWidth), static_cast<int>(previewHeight),
                                    lightSpace, lightDir, 1.45f, nullptr, m_bindlessManager.GetHeap(),
                                    m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
                                    m_frameHeapOffset, m_texWhite, m_texNormal, m_texBlack, nullptr, m_primitives["Sphere"]);

            if (m_editorUI.State.materialEditorShowSky) {
                m_skyboxPass.Render(
                    m_commandList.Get(),
                    m_device.Get(),
                    m_primitives["Cube"],
                    previewCamera,
                    m_bindlessManager.GetHeap(),
                    g_skyboxSrvIndex,
                    {0.82f, 0.89f, 0.98f, 1.0f},
                    {0.92f, 0.94f, 0.98f, 1.0f});
            }
        } else if (showActorAssetViewer) {
            const float previewWidth = (std::max)(1.0f, m_editorUI.State.actorViewerViewportW);
            const float previewHeight = (std::max)(1.0f, m_editorUI.State.actorViewerViewportH);
            D3D12_VIEWPORT sceneViewport = { 0.0f, 0.0f, previewWidth, previewHeight, 0.0f, 1.0f };
            D3D12_RECT sceneScissor = { 0, 0, static_cast<LONG>(previewWidth), static_cast<LONG>(previewHeight) };
            m_commandList->RSSetViewports(1, &sceneViewport);
            m_commandList->RSSetScissorRects(1, &sceneScissor);

            const DirectX::XMFLOAT3 centeredOffset = {
                -m_actorViewerBoundsCenter.x,
                -m_actorViewerBoundsCenter.y,
                -m_actorViewerBoundsCenter.z
            };
            const DirectX::XMFLOAT3 previewTarget = {0.0f, m_actorViewerBoundsRadius * 0.12f, 0.0f};
            const float yaw = m_editorUI.State.actorViewerYaw;
            const float pitch = m_editorUI.State.actorViewerPitch;
            const float distance = (std::max)(1.0f, m_editorUI.State.actorViewerDistance);

            const DirectX::XMFLOAT3 previewPosition = {
                previewTarget.x + sinf(yaw) * cosf(pitch) * distance,
                previewTarget.y + sinf(pitch) * distance,
                previewTarget.z + cosf(yaw) * cosf(pitch) * distance
            };

            Camera previewCamera;
            previewCamera.SetProjection(m_editorUI.State.actorViewerFov, previewWidth / previewHeight, 0.05f, 5000.0f);
            previewCamera.SetLookAt(previewPosition, previewTarget);

            std::vector<GameObject> previewObjects;
            previewObjects.reserve(2);

            if (m_editorUI.State.actorViewerShowFloor && m_assets.size() > 2) {
                GameObject floor;
                floor.name = "PreviewFloor";
                floor.position = {0.0f, -m_actorViewerBoundsExtents.y - 0.02f, 0.0f};
                const float floorScale = (std::max)(1.5f, m_actorViewerBoundsRadius * 0.7f);
                floor.scale = {floorScale, 1.0f, floorScale};
                floor.color = {0.72f, 0.74f, 0.78f, 1.0f};
                floor.asset = m_assets[2].get();
                previewObjects.push_back(floor);
            }

            GameObject previewObject;
            previewObject.name = "PreviewAsset";
            previewObject.position = centeredOffset;
            previewObject.scale = {1.0f, 1.0f, 1.0f};
            previewObject.color = {0.82f, 0.84f, 0.88f, 1.0f};
            previewObject.asset = m_actorViewerAsset.get();
            previewObject.assignedMaterial = m_actorViewerMaterial;
            previewObjects.push_back(previewObject);

            const XMMATRIX lightSpace = XMMatrixIdentity();
            const XMFLOAT3 lightDir = {-0.45f, -0.82f, 0.35f};
            m_quantaMeshPass.Render(m_commandList.Get(), m_device.Get(), previewObjects, previewCamera,
                                    static_cast<int>(previewWidth), static_cast<int>(previewHeight),
                                    lightSpace, lightDir, 1.45f, nullptr, m_bindlessManager.GetHeap(),
                                    m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
                                    m_frameHeapOffset, m_texWhite, m_texNormal, m_texBlack, nullptr, m_primitives["Sphere"]);

            if (m_editorUI.State.actorViewerShowSky) {
                m_skyboxPass.Render(
                    m_commandList.Get(),
                    m_device.Get(),
                    m_primitives["Cube"],
                    previewCamera,
                    m_bindlessManager.GetHeap(),
                    g_skyboxSrvIndex,
                    {0.82f, 0.89f, 0.98f, 1.0f},
                    {0.92f, 0.94f, 0.98f, 1.0f});
            }
        } else {
            D3D12_VIEWPORT sceneViewport = { 0.0f, topH, viewW, viewH, 0.0f, 1.0f };
            D3D12_RECT sceneScissor = { 0, (LONG)topH, (LONG)viewW, (LONG)(topH + viewH) };
            m_commandList->RSSetViewports(1, &sceneViewport);
            m_commandList->RSSetScissorRects(1, &sceneScissor);

            bool isGizmoHovered = false;
            if (m_editorUI.State.selectedObj >= 0 && m_editorUI.State.selectedObj < static_cast<int>(m_gameObjects.size())) {
                if (m_gameObjects[m_editorUI.State.selectedObj].name.find("Sky") == std::string::npos) {
                    m_uiContext.TransformGizmo(m_gameObjects[m_editorUI.State.selectedObj].position, m_camera, 0.0f, topH, viewW, viewH, isGizmoHovered);
                }
            }

            if (!m_editorUI.State.isPlaying &&
                g_InputManager->IsMouseButtonPressed(0) &&
                !m_editorUI.State.showPlaceActorsMenu &&
                !m_editorUI.State.showImportPopup &&
                !m_editorUI.State.showRenamePopup &&
                !m_editorUI.State.showContextMenu &&
                m_editorUI.State.draggedAssetIndex == -1) {
                if (m_editorUI.State.mx >= 0 && m_editorUI.State.mx <= viewW && m_editorUI.State.my >= topH && m_editorUI.State.my <= topH + viewH) {
                    if (!isGizmoHovered) {
                        m_editorUI.State.selectedObj = RaycastViewportObject(m_editorUI.State.mx, m_editorUI.State.my, topH, viewW, viewH);
                        m_editorUI.State.selectedContentAsset = -1;
                    }
                }
            }

            XMMATRIX lightSpace = XMMatrixIdentity();
            XMFLOAT3 lightDir = {0, -1, 0};
            m_quantaMeshPass.Render(m_commandList.Get(), m_device.Get(), m_gameObjects, m_camera, static_cast<int>(viewW), static_cast<int>(viewH), 
                                    lightSpace, lightDir, 1.0f, nullptr, m_bindlessManager.GetHeap(), 
                                    m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV), 
                                    m_frameHeapOffset, m_texWhite, m_texNormal, m_texBlack, nullptr, m_primitives["Sphere"]);

            const GameObject* skyObject = nullptr;
            for (auto& o : m_gameObjects) {
                if (o.name.find("Sky") != std::string::npos) {
                    skyObject = &o;
                    break;
                }
            }
            if (skyObject) {
                m_skyboxPass.Render(
                    m_commandList.Get(),
                    m_device.Get(),
                    m_primitives["Cube"],
                    m_camera,
                    m_bindlessManager.GetHeap(),
                    g_skyboxSrvIndex,
                    skyObject->color,
                    skyObject->skyHorizonColor);
            }
        }
    }

    D3D12_VIEWPORT fullViewport = { 0.0f, 0.0f, w, h, 0.0f, 1.0f };
    D3D12_RECT fullScissor = { 0, 0, (LONG)w, (LONG)h };
    m_commandList->RSSetViewports(1, &fullViewport);
    m_commandList->RSSetScissorRects(1, &fullScissor);

    if (m_engineState == EngineState::Editor &&
        m_editorUI.State.isPlaying &&
        !showActorAssetViewer &&
        !showMaterialAssetViewer &&
        !showBlueprintEditor) {
        DrawRuntimeBlueprintWidgets(0.0f, topH, viewW, viewH);
    }

    m_uiRenderer.Render(m_commandList.Get(), m_uiDrawList, w, h, m_bindlessManager.GetHeap());
    m_uiDrawList.Clear();

   
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    m_commandList->ResourceBarrier(1, &barrier);

    m_commandList->Close();
    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);

    m_swapChain->Present(1, 0);
    FlushGPU();
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    if (m_editorUI.State.triggerEditorSwap) {
        m_editorUI.State.triggerEditorSwap = false;
        m_engineState = EngineState::ProjectLoading;
        m_projectLoadingOverlayPresented = false;
        QueueProjectStartupSceneLoad(ResolveActiveProjectFilePath());
        SetWindowLongPtr(m_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(m_hwnd, HWND_TOP, 0, 0, screenW, screenH, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);
        ShowWindow(m_hwnd, SW_MAXIMIZE);
    } else if (m_engineState == EngineState::ProjectLoading && m_hasPendingProjectSceneLoad) {
        if (!m_projectLoadingOverlayPresented) {
            m_projectLoadingOverlayPresented = true;
        } else {
            ProcessPendingProjectSceneLoad();
            m_engineState = EngineState::Editor;
            m_projectLoadingOverlayPresented = false;
        }
    }

    RefreshWindowTitle();
}

void DXRenderer::CreateDefaultTextures() { 
    m_texWhite = new Texture(); m_texWhite->Create1x1Color(m_device.Get(), m_commandQueue.Get(), 0xFFFFFFFF); 
    m_texWhite->SetBindlessIndex(m_bindlessManager.AddTexture(m_device.Get(), m_texWhite->GetResource()));
    m_texBlack = new Texture(); m_texBlack->Create1x1Color(m_device.Get(), m_commandQueue.Get(), 0xFF000000); 
    m_texBlack->SetBindlessIndex(m_bindlessManager.AddTexture(m_device.Get(), m_texBlack->GetResource()));
    m_texNormal = new Texture(); m_texNormal->Create1x1Color(m_device.Get(), m_commandQueue.Get(), 0xFFFF7F7F); 
    m_texNormal->SetBindlessIndex(m_bindlessManager.AddTexture(m_device.Get(), m_texNormal->GetResource()));
}

void DXRenderer::CreateDepthBuffer() { 
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = { D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE }; 
    ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap))); 
    D3D12_RESOURCE_DESC depthDesc = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, (UINT64)m_width, (UINT)m_height, 1, 1, DXGI_FORMAT_D32_FLOAT, {1,0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL }; 
    D3D12_CLEAR_VALUE clearVal = { DXGI_FORMAT_D32_FLOAT }; clearVal.DepthStencil.Depth = 1.0f; 
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT }; 
    ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearVal, IID_PPV_ARGS(&m_depthBuffer))); 
    m_device->CreateDepthStencilView(m_depthBuffer.Get(), nullptr, m_dsvHeap->GetCPUDescriptorHandleForHeapStart()); 
}

void DXRenderer::CreateConstantBuffer() { 
    const UINT bufferSize = (sizeof(ConstantBufferData) + 255) & ~255; 
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_UPLOAD }; 
    D3D12_RESOURCE_DESC rd = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, (UINT64)bufferSize * 1000, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1,0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE }; 
    ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_constantBuffer))); 
    m_constantBuffer->Map(0, nullptr, (void**)&m_pCbvDataBegin); 
}

void DXRenderer::FlushGPU() { 
    m_fenceValue++; 
    m_commandQueue->Signal(m_fence.Get(), m_fenceValue); 
    if (m_fence->GetCompletedValue() < m_fenceValue) { 
        m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent); 
        WaitForSingleObject(m_fenceEvent, INFINITE); 
    } 
}

void DXRenderer::Shutdown() { 
    FlushGPU(); 
    SetPlayerMouseLookLocked(false);
    m_uiRenderer.Shutdown();
    CloseActorAssetViewer();
    CloseMaterialAssetEditor();
    m_materialCache.clear();
    m_textureCache.clear();
    for (auto& pair : m_primitives) delete pair.second; 
    delete m_texWhite; delete m_texBlack; delete m_texNormal; 
}
