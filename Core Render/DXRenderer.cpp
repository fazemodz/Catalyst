#include "DXRenderer.h"
#include <d3dcompiler.h>
#include <iostream>
#include <DirectXCollision.h> 
#include <algorithm>
#include <cfloat>
#include <cstring>
#include <cwctype>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>
#include <vector>
#include "PrimitiveGenerator.h"
#include "ModelLoader.h"
#include "../Blueprint/Nodes/BlueprintNodeLibrary.h"
#include "../Resources/PakFile.h"
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

std::string TrimAsciiWhitespace(const std::string& value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    return first >= last ? std::string() : std::string(first, last);
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

Catalyst::Vec3 ToCatalystVec3(const XMFLOAT3& value) {
    return Catalyst::Vec3{value.x, value.y, value.z};
}

XMFLOAT3 ToXmfFloat3(const Catalyst::Vec3& value) {
    return XMFLOAT3{value.x, value.y, value.z};
}

Catalyst::TransformData ToCatalystTransform(const GameObject& object) {
    Catalyst::TransformData transform;
    transform.position = ToCatalystVec3(object.position);
    transform.rotation = ToCatalystVec3(object.rotation);
    transform.scale = ToCatalystVec3(object.scale);
    return transform;
}

void ApplyCatalystTransform(GameObject& object, const Catalyst::TransformData& transform) {
    object.position = ToXmfFloat3(transform.position);
    object.rotation = ToXmfFloat3(transform.rotation);
    object.scale = ToXmfFloat3(transform.scale);
}

Catalyst::ColliderData ToCatalystCollider(const GameObject& object) {
    Catalyst::ColliderData collider;
    collider.enabled = object.physics.collider.enabled;
    collider.isTrigger = object.physics.collider.isTrigger;
    collider.shape = object.physics.collider.shape == PhysicsColliderShape::Sphere
        ? Catalyst::ColliderShape::Sphere
        : Catalyst::ColliderShape::Box;
    collider.centerOffset = ToCatalystVec3(object.physics.collider.centerOffset);
    collider.boxExtents = ToCatalystVec3(object.physics.collider.boxExtents);
    collider.sphereRadius = object.physics.collider.sphereRadius;
    return collider;
}

void ApplyCatalystCollider(GameObject& object, const Catalyst::ColliderData& collider) {
    object.physics.collider.enabled = collider.enabled;
    object.physics.collider.isTrigger = collider.isTrigger;
    object.physics.collider.shape = collider.shape == Catalyst::ColliderShape::Sphere
        ? PhysicsColliderShape::Sphere
        : PhysicsColliderShape::Box;
    object.physics.collider.centerOffset = ToXmfFloat3(collider.centerOffset);
    object.physics.collider.boxExtents = ToXmfFloat3(collider.boxExtents);
    object.physics.collider.sphereRadius = collider.sphereRadius;
}

Catalyst::Vec4 ToCatalystVec4(const XMFLOAT4& value) {
    return Catalyst::Vec4{value.x, value.y, value.z, value.w};
}

XMFLOAT4 ToXmfFloat4(const Catalyst::Vec4& value) {
    return XMFLOAT4{value.x, value.y, value.z, value.w};
}

Catalyst::RigidbodyData ToCatalystRigidBody(const GameObject& object) {
    Catalyst::RigidbodyData rigidBody;
    rigidBody.enabled = object.physics.rigidBody.enabled;
    rigidBody.bodyType = object.physics.rigidBody.bodyType == PhysicsBodyType::Static
        ? Catalyst::PhysicsBodyType::Static
        : (object.physics.rigidBody.bodyType == PhysicsBodyType::Kinematic
            ? Catalyst::PhysicsBodyType::Kinematic
            : Catalyst::PhysicsBodyType::Dynamic);
    rigidBody.useGravity = object.physics.rigidBody.useGravity;
    rigidBody.mass = object.physics.rigidBody.mass;
    rigidBody.linearDamping = object.physics.rigidBody.linearDamping;
    rigidBody.restitution = object.physics.rigidBody.restitution;
    rigidBody.velocity = ToCatalystVec3(object.physics.rigidBody.velocity);
    return rigidBody;
}

void ApplyCatalystRigidBody(GameObject& object, const Catalyst::RigidbodyData& rigidBody) {
    object.physics.rigidBody.enabled = rigidBody.enabled;
    object.physics.rigidBody.bodyType = rigidBody.bodyType == Catalyst::PhysicsBodyType::Static
        ? PhysicsBodyType::Static
        : (rigidBody.bodyType == Catalyst::PhysicsBodyType::Kinematic ? PhysicsBodyType::Kinematic : PhysicsBodyType::Dynamic);
    object.physics.rigidBody.useGravity = rigidBody.useGravity;
    object.physics.rigidBody.mass = rigidBody.mass;
    object.physics.rigidBody.linearDamping = rigidBody.linearDamping;
    object.physics.rigidBody.restitution = rigidBody.restitution;
    object.physics.rigidBody.velocity = ToXmfFloat3(rigidBody.velocity);
}

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

struct ScriptColliderWorldState {
    XMFLOAT3 center = {0.0f, 0.0f, 0.0f};
    XMFLOAT3 boxExtents = {0.5f, 0.5f, 0.5f};
    float sphereRadius = 0.5f;
};

struct ScriptCollisionEventState {
    bool intersects = false;
    bool isTrigger = false;
    XMFLOAT3 normal = {0.0f, 1.0f, 0.0f};
    float penetration = 0.0f;
};

XMFLOAT3 AddFloat3(const XMFLOAT3& a, const XMFLOAT3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

XMFLOAT3 SubtractFloat3(const XMFLOAT3& a, const XMFLOAT3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

XMFLOAT3 ScaleFloat3(const XMFLOAT3& value, float scalar) {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

XMFLOAT3 MultiplyFloat3(const XMFLOAT3& a, const XMFLOAT3& b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

XMFLOAT3 AbsFloat3(const XMFLOAT3& value) {
    return {std::abs(value.x), std::abs(value.y), std::abs(value.z)};
}

XMFLOAT3 TransformFloat3Normal(const XMFLOAT3& value, const XMMATRIX& matrix) {
    XMFLOAT3 transformed;
    XMStoreFloat3(&transformed, XMVector3TransformNormal(XMLoadFloat3(&value), matrix));
    return transformed;
}

float ClampFloatValue(float value, float minimum, float maximum) {
    return (std::max)(minimum, (std::min)(maximum, value));
}

float DotFloat3(const XMFLOAT3& a, const XMFLOAT3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float LengthSquaredFloat3(const XMFLOAT3& value) {
    return DotFloat3(value, value);
}

float MaxComponentFloat3(const XMFLOAT3& value) {
    return (std::max)(value.x, (std::max)(value.y, value.z));
}

ScriptColliderWorldState BuildScriptColliderWorldState(const GameObject& object) {
    ScriptColliderWorldState worldState;
    const XMFLOAT3 absoluteScale = AbsFloat3(object.scale);
    const XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw(object.rotation.x, object.rotation.y, object.rotation.z);
    const XMFLOAT3 scaledOffset = MultiplyFloat3(object.physics.collider.centerOffset, object.scale);
    const XMFLOAT3 rotatedOffset = TransformFloat3Normal(scaledOffset, rotationMatrix);
    worldState.center = AddFloat3(object.position, rotatedOffset);

    const XMFLOAT3 scaledExtents = {
        (std::max)(0.05f, object.physics.collider.boxExtents.x * absoluteScale.x),
        (std::max)(0.05f, object.physics.collider.boxExtents.y * absoluteScale.y),
        (std::max)(0.05f, object.physics.collider.boxExtents.z * absoluteScale.z)
    };
    const XMFLOAT3 axisX = TransformFloat3Normal({scaledExtents.x, 0.0f, 0.0f}, rotationMatrix);
    const XMFLOAT3 axisY = TransformFloat3Normal({0.0f, scaledExtents.y, 0.0f}, rotationMatrix);
    const XMFLOAT3 axisZ = TransformFloat3Normal({0.0f, 0.0f, scaledExtents.z}, rotationMatrix);
    worldState.boxExtents = {
        (std::max)(0.05f, std::abs(axisX.x) + std::abs(axisY.x) + std::abs(axisZ.x)),
        (std::max)(0.05f, std::abs(axisX.y) + std::abs(axisY.y) + std::abs(axisZ.y)),
        (std::max)(0.05f, std::abs(axisX.z) + std::abs(axisY.z) + std::abs(axisZ.z))
    };
    worldState.sphereRadius = (std::max)(0.05f, object.physics.collider.sphereRadius * MaxComponentFloat3(absoluteScale));
    return worldState;
}

ScriptCollisionEventState BuildScriptOverlapState(const GameObject& a, const GameObject& b) {
    ScriptCollisionEventState state;
    if (!a.physics.collider.enabled || !b.physics.collider.enabled) {
        return state;
    }

    const ScriptColliderWorldState worldA = BuildScriptColliderWorldState(a);
    const ScriptColliderWorldState worldB = BuildScriptColliderWorldState(b);
    state.isTrigger = a.physics.collider.isTrigger || b.physics.collider.isTrigger;

    if (a.physics.collider.shape == PhysicsColliderShape::Sphere &&
        b.physics.collider.shape == PhysicsColliderShape::Sphere) {
        const XMFLOAT3 delta = SubtractFloat3(worldB.center, worldA.center);
        const float radiusSum = worldA.sphereRadius + worldB.sphereRadius;
        const float distanceSquared = LengthSquaredFloat3(delta);
        if (distanceSquared > radiusSum * radiusSum) {
            return state;
        }

        const float distance = std::sqrt(distanceSquared);
        state.intersects = true;
        state.normal = distance > 0.00001f ? ScaleFloat3(delta, 1.0f / distance) : XMFLOAT3{0.0f, 1.0f, 0.0f};
        state.penetration = radiusSum - distance;
        return state;
    }

    if (a.physics.collider.shape == PhysicsColliderShape::Box &&
        b.physics.collider.shape == PhysicsColliderShape::Box) {
        const XMFLOAT3 delta = SubtractFloat3(worldB.center, worldA.center);
        const XMFLOAT3 overlap = {
            worldA.boxExtents.x + worldB.boxExtents.x - std::abs(delta.x),
            worldA.boxExtents.y + worldB.boxExtents.y - std::abs(delta.y),
            worldA.boxExtents.z + worldB.boxExtents.z - std::abs(delta.z)
        };
        if (overlap.x <= 0.0f || overlap.y <= 0.0f || overlap.z <= 0.0f) {
            return state;
        }

        state.intersects = true;
        state.penetration = overlap.x;
        state.normal = {delta.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f};
        if (overlap.y < state.penetration) {
            state.penetration = overlap.y;
            state.normal = {0.0f, delta.y >= 0.0f ? 1.0f : -1.0f, 0.0f};
        }
        if (overlap.z < state.penetration) {
            state.penetration = overlap.z;
            state.normal = {0.0f, 0.0f, delta.z >= 0.0f ? 1.0f : -1.0f};
        }
        return state;
    }

    const bool aSphere = a.physics.collider.shape == PhysicsColliderShape::Sphere;
    const ScriptColliderWorldState& sphere = aSphere ? worldA : worldB;
    const ScriptColliderWorldState& box = aSphere ? worldB : worldA;
    const XMFLOAT3 sphereLocal = SubtractFloat3(sphere.center, box.center);
    const XMFLOAT3 closestLocal = {
        ClampFloatValue(sphereLocal.x, -box.boxExtents.x, box.boxExtents.x),
        ClampFloatValue(sphereLocal.y, -box.boxExtents.y, box.boxExtents.y),
        ClampFloatValue(sphereLocal.z, -box.boxExtents.z, box.boxExtents.z)
    };
    const XMFLOAT3 closestPoint = AddFloat3(box.center, closestLocal);
    const XMFLOAT3 delta = SubtractFloat3(closestPoint, sphere.center);
    const float distanceSquared = LengthSquaredFloat3(delta);
    if (distanceSquared > sphere.sphereRadius * sphere.sphereRadius) {
        return state;
    }

    state.intersects = true;
    if (distanceSquared > 0.00001f) {
        const float distance = std::sqrt(distanceSquared);
        state.normal = ScaleFloat3(delta, 1.0f / distance);
        state.penetration = sphere.sphereRadius - distance;
    } else {
        state.normal = {sphereLocal.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f};
        state.penetration = sphere.sphereRadius;
    }
    if (!aSphere) {
        state.normal = ScaleFloat3(state.normal, -1.0f);
    }
    return state;
}

bool IsVirtualKeyDownNow(int virtualKey) {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

bool IsVirtualKeyPressedNow(int virtualKey) {
    return (GetAsyncKeyState(virtualKey) & 0x0001) != 0;
}

bool EvaluateActionState(const std::string& actionName, bool justPressed) {
    const std::string key = ToLowerAscii(TrimAsciiWhitespace(actionName));
    const auto readKey = [&](int virtualKey) {
        return justPressed ? IsVirtualKeyPressedNow(virtualKey) : IsVirtualKeyDownNow(virtualKey);
    };

    if (key == "jump") return readKey(VK_SPACE);
    if (key == "interact" || key == "use") return readKey('E');
    if (key == "sprint") return readKey(VK_SHIFT);
    if (key == "fire" || key == "primaryfire") return readKey(VK_LBUTTON);
    if (key == "secondaryfire" || key == "aim") return readKey(VK_RBUTTON);
    if (key == "crouch") return readKey(VK_CONTROL);
    if (key == "reload") return readKey('R');
    if (key == "pause") return readKey(VK_ESCAPE);
    return false;
}

float EvaluateAxisState(const std::string& axisName) {
    const std::string key = ToLowerAscii(TrimAsciiWhitespace(axisName));
    if (key == "horizontal" || key == "moveright") {
        return (IsVirtualKeyDownNow('D') ? 1.0f : 0.0f) - (IsVirtualKeyDownNow('A') ? 1.0f : 0.0f);
    }
    if (key == "vertical" || key == "moveforward") {
        return (IsVirtualKeyDownNow('W') ? 1.0f : 0.0f) - (IsVirtualKeyDownNow('S') ? 1.0f : 0.0f);
    }
    if (key == "up" || key == "moveup") {
        return (IsVirtualKeyDownNow('E') ? 1.0f : 0.0f) - (IsVirtualKeyDownNow('Q') ? 1.0f : 0.0f);
    }
    return 0.0f;
}

bool RayIntersectsAabb(const XMFLOAT3& origin, const XMFLOAT3& direction, const XMFLOAT3& center,
                       const XMFLOAT3& extents, float maxDistance, float& outDistance, XMFLOAT3& outNormal) {
    const XMFLOAT3 minPoint = {center.x - extents.x, center.y - extents.y, center.z - extents.z};
    const XMFLOAT3 maxPoint = {center.x + extents.x, center.y + extents.y, center.z + extents.z};
    float tMin = 0.0f;
    float tMax = maxDistance;
    outNormal = {0.0f, 1.0f, 0.0f};

    const auto testAxis = [&](float originAxis, float directionAxis, float minAxis, float maxAxis,
                              XMFLOAT3 negativeNormal, XMFLOAT3 positiveNormal) -> bool {
        if (std::abs(directionAxis) < 0.00001f) {
            return originAxis >= minAxis && originAxis <= maxAxis;
        }

        float invDirection = 1.0f / directionAxis;
        float t0 = (minAxis - originAxis) * invDirection;
        float t1 = (maxAxis - originAxis) * invDirection;
        XMFLOAT3 enterNormal = negativeNormal;
        if (t0 > t1) {
            std::swap(t0, t1);
            enterNormal = positiveNormal;
        }

        if (t0 > tMin) {
            tMin = t0;
            outNormal = enterNormal;
        }
        tMax = (std::min)(tMax, t1);
        return tMax >= tMin;
    };

    if (!testAxis(origin.x, direction.x, minPoint.x, maxPoint.x, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}) ||
        !testAxis(origin.y, direction.y, minPoint.y, maxPoint.y, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}) ||
        !testAxis(origin.z, direction.z, minPoint.z, maxPoint.z, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f})) {
        return false;
    }

    outDistance = tMin;
    return outDistance >= 0.0f && outDistance <= maxDistance;
}

struct EngineScriptStateBlob {
    std::map<std::string, std::string> values;
};

void WriteScriptStateFloat(void* context, const char* key, float value) {
    if (context == nullptr || key == nullptr) {
        return;
    }

    auto& blob = *static_cast<EngineScriptStateBlob*>(context);
    blob.values[key] = std::to_string(value);
}

void WriteScriptStateBool(void* context, const char* key, bool value) {
    if (context == nullptr || key == nullptr) {
        return;
    }

    auto& blob = *static_cast<EngineScriptStateBlob*>(context);
    blob.values[key] = value ? "true" : "false";
}

void WriteScriptStateString(void* context, const char* key, const char* value) {
    if (context == nullptr || key == nullptr) {
        return;
    }

    auto& blob = *static_cast<EngineScriptStateBlob*>(context);
    blob.values[key] = value != nullptr ? value : "";
}

bool ReadScriptStateFloat(void* context, const char* key, float* value) {
    if (context == nullptr || key == nullptr || value == nullptr) {
        return false;
    }

    const auto& blob = *static_cast<const EngineScriptStateBlob*>(context);
    const auto found = blob.values.find(key);
    if (found == blob.values.end()) {
        return false;
    }

    try {
        *value = std::stof(found->second);
        return true;
    } catch (...) {
        return false;
    }
}

bool ReadScriptStateBool(void* context, const char* key, bool* value) {
    if (context == nullptr || key == nullptr || value == nullptr) {
        return false;
    }

    const auto& blob = *static_cast<const EngineScriptStateBlob*>(context);
    const auto found = blob.values.find(key);
    if (found == blob.values.end()) {
        return false;
    }

    *value = (found->second == "true" || found->second == "1");
    return true;
}

bool ReadScriptStateString(void* context, const char* key, char* buffer, uint32_t bufferSize) {
    if (context == nullptr || key == nullptr || buffer == nullptr || bufferSize == 0) {
        return false;
    }

    const auto& blob = *static_cast<const EngineScriptStateBlob*>(context);
    const auto found = blob.values.find(key);
    if (found == blob.values.end()) {
        return false;
    }

    const size_t maxChars = static_cast<size_t>(bufferSize - 1);
    const size_t charsToCopy = (std::min)(maxChars, found->second.size());
    std::memcpy(buffer, found->second.data(), charsToCopy);
    buffer[charsToCopy] = '\0';
    return true;
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

uint32_t BrightenPackedColor(uint32_t color, float bias) {
    const auto Adjust = [bias](uint32_t value) {
        return static_cast<uint32_t>((std::min)(255.0f, value + bias * 255.0f));
    };

    const uint32_t a = (color >> 24) & 0xFF;
    const uint32_t r = Adjust((color >> 16) & 0xFF);
    const uint32_t g = Adjust((color >> 8) & 0xFF);
    const uint32_t b = Adjust(color & 0xFF);
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
    outFile << "  \"version\": 4,\n";
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
        outFile << "      \"tag\": \"" << EscapeJsonString(object.tag) << "\",\n";
        outFile << "      \"animationState\": \"" << EscapeJsonString(object.animationState) << "\",\n";
        outFile << "      \"layer\": " << object.layer << ",\n";
        outFile << "      \"enabled\": " << (object.enabled ? "true" : "false") << ",\n";
        outFile << "      \"type\": \"" << ObjectTypeToString(object.type) << "\",\n";
        outFile << "      \"id\": " << object.id << ",\n";
        outFile << "      \"parentId\": " << object.parentId << ",\n";
        outFile << "      \"inheritParentTransform\": " << (object.inheritParentTransform ? "true" : "false") << ",\n";
        outFile << "      \"assetId\": " << (object.asset ? object.asset->id : -1) << ",\n";
        outFile << "      \"assetName\": \"" << EscapeJsonString(object.asset ? object.asset->name : "") << "\",\n";
        outFile << "      \"assetSource\": \"" << EscapeJsonString(WideToUtf8(assetSourcePath)) << "\",\n";
        outFile << "      \"blueprintAsset\": \"" << EscapeJsonString(WideToUtf8(blueprintAssetPath)) << "\",\n";
        outFile << "      \"components\": [\n";
        for (size_t componentIndex = 0; componentIndex < object.nativeScriptComponents.size(); ++componentIndex) {
            const NativeScriptComponentDesc& component = object.nativeScriptComponents[componentIndex];
            outFile << "        {\n";
            outFile << "          \"type\": \"NativeScript\",\n";
            outFile << "          \"id\": " << component.id << ",\n";
            outFile << "          \"className\": \"" << EscapeJsonString(component.className) << "\",\n";
            outFile << "          \"enabled\": " << (component.enabled ? "true" : "false") << "\n";
            outFile << "        }" << (componentIndex + 1 < object.nativeScriptComponents.size() ? "," : "") << "\n";
        }
        outFile << "      ],\n";
        outFile << "      \"position\": ";
        WriteJsonFloat3(outFile, object.position);
        outFile << ",\n";
        outFile << "      \"rotation\": ";
        WriteJsonFloat3(outFile, object.rotation);
        outFile << ",\n";
        outFile << "      \"scale\": ";
        WriteJsonFloat3(outFile, object.scale);
        outFile << ",\n";
        outFile << "      \"localPosition\": ";
        WriteJsonFloat3(outFile, object.localPosition);
        outFile << ",\n";
        outFile << "      \"localRotation\": ";
        WriteJsonFloat3(outFile, object.localRotation);
        outFile << ",\n";
        outFile << "      \"localScale\": ";
        WriteJsonFloat3(outFile, object.localScale);
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
    if (m_editorUI.State.standalonePlay) {
        title = ResolveActiveProjectDisplayName();
    } else if (m_engineState == EngineState::ProjectLoading) {
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

void DXRenderer::PollScriptModuleHost() {
    m_scriptModuleHost.SetProject(ResolveActiveProjectFilePath());
    m_scriptModuleHost.PollSourceChanges(!m_editorUI.State.isPlaying);
}

void DXRenderer::StartNativeScriptRuntime() {
    StopNativeScriptRuntime();
    m_scriptTimers.clear();
    m_pendingDestroyedObjectIds.clear();

    std::wstring loadError;
    if (!m_scriptModuleHost.EnsureModuleLoaded(&loadError)) {
        if (!loadError.empty()) {
            DebugLog("Catalyst native script load failed: " + WideToUtf8(loadError));
        }
        return;
    }

    std::vector<SavedNativeScriptInstance> emptyState;
    RecreateNativeScriptsFromSavedState(emptyState, true);
}

void DXRenderer::StopNativeScriptRuntime() {
    m_scriptTimers.clear();
    for (RuntimeNativeScriptInstance& instance : m_runtimeNativeScripts) {
        if (instance.script == nullptr) {
            continue;
        }

        if (instance.started) {
            instance.script->OnStop();
        }
        m_scriptModuleHost.DestroyScript(instance.script);
        instance.script = nullptr;
    }

    m_runtimeNativeScripts.clear();
}

void DXRenderer::ApplyNativeScripts(float deltaTime) {
    for (RuntimeNativeScriptInstance& instance : m_runtimeNativeScripts) {
        if (instance.script == nullptr) {
            continue;
        }

        const GameObject* object = FindGameObjectById(instance.objectId);
        if (object == nullptr || !object->enabled) {
            continue;
        }

        instance.script->OnUpdate(deltaTime);
    }

    DispatchNativeScriptPhysicsEvents();
}

bool DXRenderer::HotReloadNativeScripts() {
    if (!m_scriptModuleHost.HasPendingReload()) {
        return false;
    }

    std::vector<SavedNativeScriptInstance> savedInstances;
    CollectSavedNativeScriptInstances(savedInstances);
    StopNativeScriptRuntime();

    std::wstring reloadError;
    if (!m_scriptModuleHost.PerformPendingReload(&reloadError)) {
        if (!reloadError.empty()) {
            DebugLog("Catalyst native script hot reload failed: " + WideToUtf8(reloadError));
        }
        RecreateNativeScriptsFromSavedState(savedInstances, false);
        return false;
    }

    RecreateNativeScriptsFromSavedState(savedInstances, false);
    return true;
}

void DXRenderer::CollectSavedNativeScriptInstances(std::vector<SavedNativeScriptInstance>& outSavedInstances) const {
    outSavedInstances.clear();
    outSavedInstances.reserve(m_runtimeNativeScripts.size());

    for (const RuntimeNativeScriptInstance& instance : m_runtimeNativeScripts) {
        if (instance.script == nullptr) {
            continue;
        }

        EngineScriptStateBlob blob;
        Catalyst::ScriptStateWriter writer;
        writer.context = &blob;
        writer.writeFloat = &WriteScriptStateFloat;
        writer.writeBool = &WriteScriptStateBool;
        writer.writeString = &WriteScriptStateString;
        instance.script->OnHotReloadSave(writer);

        SavedNativeScriptInstance savedInstance;
        savedInstance.objectId = instance.objectId;
        savedInstance.componentId = instance.componentId;
        savedInstance.className = instance.className;
        for (const auto& [key, value] : blob.values) {
            savedInstance.state.push_back({key, value});
        }
        outSavedInstances.push_back(std::move(savedInstance));
    }
}

void DXRenderer::RecreateNativeScriptsFromSavedState(const std::vector<SavedNativeScriptInstance>& savedInstances, bool callOnStartForFreshInstances) {
    m_runtimeNativeScripts.clear();

    auto findSavedInstance = [&](uint64_t objectId, uint64_t componentId, const std::string& className) -> const SavedNativeScriptInstance* {
        for (const SavedNativeScriptInstance& savedInstance : savedInstances) {
            if (savedInstance.objectId == objectId &&
                savedInstance.componentId == componentId &&
                savedInstance.className == className) {
                return &savedInstance;
            }
        }
        return nullptr;
    };

    for (GameObject& object : m_gameObjects) {
        if (!object.enabled) {
            continue;
        }
        for (NativeScriptComponentDesc& component : object.nativeScriptComponents) {
            component.className = TrimAsciiWhitespace(component.className);
            if (!component.enabled || component.className.empty()) {
                continue;
            }

            Catalyst::NativeScript* script = m_scriptModuleHost.CreateScript(component.className);
            if (script == nullptr) {
                DebugLog("Catalyst native script class could not be created: " + component.className);
                continue;
            }

            script->__CatalystBind(&m_nativeScriptHostApi, object.id, component.id);

            RuntimeNativeScriptInstance runtimeInstance;
            runtimeInstance.className = component.className;
            runtimeInstance.objectId = object.id;
            runtimeInstance.componentId = component.id;
            runtimeInstance.script = script;

            const SavedNativeScriptInstance* savedInstance = findSavedInstance(object.id, component.id, component.className);
            if (savedInstance != nullptr) {
                EngineScriptStateBlob blob;
                for (const SavedNativeScriptState& state : savedInstance->state) {
                    blob.values[state.key] = state.value;
                }

                Catalyst::ScriptStateReader reader;
                reader.context = &blob;
                reader.readFloat = &ReadScriptStateFloat;
                reader.readBool = &ReadScriptStateBool;
                reader.readString = &ReadScriptStateString;
                script->OnHotReloadLoad(reader);
                runtimeInstance.started = true;
            } else if (callOnStartForFreshInstances) {
                script->OnStart();
                runtimeInstance.started = true;
            }

            m_runtimeNativeScripts.push_back(runtimeInstance);
        }
    }
}

GameObject* DXRenderer::FindGameObjectById(uint64_t objectId) {
    for (GameObject& object : m_gameObjects) {
        if (object.id == objectId) {
            return &object;
        }
    }
    return nullptr;
}

const GameObject* DXRenderer::FindGameObjectById(uint64_t objectId) const {
    for (const GameObject& object : m_gameObjects) {
        if (object.id == objectId) {
            return &object;
        }
    }
    return nullptr;
}

void DXRenderer::SyncObjectLocalTransform(GameObject& object) {
    if (!object.inheritParentTransform || object.parentId == 0) {
        object.localPosition = object.position;
        object.localRotation = object.rotation;
        object.localScale = object.scale;
        return;
    }

    const GameObject* parent = FindGameObjectById(object.parentId);
    if (parent == nullptr) {
        object.parentId = 0;
        object.inheritParentTransform = false;
        object.localPosition = object.position;
        object.localRotation = object.rotation;
        object.localScale = object.scale;
        return;
    }

    const XMMATRIX parentRotation = XMMatrixRotationRollPitchYaw(parent->rotation.x, parent->rotation.y, parent->rotation.z);
    const XMMATRIX inverseRotation = XMMatrixInverse(nullptr, parentRotation);
    const XMFLOAT3 worldDelta = SubtractFloat3(object.position, parent->position);
    XMFLOAT3 unrotatedDelta;
    XMStoreFloat3(&unrotatedDelta, XMVector3TransformNormal(XMLoadFloat3(&worldDelta), inverseRotation));
    object.localPosition = {
        parent->scale.x != 0.0f ? unrotatedDelta.x / parent->scale.x : unrotatedDelta.x,
        parent->scale.y != 0.0f ? unrotatedDelta.y / parent->scale.y : unrotatedDelta.y,
        parent->scale.z != 0.0f ? unrotatedDelta.z / parent->scale.z : unrotatedDelta.z
    };
    object.localRotation = SubtractFloat3(object.rotation, parent->rotation);
    object.localScale = {
        parent->scale.x != 0.0f ? object.scale.x / parent->scale.x : object.scale.x,
        parent->scale.y != 0.0f ? object.scale.y / parent->scale.y : object.scale.y,
        parent->scale.z != 0.0f ? object.scale.z / parent->scale.z : object.scale.z
    };
}

void DXRenderer::UpdateWorldTransformForObject(GameObject& object, std::vector<uint64_t>& recursionStack) {
    if (!object.inheritParentTransform || object.parentId == 0) {
        return;
    }

    if (std::find(recursionStack.begin(), recursionStack.end(), object.id) != recursionStack.end()) {
        object.parentId = 0;
        object.inheritParentTransform = false;
        return;
    }

    GameObject* parent = FindGameObjectById(object.parentId);
    if (parent == nullptr || parent == &object) {
        object.parentId = 0;
        object.inheritParentTransform = false;
        return;
    }

    recursionStack.push_back(object.id);
    UpdateWorldTransformForObject(*parent, recursionStack);

    const XMMATRIX parentRotation = XMMatrixRotationRollPitchYaw(parent->rotation.x, parent->rotation.y, parent->rotation.z);
    const XMFLOAT3 scaledLocal = MultiplyFloat3(object.localPosition, parent->scale);
    const XMFLOAT3 rotatedLocal = TransformFloat3Normal(scaledLocal, parentRotation);
    object.position = AddFloat3(parent->position, rotatedLocal);
    object.rotation = AddFloat3(parent->rotation, object.localRotation);
    object.scale = MultiplyFloat3(parent->scale, object.localScale);
    recursionStack.pop_back();
}

void DXRenderer::UpdateAttachedObjectTransforms() {
    SyncAllRootObjectLocals();
    std::vector<uint64_t> recursionStack;
    for (GameObject& object : m_gameObjects) {
        UpdateWorldTransformForObject(object, recursionStack);
    }
}

void DXRenderer::SyncAllRootObjectLocals() {
    for (GameObject& object : m_gameObjects) {
        if (!object.inheritParentTransform || object.parentId == 0) {
            object.localPosition = object.position;
            object.localRotation = object.rotation;
            object.localScale = object.scale;
        }
    }
}

bool DXRenderer::SetObjectParent(uint64_t childObjectId, uint64_t parentObjectId, bool keepWorldTransform) {
    GameObject* child = FindGameObjectById(childObjectId);
    GameObject* parent = FindGameObjectById(parentObjectId);
    if (child == nullptr || parent == nullptr || child == parent) {
        return false;
    }

    uint64_t cursorParentId = parent->parentId;
    while (cursorParentId != 0) {
        if (cursorParentId == childObjectId) {
            return false;
        }
        const GameObject* nextParent = FindGameObjectById(cursorParentId);
        cursorParentId = nextParent != nullptr ? nextParent->parentId : 0;
    }

    child->parentId = parentObjectId;
    child->inheritParentTransform = true;
    if (keepWorldTransform) {
        SyncObjectLocalTransform(*child);
    } else {
        child->localPosition = child->position;
        child->localRotation = child->rotation;
        child->localScale = child->scale;
        UpdateAttachedObjectTransforms();
    }
    return true;
}

bool DXRenderer::ClearObjectParent(uint64_t childObjectId, bool keepWorldTransform) {
    GameObject* child = FindGameObjectById(childObjectId);
    if (child == nullptr) {
        return false;
    }

    if (!keepWorldTransform) {
        child->position = child->localPosition;
        child->rotation = child->localRotation;
        child->scale = child->localScale;
    }

    child->parentId = 0;
    child->inheritParentTransform = false;
    child->localPosition = child->position;
    child->localRotation = child->rotation;
    child->localScale = child->scale;
    return true;
}

GameObject* DXRenderer::DuplicateGameObject(uint64_t objectId) {
    const GameObject* source = FindGameObjectById(objectId);
    if (source == nullptr) {
        return nullptr;
    }

    GameObject cloneSeed;
    GameObject clone = *source;
    clone.id = cloneSeed.id;
    clone.name += "_Copy";
    clone.position.x += 1.0f;
    clone.parentId = 0;
    clone.inheritParentTransform = false;
    clone.localPosition = clone.position;
    clone.localRotation = clone.rotation;
    clone.localScale = clone.scale;
    for (NativeScriptComponentDesc& component : clone.nativeScriptComponents) {
        NativeScriptComponentDesc idSeed;
        component.id = idSeed.id;
    }

    m_gameObjects.push_back(std::move(clone));
    return &m_gameObjects.back();
}

GameObject* DXRenderer::InstantiateObjectFromAssetPath(const std::wstring& assetPath, const Catalyst::TransformData& transform) {
    Asset* asset = FindAssetBySourcePath(assetPath);
    if (asset == nullptr) {
        return nullptr;
    }

    GameObject object;
    object.name = fs::path(assetPath).stem().string();
    object.asset = asset;
    ApplyCatalystTransform(object, transform);
    object.localPosition = object.position;
    object.localRotation = object.rotation;
    object.localScale = object.scale;
    m_gameObjects.push_back(std::move(object));
    return &m_gameObjects.back();
}

void DXRenderer::FlushDestroyedGameObjects() {
    if (m_pendingDestroyedObjectIds.empty()) {
        return;
    }

    std::sort(m_pendingDestroyedObjectIds.begin(), m_pendingDestroyedObjectIds.end());
    m_pendingDestroyedObjectIds.erase(std::unique(m_pendingDestroyedObjectIds.begin(), m_pendingDestroyedObjectIds.end()),
                                      m_pendingDestroyedObjectIds.end());

    for (uint64_t objectId : m_pendingDestroyedObjectIds) {
        m_scriptTimers.erase(std::remove_if(m_scriptTimers.begin(), m_scriptTimers.end(),
                                            [&](const ScriptTimerEntry& timer) { return timer.objectId == objectId; }),
                             m_scriptTimers.end());
        m_runtimeNativeScripts.erase(std::remove_if(m_runtimeNativeScripts.begin(), m_runtimeNativeScripts.end(),
                                                    [&](RuntimeNativeScriptInstance& instance) {
                                                        if (instance.objectId != objectId) {
                                                            return false;
                                                        }
                                                        if (instance.script != nullptr) {
                                                            if (instance.started) {
                                                                instance.script->OnStop();
                                                            }
                                                            m_scriptModuleHost.DestroyScript(instance.script);
                                                            instance.script = nullptr;
                                                        }
                                                        return true;
                                                    }),
                                   m_runtimeNativeScripts.end());

        for (GameObject& object : m_gameObjects) {
            if (object.parentId == objectId) {
                object.parentId = 0;
                object.inheritParentTransform = false;
                object.localPosition = object.position;
                object.localRotation = object.rotation;
                object.localScale = object.scale;
            }
        }

        m_gameObjects.erase(std::remove_if(m_gameObjects.begin(), m_gameObjects.end(),
                                           [&](const GameObject& object) { return object.id == objectId; }),
                            m_gameObjects.end());
    }

    m_pendingDestroyedObjectIds.clear();
}

void DXRenderer::UpdateScriptTimers(float deltaTime) {
    if (deltaTime <= 0.0f || m_scriptTimers.empty()) {
        return;
    }

    for (size_t i = 0; i < m_scriptTimers.size();) {
        ScriptTimerEntry& timer = m_scriptTimers[i];
        timer.remainingSeconds -= deltaTime;
        if (timer.remainingSeconds > 0.0f) {
            ++i;
            continue;
        }

        RuntimeNativeScriptInstance* targetInstance = nullptr;
        for (RuntimeNativeScriptInstance& instance : m_runtimeNativeScripts) {
            if (instance.objectId == timer.objectId && instance.componentId == timer.componentId) {
                targetInstance = &instance;
                break;
            }
        }

        if (targetInstance != nullptr && targetInstance->script != nullptr) {
            targetInstance->script->OnTimer(timer.timerId);
        }

        if (timer.looping) {
            timer.remainingSeconds += (std::max)(0.001f, timer.intervalSeconds);
            ++i;
        } else {
            m_scriptTimers.erase(m_scriptTimers.begin() + static_cast<long long>(i));
        }
    }
}

bool DXRenderer::SaveScriptValue(const std::string& slotName, const std::string& key, const std::string& value) {
    const std::wstring projectFilePath = ResolveActiveProjectFilePath();
    if (projectFilePath.empty() || slotName.empty() || key.empty()) {
        return false;
    }

    std::map<std::string, std::string> entries;
    const fs::path savePath = fs::path(projectFilePath).parent_path() / L"SaveData" / (Utf8ToWide(slotName) + L".catalystsave");
    if (fs::exists(savePath)) {
        std::ifstream inputFile(savePath, std::ios::binary);
        const std::string content((std::istreambuf_iterator<char>(inputFile)), std::istreambuf_iterator<char>());
        JsonValue rootValue;
        JsonParser parser(content);
        if (parser.Parse(rootValue) && rootValue.type == JsonValueType::Object) {
            const JsonValue* entriesValue = FindJsonField(rootValue, "entries");
            if (entriesValue != nullptr && entriesValue->type == JsonValueType::Array) {
                for (const JsonValue& entry : entriesValue->arrayValue) {
                    if (entry.type != JsonValueType::Object) {
                        continue;
                    }
                    entries[GetJsonString(entry, "key")] = GetJsonString(entry, "value");
                }
            }
        }
    }

    entries[key] = value;
    std::error_code ec;
    fs::create_directories(savePath.parent_path(), ec);
    std::ofstream outputFile(savePath, std::ios::binary | std::ios::trunc);
    if (!outputFile.is_open()) {
        return false;
    }

    outputFile << "{\n  \"entries\": [\n";
    size_t index = 0;
    for (const auto& [entryKey, entryValue] : entries) {
        outputFile << "    {\"key\": \"" << EscapeJsonString(entryKey) << "\", \"value\": \"" << EscapeJsonString(entryValue) << "\"}";
        outputFile << (++index < entries.size() ? ",\n" : "\n");
    }
    outputFile << "  ]\n}\n";
    return outputFile.good();
}

bool DXRenderer::LoadScriptValue(const std::string& slotName, const std::string& key, std::string& outValue) const {
    const std::wstring projectFilePath = ResolveActiveProjectFilePath();
    if (projectFilePath.empty() || slotName.empty() || key.empty()) {
        return false;
    }

    const fs::path savePath = fs::path(projectFilePath).parent_path() / L"SaveData" / (Utf8ToWide(slotName) + L".catalystsave");
    std::ifstream inputFile(savePath, std::ios::binary);
    if (!inputFile.is_open()) {
        return false;
    }

    const std::string content((std::istreambuf_iterator<char>(inputFile)), std::istreambuf_iterator<char>());
    JsonValue rootValue;
    JsonParser parser(content);
    if (!parser.Parse(rootValue) || rootValue.type != JsonValueType::Object) {
        return false;
    }

    const JsonValue* entriesValue = FindJsonField(rootValue, "entries");
    if (entriesValue == nullptr || entriesValue->type != JsonValueType::Array) {
        return false;
    }

    for (const JsonValue& entry : entriesValue->arrayValue) {
        if (entry.type != JsonValueType::Object) {
            continue;
        }
        if (GetJsonString(entry, "key") == key) {
            outValue = GetJsonString(entry, "value");
            return true;
        }
    }

    return false;
}

NativeScriptComponentDesc* DXRenderer::FindNativeScriptComponent(GameObject& object, uint64_t componentId) {
    for (NativeScriptComponentDesc& component : object.nativeScriptComponents) {
        if (component.id == componentId) {
            return &component;
        }
    }
    return nullptr;
}

const NativeScriptComponentDesc* DXRenderer::FindNativeScriptComponent(const GameObject& object, uint64_t componentId) const {
    for (const NativeScriptComponentDesc& component : object.nativeScriptComponents) {
        if (component.id == componentId) {
            return &component;
        }
    }
    return nullptr;
}

void DXRenderer::ScriptHostLog(void* userData, const char* message) {
    const DXRenderer* renderer = static_cast<const DXRenderer*>(userData);
    (void)renderer;
    DebugLog(message != nullptr ? message : "");
}

bool DXRenderer::ScriptHostGetKeyDown(void* userData, uint32_t keyCode) {
    const DXRenderer* renderer = static_cast<const DXRenderer*>(userData);
    (void)renderer;
    return (GetAsyncKeyState(static_cast<int>(keyCode)) & 0x8000) != 0;
}

bool DXRenderer::ScriptHostGetKeyPressed(void* userData, uint32_t keyCode) {
    const DXRenderer* renderer = static_cast<const DXRenderer*>(userData);
    (void)renderer;
    return (GetAsyncKeyState(static_cast<int>(keyCode)) & 0x0001) != 0;
}

bool DXRenderer::ScriptHostGetActionDown(void* userData, const char* actionName) {
    const DXRenderer* renderer = static_cast<const DXRenderer*>(userData);
    (void)renderer;
    return EvaluateActionState(actionName != nullptr ? actionName : "", false);
}

bool DXRenderer::ScriptHostGetActionPressed(void* userData, const char* actionName) {
    const DXRenderer* renderer = static_cast<const DXRenderer*>(userData);
    (void)renderer;
    return EvaluateActionState(actionName != nullptr ? actionName : "", true);
}

float DXRenderer::ScriptHostGetAxis(void* userData, const char* axisName) {
    const DXRenderer* renderer = static_cast<const DXRenderer*>(userData);
    (void)renderer;
    return EvaluateAxisState(axisName != nullptr ? axisName : "");
}

float DXRenderer::ScriptHostGetDeltaTime(void* userData) {
    const DXRenderer* renderer = static_cast<const DXRenderer*>(userData);
    return renderer != nullptr ? renderer->m_lastScaledScriptDeltaTime : 0.0f;
}

float DXRenderer::ScriptHostGetUnscaledDeltaTime(void* userData) {
    const DXRenderer* renderer = static_cast<const DXRenderer*>(userData);
    return renderer != nullptr ? renderer->m_lastUnscaledScriptDeltaTime : 0.0f;
}

float DXRenderer::ScriptHostGetTimeScale(void* userData) {
    const DXRenderer* renderer = static_cast<const DXRenderer*>(userData);
    return renderer != nullptr ? renderer->m_scriptTimeScale : 1.0f;
}

bool DXRenderer::ScriptHostSetTimeScale(void* userData, float timeScale) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr) {
        return false;
    }

    renderer->m_scriptTimeScale = ClampFloatValue(timeScale, 0.0f, 5.0f);
    return true;
}

bool DXRenderer::ScriptHostGetObjectType(void* userData, Catalyst::ObjectId objectId, uint32_t* outObjectType) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || outObjectType == nullptr) {
        return false;
    }

    const GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr) {
        return false;
    }

    *outObjectType = static_cast<uint32_t>(object->type);
    return true;
}

bool DXRenderer::ScriptHostGetObjectEnabled(void* userData, Catalyst::ObjectId objectId, bool* outEnabled) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || outEnabled == nullptr) {
        return false;
    }

    const GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr) {
        return false;
    }

    *outEnabled = object->enabled;
    return true;
}

bool DXRenderer::ScriptHostSetObjectEnabled(void* userData, Catalyst::ObjectId objectId, bool enabled) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr) {
        return false;
    }

    GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr) {
        return false;
    }

    object->enabled = enabled;
    return true;
}

bool DXRenderer::ScriptHostGetObjectTransform(void* userData, Catalyst::ObjectId objectId, Catalyst::TransformData* outTransform) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || outTransform == nullptr) {
        return false;
    }

    const GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr) {
        return false;
    }

    *outTransform = ToCatalystTransform(*object);
    return true;
}

bool DXRenderer::ScriptHostSetObjectTransform(void* userData, Catalyst::ObjectId objectId, const Catalyst::TransformData* transform) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || transform == nullptr) {
        return false;
    }

    GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr) {
        return false;
    }

    ApplyCatalystTransform(*object, *transform);
    renderer->SyncObjectLocalTransform(*object);
    return true;
}

bool DXRenderer::ScriptHostTranslateObject(void* userData, Catalyst::ObjectId objectId, Catalyst::Vec3 delta) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr) {
        return false;
    }

    GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr) {
        return false;
    }

    object->position.x += delta.x;
    object->position.y += delta.y;
    object->position.z += delta.z;
    renderer->SyncObjectLocalTransform(*object);
    return true;
}

bool DXRenderer::ScriptHostCopyObjectName(void* userData, Catalyst::ObjectId objectId, char* buffer, uint32_t bufferSize) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || buffer == nullptr || bufferSize == 0) {
        return false;
    }

    const GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr) {
        return false;
    }

    const size_t maxChars = static_cast<size_t>(bufferSize - 1);
    const size_t charsToCopy = (std::min)(maxChars, object->name.size());
    std::memcpy(buffer, object->name.data(), charsToCopy);
    buffer[charsToCopy] = '\0';
    return true;
}

bool DXRenderer::ScriptHostCopyObjectTag(void* userData, Catalyst::ObjectId objectId, char* buffer, uint32_t bufferSize) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || buffer == nullptr || bufferSize == 0) {
        return false;
    }

    const GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr) {
        return false;
    }

    const size_t maxChars = static_cast<size_t>(bufferSize - 1);
    const size_t charsToCopy = (std::min)(maxChars, object->tag.size());
    std::memcpy(buffer, object->tag.data(), charsToCopy);
    buffer[charsToCopy] = '\0';
    return true;
}

bool DXRenderer::ScriptHostSetObjectTag(void* userData, Catalyst::ObjectId objectId, const char* tag) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr) {
        return false;
    }

    GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr) {
        return false;
    }

    object->tag = TrimAsciiWhitespace(tag != nullptr ? tag : "");
    return true;
}

bool DXRenderer::ScriptHostGetObjectLayer(void* userData, Catalyst::ObjectId objectId, uint32_t* outLayer) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || outLayer == nullptr) {
        return false;
    }

    const GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr) {
        return false;
    }

    *outLayer = object->layer;
    return true;
}

bool DXRenderer::ScriptHostSetObjectLayer(void* userData, Catalyst::ObjectId objectId, uint32_t layer) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr) {
        return false;
    }

    GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr) {
        return false;
    }

    object->layer = layer;
    return true;
}

Catalyst::ObjectId DXRenderer::ScriptHostFindObjectByName(void* userData, const char* name) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr) {
        return 0;
    }

    const std::string targetName = TrimAsciiWhitespace(name != nullptr ? name : "");
    for (const GameObject& object : renderer->m_gameObjects) {
        if (object.name == targetName) {
            return object.id;
        }
    }
    return 0;
}

Catalyst::ObjectId DXRenderer::ScriptHostFindFirstObjectWithTag(void* userData, const char* tag) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr) {
        return 0;
    }

    const std::string targetTag = TrimAsciiWhitespace(tag != nullptr ? tag : "");
    for (const GameObject& object : renderer->m_gameObjects) {
        if (object.tag == targetTag) {
            return object.id;
        }
    }
    return 0;
}

uint32_t DXRenderer::ScriptHostGetObjectsWithTag(void* userData, const char* tag, Catalyst::ObjectId* outObjects, uint32_t capacity) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || outObjects == nullptr || capacity == 0) {
        return 0;
    }

    const std::string targetTag = TrimAsciiWhitespace(tag != nullptr ? tag : "");
    uint32_t count = 0;
    for (const GameObject& object : renderer->m_gameObjects) {
        if (object.tag != targetTag) {
            continue;
        }
        if (count >= capacity) {
            break;
        }
        outObjects[count++] = object.id;
    }
    return count;
}

bool DXRenderer::ScriptHostAttachObject(void* userData, Catalyst::ObjectId childObjectId, Catalyst::ObjectId parentObjectId, bool keepWorldTransform) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    return renderer != nullptr && renderer->SetObjectParent(childObjectId, parentObjectId, keepWorldTransform);
}

bool DXRenderer::ScriptHostDetachObject(void* userData, Catalyst::ObjectId childObjectId, bool keepWorldTransform) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    return renderer != nullptr && renderer->ClearObjectParent(childObjectId, keepWorldTransform);
}

Catalyst::ObjectId DXRenderer::ScriptHostDuplicateObject(void* userData, Catalyst::ObjectId objectId) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr) {
        return 0;
    }

    GameObject* duplicate = renderer->DuplicateGameObject(objectId);
    return duplicate != nullptr ? duplicate->id : 0;
}

bool DXRenderer::ScriptHostDestroyObject(void* userData, Catalyst::ObjectId objectId) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || renderer->FindGameObjectById(objectId) == nullptr) {
        return false;
    }

    renderer->m_pendingDestroyedObjectIds.push_back(objectId);
    return true;
}

Catalyst::ObjectId DXRenderer::ScriptHostInstantiateObjectFromAssetPath(void* userData, const char* assetPath, const Catalyst::TransformData* transform) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || assetPath == nullptr || transform == nullptr) {
        return 0;
    }

    const std::wstring projectFilePath = renderer->ResolveActiveProjectFilePath();
    const std::wstring projectRoot = projectFilePath.empty() ? L"" : fs::path(projectFilePath).parent_path().wstring();
    const std::wstring resolvedPath = projectRoot.empty()
        ? NormalizeAssetPath(Utf8ToWide(assetPath))
        : ResolveSceneReferencePath(projectRoot, assetPath);

    GameObject* object = renderer->InstantiateObjectFromAssetPath(resolvedPath, *transform);
    return object != nullptr ? object->id : 0;
}

bool DXRenderer::ScriptHostGetObjectCollider(void* userData, Catalyst::ObjectId objectId, Catalyst::ColliderData* outCollider) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || outCollider == nullptr) {
        return false;
    }

    const GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr) {
        return false;
    }

    *outCollider = ToCatalystCollider(*object);
    return true;
}

bool DXRenderer::ScriptHostSetObjectCollider(void* userData, Catalyst::ObjectId objectId, const Catalyst::ColliderData* collider) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || collider == nullptr) {
        return false;
    }

    GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr) {
        return false;
    }

    ApplyCatalystCollider(*object, *collider);
    return true;
}

bool DXRenderer::ScriptHostGetObjectRigidBody(void* userData, Catalyst::ObjectId objectId, Catalyst::RigidbodyData* outRigidBody) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || outRigidBody == nullptr) {
        return false;
    }

    const GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr) {
        return false;
    }

    *outRigidBody = ToCatalystRigidBody(*object);
    return true;
}

bool DXRenderer::ScriptHostSetObjectRigidBody(void* userData, Catalyst::ObjectId objectId, const Catalyst::RigidbodyData* rigidBody) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || rigidBody == nullptr) {
        return false;
    }

    GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr) {
        return false;
    }

    ApplyCatalystRigidBody(*object, *rigidBody);
    return true;
}

bool DXRenderer::ScriptHostAddForce(void* userData, Catalyst::ObjectId objectId, Catalyst::Vec3 force) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr) {
        return false;
    }

    GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr || !object->physics.rigidBody.enabled || object->physics.rigidBody.bodyType != PhysicsBodyType::Dynamic) {
        return false;
    }

    const float mass = (std::max)(0.001f, object->physics.rigidBody.mass);
    object->physics.rigidBody.velocity.x += (force.x / mass) * renderer->m_lastScaledScriptDeltaTime;
    object->physics.rigidBody.velocity.y += (force.y / mass) * renderer->m_lastScaledScriptDeltaTime;
    object->physics.rigidBody.velocity.z += (force.z / mass) * renderer->m_lastScaledScriptDeltaTime;
    return true;
}

bool DXRenderer::ScriptHostAddImpulse(void* userData, Catalyst::ObjectId objectId, Catalyst::Vec3 impulse) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr) {
        return false;
    }

    GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr || !object->physics.rigidBody.enabled || object->physics.rigidBody.bodyType != PhysicsBodyType::Dynamic) {
        return false;
    }

    const float mass = (std::max)(0.001f, object->physics.rigidBody.mass);
    object->physics.rigidBody.velocity.x += impulse.x / mass;
    object->physics.rigidBody.velocity.y += impulse.y / mass;
    object->physics.rigidBody.velocity.z += impulse.z / mass;
    return true;
}

bool DXRenderer::ScriptHostRaycast(void* userData, const Catalyst::Vec3* origin, const Catalyst::Vec3* direction, float maxDistance, Catalyst::RaycastHit* outHit) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || origin == nullptr || direction == nullptr || outHit == nullptr) {
        return false;
    }

    const XMFLOAT3 rayOrigin = ToXmfFloat3(*origin);
    const XMFLOAT3 rayDirection = ToXmfFloat3(*direction);
    float bestDistance = (std::max)(0.0f, maxDistance);
    bool didHit = false;
    Catalyst::RaycastHit bestHit;

    for (const GameObject& object : renderer->m_gameObjects) {
        if (!object.enabled || !object.physics.collider.enabled) {
            continue;
        }

        const ScriptColliderWorldState worldState = BuildScriptColliderWorldState(object);
        float hitDistance = 0.0f;
        XMFLOAT3 hitNormal = {0.0f, 1.0f, 0.0f};
        bool hit = false;
        if (object.physics.collider.shape == PhysicsColliderShape::Sphere) {
            hit = RayIntersectsSphere(rayOrigin, rayDirection, worldState.center, worldState.sphereRadius, hitDistance);
            if (hit) {
                const XMFLOAT3 point = AddFloat3(rayOrigin, ScaleFloat3(rayDirection, hitDistance));
                hitNormal = ScaleFloat3(SubtractFloat3(point, worldState.center), 1.0f / (std::max)(0.0001f, worldState.sphereRadius));
            }
        } else {
            hit = RayIntersectsAabb(rayOrigin, rayDirection, worldState.center, worldState.boxExtents, bestDistance, hitDistance, hitNormal);
        }

        if (!hit || hitDistance > bestDistance) {
            continue;
        }

        didHit = true;
        bestDistance = hitDistance;
        bestHit.hit = true;
        bestHit.objectId = object.id;
        bestHit.distance = hitDistance;
        bestHit.point = ToCatalystVec3(AddFloat3(rayOrigin, ScaleFloat3(rayDirection, hitDistance)));
        bestHit.normal = ToCatalystVec3(hitNormal);
        bestHit.isTrigger = object.physics.collider.isTrigger;
    }

    *outHit = bestHit;
    return didHit;
}

bool DXRenderer::ScriptHostCopyObjectMaterialPath(void* userData, Catalyst::ObjectId objectId, char* buffer, uint32_t bufferSize) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || buffer == nullptr || bufferSize == 0) {
        return false;
    }

    const GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr || object->assignedMaterial == nullptr) {
        return false;
    }

    const std::string materialPath = WideToUtf8(renderer->GetCachedMaterialPath(object->assignedMaterial));
    const size_t maxChars = static_cast<size_t>(bufferSize - 1);
    const size_t charsToCopy = (std::min)(maxChars, materialPath.size());
    std::memcpy(buffer, materialPath.data(), charsToCopy);
    buffer[charsToCopy] = '\0';
    return true;
}

bool DXRenderer::ScriptHostSetObjectMaterialPath(void* userData, Catalyst::ObjectId objectId, const char* materialPath) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || materialPath == nullptr) {
        return false;
    }

    GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr) {
        return false;
    }

    const std::wstring projectFilePath = renderer->ResolveActiveProjectFilePath();
    const std::wstring projectRoot = projectFilePath.empty() ? L"" : fs::path(projectFilePath).parent_path().wstring();
    const std::wstring resolvedPath = projectRoot.empty()
        ? NormalizeAssetPath(Utf8ToWide(materialPath))
        : ResolveSceneReferencePath(projectRoot, materialPath);
    object->assignedMaterial = renderer->LoadMaterialAsset(resolvedPath);
    return object->assignedMaterial != nullptr;
}

bool DXRenderer::ScriptHostGetObjectColor(void* userData, Catalyst::ObjectId objectId, Catalyst::Vec4* outColor) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || outColor == nullptr) {
        return false;
    }

    const GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr) {
        return false;
    }

    *outColor = ToCatalystVec4(object->color);
    return true;
}

bool DXRenderer::ScriptHostSetObjectColor(void* userData, Catalyst::ObjectId objectId, const Catalyst::Vec4* color) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || color == nullptr) {
        return false;
    }

    GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr) {
        return false;
    }

    object->color = ToXmfFloat4(*color);
    return true;
}

bool DXRenderer::ScriptHostGetLightIntensity(void* userData, Catalyst::ObjectId objectId, float* outIntensity) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || outIntensity == nullptr) {
        return false;
    }

    const GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr || object->type != ObjectType::Light) {
        return false;
    }

    *outIntensity = object->lightIntensity;
    return true;
}

bool DXRenderer::ScriptHostSetLightIntensity(void* userData, Catalyst::ObjectId objectId, float intensity) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr) {
        return false;
    }

    GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr || object->type != ObjectType::Light) {
        return false;
    }

    object->lightIntensity = (std::max)(0.0f, intensity);
    return true;
}

bool DXRenderer::ScriptHostGetMainCameraFov(void* userData, float* outFovDegrees) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || outFovDegrees == nullptr) {
        return false;
    }

    *outFovDegrees = renderer->m_scriptCameraFov;
    return true;
}

bool DXRenderer::ScriptHostSetMainCameraFov(void* userData, float fovDegrees) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr) {
        return false;
    }

    renderer->m_scriptCameraFov = ClampFloatValue(fovDegrees, 5.0f, 170.0f);
    return true;
}

Catalyst::WidgetId DXRenderer::ScriptHostFindWidgetByText(void* userData, Catalyst::ObjectId ownerObjectId, const char* text) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || text == nullptr) {
        return -1;
    }

    const std::string targetText = TrimAsciiWhitespace(text);
    for (auto& [key, instance] : renderer->m_runtimeWidgetInstances) {
        if (ownerObjectId != 0 && instance.ownerObjectIndex >= 0) {
            if (instance.ownerObjectIndex >= static_cast<int>(renderer->m_gameObjects.size())) {
                continue;
            }
            const GameObject& ownerObject = renderer->m_gameObjects[static_cast<size_t>(instance.ownerObjectIndex)];
            if (ownerObject.id != ownerObjectId) {
                continue;
            }
        }

        for (const RuntimeWidgetNode& node : instance.nodes) {
            if (node.displayText == targetText) {
                return node.id;
            }
        }
    }

    return -1;
}

bool DXRenderer::ScriptHostSetWidgetText(void* userData, Catalyst::ObjectId ownerObjectId, Catalyst::WidgetId widgetId, const char* text) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || text == nullptr) {
        return false;
    }

    for (auto& [key, instance] : renderer->m_runtimeWidgetInstances) {
        if (ownerObjectId != 0 && instance.ownerObjectIndex >= 0) {
            if (instance.ownerObjectIndex >= static_cast<int>(renderer->m_gameObjects.size())) {
                continue;
            }
            const GameObject& ownerObject = renderer->m_gameObjects[static_cast<size_t>(instance.ownerObjectIndex)];
            if (ownerObject.id != ownerObjectId) {
                continue;
            }
        }

        RuntimeWidgetNode* node = renderer->FindRuntimeWidgetNode(instance, widgetId);
        if (node != nullptr) {
            node->displayText = text;
            return true;
        }
    }

    return false;
}

bool DXRenderer::ScriptHostSetWidgetVisible(void* userData, Catalyst::ObjectId ownerObjectId, Catalyst::WidgetId widgetId, bool visible) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr) {
        return false;
    }

    for (auto& [key, instance] : renderer->m_runtimeWidgetInstances) {
        if (ownerObjectId != 0 && instance.ownerObjectIndex >= 0) {
            if (instance.ownerObjectIndex >= static_cast<int>(renderer->m_gameObjects.size())) {
                continue;
            }
            const GameObject& ownerObject = renderer->m_gameObjects[static_cast<size_t>(instance.ownerObjectIndex)];
            if (ownerObject.id != ownerObjectId) {
                continue;
            }
        }

        RuntimeWidgetNode* node = renderer->FindRuntimeWidgetNode(instance, widgetId);
        if (node != nullptr) {
            node->visibleInGame = visible;
            return true;
        }
    }

    return false;
}

bool DXRenderer::ScriptHostSetWidgetTint(void* userData, Catalyst::ObjectId ownerObjectId, Catalyst::WidgetId widgetId, const Catalyst::Vec4* color) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || color == nullptr) {
        return false;
    }

    const uint32_t packedColor =
        (static_cast<uint32_t>(ClampFloatValue(color->w, 0.0f, 1.0f) * 255.0f) << 24) |
        (static_cast<uint32_t>(ClampFloatValue(color->x, 0.0f, 1.0f) * 255.0f) << 16) |
        (static_cast<uint32_t>(ClampFloatValue(color->y, 0.0f, 1.0f) * 255.0f) << 8) |
        static_cast<uint32_t>(ClampFloatValue(color->z, 0.0f, 1.0f) * 255.0f);

    for (auto& [key, instance] : renderer->m_runtimeWidgetInstances) {
        if (ownerObjectId != 0 && instance.ownerObjectIndex >= 0) {
            if (instance.ownerObjectIndex >= static_cast<int>(renderer->m_gameObjects.size())) {
                continue;
            }
            const GameObject& ownerObject = renderer->m_gameObjects[static_cast<size_t>(instance.ownerObjectIndex)];
            if (ownerObject.id != ownerObjectId) {
                continue;
            }
        }

        RuntimeWidgetNode* node = renderer->FindRuntimeWidgetNode(instance, widgetId);
        if (node != nullptr) {
            node->tintColor = packedColor;
            return true;
        }
    }

    return false;
}

bool DXRenderer::ScriptHostSetTimer(void* userData, Catalyst::ObjectId objectId, Catalyst::ComponentId componentId, uint64_t timerId, float delaySeconds, bool looping) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || timerId == 0) {
        return false;
    }

    renderer->m_scriptTimers.erase(std::remove_if(renderer->m_scriptTimers.begin(), renderer->m_scriptTimers.end(),
                                                  [&](const ScriptTimerEntry& timer) {
                                                      return timer.objectId == objectId &&
                                                             timer.componentId == componentId &&
                                                             timer.timerId == timerId;
                                                  }),
                                   renderer->m_scriptTimers.end());
    renderer->m_scriptTimers.push_back({objectId, componentId, timerId, (std::max)(0.001f, delaySeconds),
                                        (std::max)(0.001f, delaySeconds), looping});
    return true;
}

bool DXRenderer::ScriptHostCancelTimer(void* userData, Catalyst::ObjectId objectId, Catalyst::ComponentId componentId, uint64_t timerId) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr) {
        return false;
    }

    const size_t oldSize = renderer->m_scriptTimers.size();
    renderer->m_scriptTimers.erase(std::remove_if(renderer->m_scriptTimers.begin(), renderer->m_scriptTimers.end(),
                                                  [&](const ScriptTimerEntry& timer) {
                                                      return timer.objectId == objectId &&
                                                             timer.componentId == componentId &&
                                                             timer.timerId == timerId;
                                                  }),
                                   renderer->m_scriptTimers.end());
    return renderer->m_scriptTimers.size() != oldSize;
}

bool DXRenderer::ScriptHostSaveString(void* userData, const char* slotName, const char* key, const char* value) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    return renderer != nullptr &&
           renderer->SaveScriptValue(slotName != nullptr ? slotName : "", key != nullptr ? key : "", value != nullptr ? value : "");
}

bool DXRenderer::ScriptHostSaveFloat(void* userData, const char* slotName, const char* key, float value) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    return renderer != nullptr &&
           renderer->SaveScriptValue(slotName != nullptr ? slotName : "", key != nullptr ? key : "", std::to_string(value));
}

bool DXRenderer::ScriptHostSaveBool(void* userData, const char* slotName, const char* key, bool value) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    return renderer != nullptr &&
           renderer->SaveScriptValue(slotName != nullptr ? slotName : "", key != nullptr ? key : "", value ? "true" : "false");
}

bool DXRenderer::ScriptHostLoadString(void* userData, const char* slotName, const char* key, char* buffer, uint32_t bufferSize) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || buffer == nullptr || bufferSize == 0) {
        return false;
    }

    std::string value;
    if (!renderer->LoadScriptValue(slotName != nullptr ? slotName : "", key != nullptr ? key : "", value)) {
        return false;
    }

    const size_t maxChars = static_cast<size_t>(bufferSize - 1);
    const size_t charsToCopy = (std::min)(maxChars, value.size());
    std::memcpy(buffer, value.data(), charsToCopy);
    buffer[charsToCopy] = '\0';
    return true;
}

bool DXRenderer::ScriptHostLoadFloat(void* userData, const char* slotName, const char* key, float* outValue) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || outValue == nullptr) {
        return false;
    }

    std::string value;
    if (!renderer->LoadScriptValue(slotName != nullptr ? slotName : "", key != nullptr ? key : "", value)) {
        return false;
    }

    try {
        *outValue = std::stof(value);
        return true;
    } catch (...) {
        return false;
    }
}

bool DXRenderer::ScriptHostLoadBool(void* userData, const char* slotName, const char* key, bool* outValue) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || outValue == nullptr) {
        return false;
    }

    std::string value;
    if (!renderer->LoadScriptValue(slotName != nullptr ? slotName : "", key != nullptr ? key : "", value)) {
        return false;
    }

    value = ToLowerAscii(value);
    *outValue = value == "true" || value == "1" || value == "yes";
    return true;
}

bool DXRenderer::ScriptHostCopyAnimationState(void* userData, Catalyst::ObjectId objectId, char* buffer, uint32_t bufferSize) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr || buffer == nullptr || bufferSize == 0) {
        return false;
    }

    const GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr) {
        return false;
    }

    const size_t maxChars = static_cast<size_t>(bufferSize - 1);
    const size_t charsToCopy = (std::min)(maxChars, object->animationState.size());
    std::memcpy(buffer, object->animationState.data(), charsToCopy);
    buffer[charsToCopy] = '\0';
    return true;
}

bool DXRenderer::ScriptHostSetAnimationState(void* userData, Catalyst::ObjectId objectId, const char* stateName) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr) {
        return false;
    }

    GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr) {
        return false;
    }

    object->animationState = stateName != nullptr ? stateName : "";
    return true;
}

bool DXRenderer::ScriptHostTriggerAnimation(void* userData, Catalyst::ObjectId objectId, const char* triggerName) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer == nullptr) {
        return false;
    }

    GameObject* object = renderer->FindGameObjectById(objectId);
    if (object == nullptr) {
        return false;
    }

    object->animationTrigger = triggerName != nullptr ? triggerName : "";
    if (!object->animationTrigger.empty()) {
        object->animationState = object->animationTrigger;
    }
    return true;
}

Catalyst::AudioHandle DXRenderer::ScriptHostPlayOneShot2D(void* userData, const char* assetPath, float volume) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer != nullptr) {
        DebugLog(std::string("Catalyst audio stub invoked for asset: ") + (assetPath != nullptr ? assetPath : "") +
                 " volume=" + std::to_string(volume));
    }
    return 0;
}

Catalyst::AudioHandle DXRenderer::ScriptHostPlayOneShot3D(void* userData, const char* assetPath, Catalyst::Vec3 position, float volume) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer != nullptr) {
        DebugLog(std::string("Catalyst 3D audio stub invoked for asset: ") + (assetPath != nullptr ? assetPath : "") +
                 " volume=" + std::to_string(volume) +
                 " position=(" + std::to_string(position.x) + "," + std::to_string(position.y) + "," + std::to_string(position.z) + ")");
    }
    return 0;
}

bool DXRenderer::ScriptHostStopAudio(void* userData, Catalyst::AudioHandle handle) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer != nullptr && handle != 0) {
        DebugLog("Catalyst audio stop is not backed by a runtime mixer yet.");
    }
    return false;
}

bool DXRenderer::ScriptHostSetAudioVolume(void* userData, Catalyst::AudioHandle handle, float volume) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer != nullptr && handle != 0) {
        DebugLog("Catalyst audio volume is not backed by a runtime mixer yet.");
    }
    (void)volume;
    return false;
}

bool DXRenderer::ScriptHostSetAudioPitch(void* userData, Catalyst::AudioHandle handle, float pitch) {
    DXRenderer* renderer = static_cast<DXRenderer*>(userData);
    if (renderer != nullptr && handle != 0) {
        DebugLog("Catalyst audio pitch is not backed by a runtime mixer yet.");
    }
    (void)pitch;
    return false;
}

void DXRenderer::DispatchNativeScriptPhysicsEvents() {
    for (RuntimeNativeScriptInstance& instance : m_runtimeNativeScripts) {
        if (instance.script == nullptr) {
            continue;
        }

        const GameObject* object = FindGameObjectById(instance.objectId);
        if (object == nullptr || !object->enabled || !object->physics.collider.enabled) {
            instance.activeCollisionObjects.clear();
            instance.activeTriggerObjects.clear();
            continue;
        }

        std::vector<uint64_t> currentCollisionObjects;
        std::vector<uint64_t> currentTriggerObjects;

        for (const GameObject& otherObject : m_gameObjects) {
            if (otherObject.id == object->id || !otherObject.enabled || !otherObject.physics.collider.enabled) {
                continue;
            }

            const ScriptCollisionEventState overlap = BuildScriptOverlapState(*object, otherObject);
            if (!overlap.intersects) {
                continue;
            }

            Catalyst::CollisionEvent event;
            event.otherObjectId = otherObject.id;
            event.normal = ToCatalystVec3(overlap.normal);
            event.penetration = overlap.penetration;
            event.otherIsTrigger = otherObject.physics.collider.isTrigger;

            if (overlap.isTrigger) {
                currentTriggerObjects.push_back(otherObject.id);
                const bool wasActive = std::find(instance.activeTriggerObjects.begin(), instance.activeTriggerObjects.end(), otherObject.id) != instance.activeTriggerObjects.end();
                if (!wasActive) {
                    instance.script->OnTriggerEnter(event);
                }
                instance.script->OnTriggerStay(event);
            } else {
                currentCollisionObjects.push_back(otherObject.id);
                const bool wasActive = std::find(instance.activeCollisionObjects.begin(), instance.activeCollisionObjects.end(), otherObject.id) != instance.activeCollisionObjects.end();
                if (!wasActive) {
                    instance.script->OnCollisionEnter(event);
                }
                instance.script->OnCollisionStay(event);
            }
        }

        for (uint64_t previousObjectId : instance.activeCollisionObjects) {
            if (std::find(currentCollisionObjects.begin(), currentCollisionObjects.end(), previousObjectId) == currentCollisionObjects.end()) {
                Catalyst::CollisionEvent exitEvent;
                exitEvent.otherObjectId = previousObjectId;
                instance.script->OnCollisionExit(exitEvent);
            }
        }

        for (uint64_t previousObjectId : instance.activeTriggerObjects) {
            if (std::find(currentTriggerObjects.begin(), currentTriggerObjects.end(), previousObjectId) == currentTriggerObjects.end()) {
                Catalyst::CollisionEvent exitEvent;
                exitEvent.otherObjectId = previousObjectId;
                exitEvent.otherIsTrigger = true;
                instance.script->OnTriggerExit(exitEvent);
            }
        }

        instance.activeCollisionObjects = std::move(currentCollisionObjects);
        instance.activeTriggerObjects = std::move(currentTriggerObjects);
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
        if (!object.enabled || !object.blueprintHasTrigger) {
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
        if (object.enabled && object.blueprintPlayerControlled) {
            controlledObject = &object;
            break;
        }
    }

    for (GameObject& object : m_gameObjects) {
        if (!object.enabled || &object == controlledObject || !object.blueprintPlayerControlled) {
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

    CookedUIBlueprint::Asset cookedAsset;
    bool loadedCookedAsset = false;

    const std::wstring projectRoot = FindProjectRootFromAssetPath(assetPath);
    const std::wstring cookedAssetPath = CookedUIBlueprint::GetCookedAssetPath(assetPath);
    if (!projectRoot.empty()) {
        const fs::path pakPath = fs::path(projectRoot) / L"data.pak";
        std::error_code pakEc;
        if (fs::exists(pakPath, pakEc) && !pakEc) {
            std::error_code relEc;
            const fs::path logicalCookedPath = fs::relative(fs::path(cookedAssetPath), fs::path(projectRoot), relEc);
            if (!relEc && !logicalCookedPath.empty()) {
                std::vector<uint8_t> cookedBytes;
                if (CatalystPak::ReadEntry(pakPath.wstring(), logicalCookedPath.generic_wstring(), cookedBytes, nullptr)) {
                    loadedCookedAsset = CookedUIBlueprint::LoadFromBinaryBuffer(cookedBytes, cookedAsset, nullptr);
                }
            }
        }
    }

    if (!loadedCookedAsset) {
        std::error_code cookedEc;
        if (fs::exists(cookedAssetPath, cookedEc) && !cookedEc) {
            loadedCookedAsset = CookedUIBlueprint::LoadFromBinaryFile(cookedAssetPath, cookedAsset, nullptr);
        }
    }

    if (!loadedCookedAsset) {
        loadedCookedAsset = CookedUIBlueprint::CompileFromJsonFile(assetPath, cookedAsset, nullptr);
    }

    if (!loadedCookedAsset) {
        return false;
    }

    outInstance.nodes.reserve(cookedAsset.elements.size());
    for (const CookedUIBlueprint::Element& element : cookedAsset.elements) {
        RuntimeWidgetNode node;
        node.id = element.id;
        node.elementType = element.type;
        node.displayText = element.displayText;
        node.canvasX = element.canvasX;
        node.canvasY = element.canvasY;
        node.canvasWidth = element.canvasWidth;
        node.canvasHeight = element.canvasHeight;
        node.tintColor = element.tintColor;
        node.visibleInGame = element.visibleInGame;
        outInstance.nodes.push_back(node);
    }

    outInstance.instructions.reserve(cookedAsset.instructions.size());
    for (const CookedUIBlueprint::Instruction& instruction : cookedAsset.instructions) {
        RuntimeWidgetInstruction runtimeInstruction;
        runtimeInstruction.opcode = instruction.opcode;
        runtimeInstruction.targetNodeId = instruction.targetNodeId;
        runtimeInstruction.color = instruction.color;
        runtimeInstruction.durationSeconds = instruction.durationSeconds;
        runtimeInstruction.text = instruction.text;
        outInstance.instructions.push_back(runtimeInstruction);
    }

    outInstance.events.reserve(cookedAsset.events.size());
    for (const CookedUIBlueprint::EventHandler& eventHandler : cookedAsset.events) {
        RuntimeWidgetEventHandler runtimeEventHandler;
        runtimeEventHandler.type = eventHandler.type;
        runtimeEventHandler.sourceNodeId = eventHandler.sourceNodeId;
        runtimeEventHandler.firstInstructionIndex = eventHandler.firstInstructionIndex;
        runtimeEventHandler.instructionCount = eventHandler.instructionCount;
        outInstance.events.push_back(runtimeEventHandler);
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

void DXRenderer::ExecuteRuntimeWidgetEvent(RuntimeWidgetInstance& instance, const RuntimeWidgetEventHandler& eventHandler) {
    const uint32_t firstInstructionIndex = (std::min)(eventHandler.firstInstructionIndex, static_cast<uint32_t>(instance.instructions.size()));
    const uint32_t lastInstructionIndex = (std::min)(firstInstructionIndex + eventHandler.instructionCount,
                                                     static_cast<uint32_t>(instance.instructions.size()));

    for (uint32_t instructionIndex = firstInstructionIndex; instructionIndex < lastInstructionIndex; ++instructionIndex) {
        const RuntimeWidgetInstruction& instruction = instance.instructions[instructionIndex];
        switch (instruction.opcode) {
        case CookedUIBlueprint::Opcode::SetTextColor: {
            RuntimeWidgetNode* targetNode = FindRuntimeWidgetNode(instance, instruction.targetNodeId);
            if (targetNode != nullptr && targetNode->elementType == CookedUIBlueprint::ElementType::TextBlock) {
                targetNode->tintColor = instruction.color;
            }
            break;
        }
        case CookedUIBlueprint::Opcode::Print:
            if (!instruction.text.empty()) {
                OutputDebugStringA((instruction.text + "\n").c_str());
            }
            break;
        case CookedUIBlueprint::Opcode::OpenLevel:
            if (!instruction.text.empty()) {
                OpenSceneMap(fs::path(instruction.text).wstring());
            }
            break;
        case CookedUIBlueprint::Opcode::SwapWidget:
            if (!instruction.text.empty() &&
                instance.ownerObjectIndex >= 0 &&
                instance.ownerObjectIndex < static_cast<int>(m_gameObjects.size())) {
                GameObject& ownerObject = m_gameObjects[static_cast<size_t>(instance.ownerObjectIndex)];
                if (instance.ownerWidgetIndex >= 0 &&
                    instance.ownerWidgetIndex < static_cast<int>(ownerObject.blueprintViewportWidgetAssetPaths.size())) {
                    ownerObject.blueprintViewportWidgetAssetPaths[static_cast<size_t>(instance.ownerWidgetIndex)] =
                        fs::path(instruction.text).wstring();
                }
            }
            break;
        case CookedUIBlueprint::Opcode::Delay:
        default:
            break;
        }
    }
}

void DXRenderer::DrawRuntimeBlueprintWidgets(float viewportLeft, float viewportTop, float viewportWidth, float viewportHeight) {
    struct ActiveWidgetRequest {
        std::string key;
        std::wstring assetPath;
        std::string ownerName;
        int ownerObjectIndex = -1;
        int ownerWidgetIndex = -1;
    };

    std::vector<ActiveWidgetRequest> activeWidgets;
    for (size_t objectIndex = 0; objectIndex < m_gameObjects.size(); ++objectIndex) {
        const GameObject& object = m_gameObjects[objectIndex];
        if (!object.enabled) {
            continue;
        }
        for (size_t widgetIndex = 0; widgetIndex < object.blueprintViewportWidgetAssetPaths.size(); ++widgetIndex) {
            const std::wstring& widgetAssetPath = object.blueprintViewportWidgetAssetPaths[widgetIndex];
            if (widgetAssetPath.empty()) {
                continue;
            }

            activeWidgets.push_back({
                std::to_string(objectIndex) + ":" + std::to_string(widgetIndex) + ":" + WideToUtf8(widgetAssetPath),
                widgetAssetPath,
                object.name.empty() ? "Blueprint Actor" : object.name,
                static_cast<int>(objectIndex),
                static_cast<int>(widgetIndex)
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
            instance->ownerObjectIndex = request.ownerObjectIndex;
            instance->ownerWidgetIndex = request.ownerWidgetIndex;
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

        if (!instance->constructExecuted) {
            for (const RuntimeWidgetEventHandler& eventHandler : instance->events) {
                if (eventHandler.type == CookedUIBlueprint::EventType::Construct) {
                    ExecuteRuntimeWidgetEvent(*instance, eventHandler);
                }
            }
            instance->constructExecuted = true;
        }

        bool hasVisibleNodes = false;
        float widgetContentWidth = 220.0f;
        float widgetContentHeight = 120.0f;
        for (const RuntimeWidgetNode& node : instance->nodes) {
            if (!node.visibleInGame) {
                continue;
            }
            hasVisibleNodes = true;
            widgetContentWidth = (std::max)(widgetContentWidth, node.canvasX + node.canvasWidth + 18.0f);
            widgetContentHeight = (std::max)(widgetContentHeight, node.canvasY + node.canvasHeight + 18.0f);
        }
        if (!hasVisibleNodes) {
            continue;
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
                if (!node.visibleInGame || node.elementType != CookedUIBlueprint::ElementType::Button) {
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
                    for (const RuntimeWidgetEventHandler& eventHandler : instance->events) {
                        if (eventHandler.type == CookedUIBlueprint::EventType::ButtonPressed &&
                            eventHandler.sourceNodeId == node.id) {
                            ExecuteRuntimeWidgetEvent(*instance, eventHandler);
                        }
                    }
                }
            }
        }

        for (int drawPass = 0; drawPass < 2; ++drawPass) {
            for (const RuntimeWidgetNode& node : instance->nodes) {
                if (!node.visibleInGame) {
                    continue;
                }
                const bool isCanvas = node.elementType == CookedUIBlueprint::ElementType::Canvas;
                if ((drawPass == 0) != isCanvas) {
                    continue;
                }

                float elementX = 0.0f;
                float elementY = 0.0f;
                float elementW = 0.0f;
                float elementH = 0.0f;
                GetElementRect(node, elementX, elementY, elementW, elementH);
                const uint32_t tintColor = node.tintColor;

                if (node.elementType == CookedUIBlueprint::ElementType::Canvas) {
                    m_uiDrawList.AddRectFilled(elementX, elementY, elementW, elementH, tintColor);
                    m_uiDrawList.AddText(m_fontManager, node.displayText, elementX + 10.0f, elementY + 20.0f, 0xFFFFFFFF, elementW - 20.0f);
                } else if (node.elementType == CookedUIBlueprint::ElementType::Button) {
                    const bool hovered = g_InputManager != nullptr &&
                        IsPointInRect(static_cast<float>(g_InputManager->GetMouseX()),
                                      static_cast<float>(g_InputManager->GetMouseY()),
                                      elementX, elementY, elementW, elementH);
                    m_uiDrawList.AddRectFilled(elementX, elementY, elementW, elementH,
                                               hovered ? BrightenPackedColor(tintColor, 0.08f) : tintColor);
                    m_uiDrawList.AddRectFilled(elementX, elementY, elementW, 3.0f, 0xFFFFFFFF);
                    m_uiDrawList.AddText(m_fontManager, node.displayText,
                                         elementX + 10.0f, elementY + elementH * 0.5f, 0xFFFFFFFF, elementW - 20.0f);
                } else if (node.elementType == CookedUIBlueprint::ElementType::TextBlock) {
                    m_uiDrawList.AddText(m_fontManager, node.displayText, elementX + 4.0f, elementY + 20.0f, tintColor, elementW - 8.0f);
                } else if (node.elementType == CookedUIBlueprint::ElementType::Image) {
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
    StopNativeScriptRuntime();
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

    SyncAllRootObjectLocals();
    RefreshSceneSavedDocument();
}

void DXRenderer::ClearProjectRuntimeAssets() {
    StopNativeScriptRuntime();
    m_scriptModuleHost.ClearProject();
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

    (void)EnsureProjectCodeWorkspaceReady(normalizedProjectFilePath);
    m_editorUI.State.currentProjectFile = normalizedProjectFilePath;
    const std::wstring projectRoot = fs::path(normalizedProjectFilePath).parent_path().wstring();
    m_editorUI.State.currentProjectFolder = projectRoot;
    m_scriptModuleHost.SetProject(normalizedProjectFilePath);
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
        sceneObject.tag = GetJsonString(serializedObject, "tag");
        sceneObject.animationState = GetJsonString(serializedObject, "animationState");
        sceneObject.layer = static_cast<uint32_t>(GetJsonInt(serializedObject, "layer", 0));
        sceneObject.enabled = GetJsonBool(serializedObject, "enabled", true);
        sceneObject.id = static_cast<uint64_t>(GetJsonInt(serializedObject, "id", static_cast<int>(sceneObject.id)));
        sceneObject.parentId = static_cast<uint64_t>(GetJsonInt(serializedObject, "parentId", 0));
        sceneObject.inheritParentTransform = GetJsonBool(serializedObject, "inheritParentTransform", sceneObject.parentId != 0);
        sceneObject.type = ObjectTypeFromString(GetJsonString(serializedObject, "type", "Mesh"));
        sceneObject.position = GetJsonFloat3(serializedObject, "position", {0.0f, 0.0f, 0.0f});
        sceneObject.rotation = GetJsonFloat3(serializedObject, "rotation", {0.0f, 0.0f, 0.0f});
        sceneObject.scale = GetJsonFloat3(serializedObject, "scale", {1.0f, 1.0f, 1.0f});
        sceneObject.localPosition = GetJsonFloat3(serializedObject, "localPosition", sceneObject.position);
        sceneObject.localRotation = GetJsonFloat3(serializedObject, "localRotation", sceneObject.rotation);
        sceneObject.localScale = GetJsonFloat3(serializedObject, "localScale", sceneObject.scale);
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
        const JsonValue* componentsValue = FindJsonField(serializedObject, "components");
        if (componentsValue != nullptr && componentsValue->type == JsonValueType::Array) {
            for (const JsonValue& componentValue : componentsValue->arrayValue) {
                if (componentValue.type != JsonValueType::Object) {
                    continue;
                }

                if (GetJsonString(componentValue, "type") != "NativeScript") {
                    continue;
                }

                NativeScriptComponentDesc component;
                component.id = static_cast<uint64_t>(GetJsonInt(componentValue, "id", static_cast<int>(component.id)));
                component.className = TrimAsciiWhitespace(GetJsonString(componentValue, "className"));
                component.enabled = GetJsonBool(componentValue, "enabled", true);
                sceneObject.nativeScriptComponents.push_back(component);
            }
        }

        const std::string legacyNativeBehavior = TrimAsciiWhitespace(GetJsonString(serializedObject, "nativeBehavior"));
        if (!legacyNativeBehavior.empty() && sceneObject.nativeScriptComponents.empty()) {
            NativeScriptComponentDesc legacyComponent;
            legacyComponent.className = legacyNativeBehavior;
            sceneObject.nativeScriptComponents.push_back(legacyComponent);
        }

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

    UpdateAttachedObjectTransforms();
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
    m_nativeScriptHostApi.apiVersion = Catalyst::kScriptApiVersion;
    m_nativeScriptHostApi.userData = this;
    m_nativeScriptHostApi.log = &DXRenderer::ScriptHostLog;
    m_nativeScriptHostApi.getKeyDown = &DXRenderer::ScriptHostGetKeyDown;
    m_nativeScriptHostApi.getKeyPressed = &DXRenderer::ScriptHostGetKeyPressed;
    m_nativeScriptHostApi.getActionDown = &DXRenderer::ScriptHostGetActionDown;
    m_nativeScriptHostApi.getActionPressed = &DXRenderer::ScriptHostGetActionPressed;
    m_nativeScriptHostApi.getAxis = &DXRenderer::ScriptHostGetAxis;
    m_nativeScriptHostApi.getDeltaTime = &DXRenderer::ScriptHostGetDeltaTime;
    m_nativeScriptHostApi.getUnscaledDeltaTime = &DXRenderer::ScriptHostGetUnscaledDeltaTime;
    m_nativeScriptHostApi.getTimeScale = &DXRenderer::ScriptHostGetTimeScale;
    m_nativeScriptHostApi.setTimeScale = &DXRenderer::ScriptHostSetTimeScale;
    m_nativeScriptHostApi.getObjectType = &DXRenderer::ScriptHostGetObjectType;
    m_nativeScriptHostApi.getObjectEnabled = &DXRenderer::ScriptHostGetObjectEnabled;
    m_nativeScriptHostApi.setObjectEnabled = &DXRenderer::ScriptHostSetObjectEnabled;
    m_nativeScriptHostApi.getObjectTransform = &DXRenderer::ScriptHostGetObjectTransform;
    m_nativeScriptHostApi.setObjectTransform = &DXRenderer::ScriptHostSetObjectTransform;
    m_nativeScriptHostApi.translateObject = &DXRenderer::ScriptHostTranslateObject;
    m_nativeScriptHostApi.copyObjectName = &DXRenderer::ScriptHostCopyObjectName;
    m_nativeScriptHostApi.copyObjectTag = &DXRenderer::ScriptHostCopyObjectTag;
    m_nativeScriptHostApi.setObjectTag = &DXRenderer::ScriptHostSetObjectTag;
    m_nativeScriptHostApi.getObjectLayer = &DXRenderer::ScriptHostGetObjectLayer;
    m_nativeScriptHostApi.setObjectLayer = &DXRenderer::ScriptHostSetObjectLayer;
    m_nativeScriptHostApi.findObjectByName = &DXRenderer::ScriptHostFindObjectByName;
    m_nativeScriptHostApi.findFirstObjectWithTag = &DXRenderer::ScriptHostFindFirstObjectWithTag;
    m_nativeScriptHostApi.getObjectsWithTag = &DXRenderer::ScriptHostGetObjectsWithTag;
    m_nativeScriptHostApi.attachObject = &DXRenderer::ScriptHostAttachObject;
    m_nativeScriptHostApi.detachObject = &DXRenderer::ScriptHostDetachObject;
    m_nativeScriptHostApi.duplicateObject = &DXRenderer::ScriptHostDuplicateObject;
    m_nativeScriptHostApi.destroyObject = &DXRenderer::ScriptHostDestroyObject;
    m_nativeScriptHostApi.instantiateObjectFromAssetPath = &DXRenderer::ScriptHostInstantiateObjectFromAssetPath;
    m_nativeScriptHostApi.getObjectCollider = &DXRenderer::ScriptHostGetObjectCollider;
    m_nativeScriptHostApi.setObjectCollider = &DXRenderer::ScriptHostSetObjectCollider;
    m_nativeScriptHostApi.getObjectRigidBody = &DXRenderer::ScriptHostGetObjectRigidBody;
    m_nativeScriptHostApi.setObjectRigidBody = &DXRenderer::ScriptHostSetObjectRigidBody;
    m_nativeScriptHostApi.addForce = &DXRenderer::ScriptHostAddForce;
    m_nativeScriptHostApi.addImpulse = &DXRenderer::ScriptHostAddImpulse;
    m_nativeScriptHostApi.raycast = &DXRenderer::ScriptHostRaycast;
    m_nativeScriptHostApi.copyObjectMaterialPath = &DXRenderer::ScriptHostCopyObjectMaterialPath;
    m_nativeScriptHostApi.setObjectMaterialPath = &DXRenderer::ScriptHostSetObjectMaterialPath;
    m_nativeScriptHostApi.getObjectColor = &DXRenderer::ScriptHostGetObjectColor;
    m_nativeScriptHostApi.setObjectColor = &DXRenderer::ScriptHostSetObjectColor;
    m_nativeScriptHostApi.getLightIntensity = &DXRenderer::ScriptHostGetLightIntensity;
    m_nativeScriptHostApi.setLightIntensity = &DXRenderer::ScriptHostSetLightIntensity;
    m_nativeScriptHostApi.getMainCameraFov = &DXRenderer::ScriptHostGetMainCameraFov;
    m_nativeScriptHostApi.setMainCameraFov = &DXRenderer::ScriptHostSetMainCameraFov;
    m_nativeScriptHostApi.findWidgetByText = &DXRenderer::ScriptHostFindWidgetByText;
    m_nativeScriptHostApi.setWidgetText = &DXRenderer::ScriptHostSetWidgetText;
    m_nativeScriptHostApi.setWidgetVisible = &DXRenderer::ScriptHostSetWidgetVisible;
    m_nativeScriptHostApi.setWidgetTint = &DXRenderer::ScriptHostSetWidgetTint;
    m_nativeScriptHostApi.setTimer = &DXRenderer::ScriptHostSetTimer;
    m_nativeScriptHostApi.cancelTimer = &DXRenderer::ScriptHostCancelTimer;
    m_nativeScriptHostApi.saveString = &DXRenderer::ScriptHostSaveString;
    m_nativeScriptHostApi.saveFloat = &DXRenderer::ScriptHostSaveFloat;
    m_nativeScriptHostApi.saveBool = &DXRenderer::ScriptHostSaveBool;
    m_nativeScriptHostApi.loadString = &DXRenderer::ScriptHostLoadString;
    m_nativeScriptHostApi.loadFloat = &DXRenderer::ScriptHostLoadFloat;
    m_nativeScriptHostApi.loadBool = &DXRenderer::ScriptHostLoadBool;
    m_nativeScriptHostApi.copyAnimationState = &DXRenderer::ScriptHostCopyAnimationState;
    m_nativeScriptHostApi.setAnimationState = &DXRenderer::ScriptHostSetAnimationState;
    m_nativeScriptHostApi.triggerAnimation = &DXRenderer::ScriptHostTriggerAnimation;
    m_nativeScriptHostApi.playOneShot2D = &DXRenderer::ScriptHostPlayOneShot2D;
    m_nativeScriptHostApi.playOneShot3D = &DXRenderer::ScriptHostPlayOneShot3D;
    m_nativeScriptHostApi.stopAudio = &DXRenderer::ScriptHostStopAudio;
    m_nativeScriptHostApi.setAudioVolume = &DXRenderer::ScriptHostSetAudioVolume;
    m_nativeScriptHostApi.setAudioPitch = &DXRenderer::ScriptHostSetAudioPitch;
    m_scriptModuleHost.SetHostApi(m_nativeScriptHostApi);

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

    m_camera.SetProjection(m_scriptCameraFov, static_cast<float>(width) / static_cast<float>(height), 0.1f, 5000.0f);
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
    PollScriptModuleHost();

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

    {
        AppLaunchRequest req;
        if (ConsumeAppLaunchRequest(req) && req.play) {
            m_hasPendingLaunchPlay = true;
            m_pendingLaunchProjectFile = NormalizeAssetPath(req.projectFilePath);
            m_pendingLaunchMapPath = NormalizeAssetPath(req.mapPath);
            m_engineState = EngineState::ProjectLoading;
            m_projectLoadingOverlayPresented = false;
            m_editorUI.State.standalonePlay = true;
            m_editorUI.State.isPlaying = false;
            QueueProjectStartupSceneLoad(m_pendingLaunchProjectFile);
        }
    }

    if (m_engineState == EngineState::Editor && m_editorUI.State.standalonePlay) {
        topH = 0.0f;
        rightW = 0.0f;
        bottomH = 0.0f;
        viewW = (std::max)(1.0f, w);
        viewH = (std::max)(1.0f, h);
    }

    
    if (m_engineState == EngineState::Launcher) {
        m_editorUI.DrawLauncher(this, w, h);
    } else if (m_engineState == EngineState::ProjectLoading) {
        m_editorUI.DrawProjectLoading(this, w, h);
    } else {
        if (!m_editorUI.State.standalonePlay) {
            m_editorUI.DrawEditor(this, w, h, topH, rightW, bottomH);
        }
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
            m_camera.SetProjection(m_scriptCameraFov, viewW / viewH, 0.1f, 5000.0f);
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
            StartNativeScriptRuntime();
            m_physicsSystem.Reset();
            m_jumpKeyWasDown = false;
            m_escapeKeyWasDown = false;
            m_playerMouseLookSuppressed = false;
            m_playerControllerPitch = 0.0f;
            m_playerControllerYaw = 0.0f;
            m_scriptTimeScale = 1.0f;
            m_scriptCameraFov = 45.0f;
            m_lastScaledScriptDeltaTime = 0.0f;
            m_lastUnscaledScriptDeltaTime = 0.0f;
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
            StopNativeScriptRuntime();
            m_physicsSystem.Reset();
            m_jumpKeyWasDown = false;
            m_escapeKeyWasDown = false;
            m_playerMouseLookSuppressed = false;
            m_scriptTimeScale = 1.0f;
            m_scriptCameraFov = 45.0f;
            m_lastScaledScriptDeltaTime = 0.0f;
            m_lastUnscaledScriptDeltaTime = 0.0f;
            SetPlayerMouseLookLocked(false);
        }

        m_lastPlayMode = m_editorUI.State.isPlaying;
        if (m_editorUI.State.isPlaying) {
            const float gameplayDeltaTime = deltaTime * m_scriptTimeScale;
            m_lastUnscaledScriptDeltaTime = deltaTime;
            m_lastScaledScriptDeltaTime = gameplayDeltaTime;
            if (m_scriptModuleHost.HasPendingReload()) {
                (void)HotReloadNativeScripts();
            }
            UpdateAttachedObjectTransforms();
            ApplyBlueprintGameplayNodes(gameplayDeltaTime, topH, viewW, viewH, sceneMouseInViewport);
            ApplyNativeScripts(gameplayDeltaTime);
            UpdateScriptTimers(gameplayDeltaTime);
            m_physicsSystem.Step(m_gameObjects, gameplayDeltaTime);
            UpdateAttachedObjectTransforms();
            FlushDestroyedGameObjects();
        }
    } else if (m_lastPlayMode) {
        if (!m_playModeSnapshot.empty()) {
            m_gameObjects = m_playModeSnapshot;
        }
        m_editorUI.State.isPlaying = false;
        m_lastPlayMode = false;
        m_playModeSnapshot.clear();
        m_runtimeWidgetInstances.clear();
        StopNativeScriptRuntime();
        m_physicsSystem.Reset();
        m_jumpKeyWasDown = false;
        m_escapeKeyWasDown = false;
        m_playerMouseLookSuppressed = false;
        m_scriptTimeScale = 1.0f;
        m_scriptCameraFov = 45.0f;
        m_lastScaledScriptDeltaTime = 0.0f;
        m_lastUnscaledScriptDeltaTime = 0.0f;
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

            if (m_hasPendingLaunchPlay) {
                if (!m_pendingLaunchMapPath.empty()) {
                    OpenSceneMap(m_pendingLaunchMapPath);
                }
                m_editorUI.State.isPlaying = true;
                m_hasPendingLaunchPlay = false;
                m_pendingLaunchProjectFile.clear();
                m_pendingLaunchMapPath.clear();
            }
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
void DXRenderer::RenderBlueprintPreview(const std::wstring& assetPath) {
    // Render a very simple preview – just the asset name in the lower
    // left corner of the viewport.  This is a placeholder that can be
    // replaced with a proper node graph renderer later.
    std::string name = WideToUtf8(assetPath);
    const float x = 10.0f;
    const float y = static_cast<float>(m_height) - 30.0f;
    m_uiContext.AddText(name, x, y, 0xFFFFFFFF, 0.0f);
}

void DXRenderer::Shutdown() { 
    FlushGPU(); 
    SetPlayerMouseLookLocked(false);
    StopNativeScriptRuntime();
    m_scriptModuleHost.Shutdown();
    m_uiRenderer.Shutdown();
    CloseActorAssetViewer();
    CloseMaterialAssetEditor();
    m_materialCache.clear();
    m_textureCache.clear();
    for (auto& pair : m_primitives) delete pair.second; 
    delete m_texWhite; delete m_texBlack; delete m_texNormal; 
}
