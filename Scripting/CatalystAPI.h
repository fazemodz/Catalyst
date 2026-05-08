#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace Catalyst {
constexpr uint32_t kScriptApiVersion = 3;

using ObjectId = uint64_t;
using ComponentId = uint64_t;
using WidgetId = int32_t;
using AudioHandle = uint64_t;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct TransformData {
    Vec3 position;
    Vec3 rotation;
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

enum class ObjectType : uint32_t {
    Mesh = 0,
    Light = 1,
    Skybox = 2,
    PostProcessVolume = 3
};

enum class ColliderShape : uint32_t {
    Box = 0,
    Sphere = 1
};

enum class PhysicsBodyType : uint32_t {
    Static = 0,
    Dynamic = 1,
    Kinematic = 2
};

struct ColliderData {
    bool enabled = false;
    bool isTrigger = false;
    ColliderShape shape = ColliderShape::Box;
    Vec3 centerOffset;
    Vec3 boxExtents{0.5f, 0.5f, 0.5f};
    float sphereRadius = 0.5f;
};

struct RigidbodyData {
    bool enabled = false;
    PhysicsBodyType bodyType = PhysicsBodyType::Dynamic;
    bool useGravity = true;
    float mass = 1.0f;
    float linearDamping = 0.12f;
    float restitution = 0.2f;
    Vec3 velocity;
};

struct RaycastHit {
    bool hit = false;
    ObjectId objectId = 0;
    Vec3 point;
    Vec3 normal;
    float distance = 0.0f;
    bool isTrigger = false;
};

struct CollisionEvent {
    ObjectId otherObjectId = 0;
    Vec3 normal;
    float penetration = 0.0f;
    bool otherIsTrigger = false;
};

enum class Key : uint32_t {
    A = 0x41, B = 0x42, C = 0x43, D = 0x44, E = 0x45, F = 0x46, G = 0x47,
    H = 0x48, I = 0x49, J = 0x4A, K = 0x4B, L = 0x4C, M = 0x4D, N = 0x4E,
    O = 0x4F, P = 0x50, Q = 0x51, R = 0x52, S = 0x53, T = 0x54, U = 0x55,
    V = 0x56, W = 0x57, X = 0x58, Y = 0x59, Z = 0x5A,
    Space = 0x20, Escape = 0x1B, Enter = 0x0D, Shift = 0x10, Ctrl = 0x11,
    LeftMouse = 0x01, RightMouse = 0x02
};

struct ScriptStateWriter {
    void* context = nullptr;
    void (*writeFloat)(void* context, const char* key, float value) = nullptr;
    void (*writeBool)(void* context, const char* key, bool value) = nullptr;
    void (*writeString)(void* context, const char* key, const char* value) = nullptr;

    void WriteFloat(const char* key, float value) const {
        if (writeFloat != nullptr) {
            writeFloat(context, key, value);
        }
    }

    void WriteBool(const char* key, bool value) const {
        if (writeBool != nullptr) {
            writeBool(context, key, value);
        }
    }

    void WriteString(const char* key, const char* value) const {
        if (writeString != nullptr) {
            writeString(context, key, value != nullptr ? value : "");
        }
    }
};

struct ScriptStateReader {
    void* context = nullptr;
    bool (*readFloat)(void* context, const char* key, float* value) = nullptr;
    bool (*readBool)(void* context, const char* key, bool* value) = nullptr;
    bool (*readString)(void* context, const char* key, char* buffer, uint32_t bufferSize) = nullptr;

    bool TryReadFloat(const char* key, float& value) const {
        return readFloat != nullptr && readFloat(context, key, &value);
    }

    bool TryReadBool(const char* key, bool& value) const {
        return readBool != nullptr && readBool(context, key, &value);
    }

    bool TryReadString(const char* key, char* buffer, uint32_t bufferSize) const {
        return readString != nullptr && readString(context, key, buffer, bufferSize);
    }

    float ReadFloat(const char* key, float fallback) const {
        float value = fallback;
        (void)TryReadFloat(key, value);
        return value;
    }

    bool ReadBool(const char* key, bool fallback) const {
        bool value = fallback;
        (void)TryReadBool(key, value);
        return value;
    }

    std::string ReadString(const char* key, const char* fallback = "") const {
        char buffer[512] = {};
        if (TryReadString(key, buffer, static_cast<uint32_t>(sizeof(buffer)))) {
            return buffer;
        }
        return fallback != nullptr ? fallback : "";
    }
};

struct ScriptHostApi {
    uint32_t apiVersion = kScriptApiVersion;
    void* userData = nullptr;
    void (*log)(void* userData, const char* message) = nullptr;
    bool (*getKeyDown)(void* userData, uint32_t keyCode) = nullptr;
    bool (*getKeyPressed)(void* userData, uint32_t keyCode) = nullptr;
    bool (*getActionDown)(void* userData, const char* actionName) = nullptr;
    bool (*getActionPressed)(void* userData, const char* actionName) = nullptr;
    float (*getAxis)(void* userData, const char* axisName) = nullptr;
    float (*getDeltaTime)(void* userData) = nullptr;
    float (*getUnscaledDeltaTime)(void* userData) = nullptr;
    float (*getTimeScale)(void* userData) = nullptr;
    bool (*setTimeScale)(void* userData, float timeScale) = nullptr;
    bool (*getObjectType)(void* userData, ObjectId objectId, uint32_t* outObjectType) = nullptr;
    bool (*getObjectEnabled)(void* userData, ObjectId objectId, bool* outEnabled) = nullptr;
    bool (*setObjectEnabled)(void* userData, ObjectId objectId, bool enabled) = nullptr;
    bool (*getObjectTransform)(void* userData, ObjectId objectId, TransformData* outTransform) = nullptr;
    bool (*setObjectTransform)(void* userData, ObjectId objectId, const TransformData* transform) = nullptr;
    bool (*translateObject)(void* userData, ObjectId objectId, Vec3 delta) = nullptr;
    bool (*copyObjectName)(void* userData, ObjectId objectId, char* buffer, uint32_t bufferSize) = nullptr;
    bool (*copyObjectTag)(void* userData, ObjectId objectId, char* buffer, uint32_t bufferSize) = nullptr;
    bool (*setObjectTag)(void* userData, ObjectId objectId, const char* tag) = nullptr;
    bool (*getObjectLayer)(void* userData, ObjectId objectId, uint32_t* outLayer) = nullptr;
    bool (*setObjectLayer)(void* userData, ObjectId objectId, uint32_t layer) = nullptr;
    ObjectId (*findObjectByName)(void* userData, const char* name) = nullptr;
    ObjectId (*findFirstObjectWithTag)(void* userData, const char* tag) = nullptr;
    uint32_t (*getObjectsWithTag)(void* userData, const char* tag, ObjectId* outObjects, uint32_t capacity) = nullptr;
    bool (*attachObject)(void* userData, ObjectId childObjectId, ObjectId parentObjectId, bool keepWorldTransform) = nullptr;
    bool (*detachObject)(void* userData, ObjectId childObjectId, bool keepWorldTransform) = nullptr;
    ObjectId (*duplicateObject)(void* userData, ObjectId objectId) = nullptr;
    bool (*destroyObject)(void* userData, ObjectId objectId) = nullptr;
    ObjectId (*instantiateObjectFromAssetPath)(void* userData, const char* assetPath, const TransformData* transform) = nullptr;
    bool (*getObjectCollider)(void* userData, ObjectId objectId, ColliderData* outCollider) = nullptr;
    bool (*setObjectCollider)(void* userData, ObjectId objectId, const ColliderData* collider) = nullptr;
    bool (*getObjectRigidBody)(void* userData, ObjectId objectId, RigidbodyData* outRigidBody) = nullptr;
    bool (*setObjectRigidBody)(void* userData, ObjectId objectId, const RigidbodyData* rigidBody) = nullptr;
    bool (*addForce)(void* userData, ObjectId objectId, Vec3 force) = nullptr;
    bool (*addImpulse)(void* userData, ObjectId objectId, Vec3 impulse) = nullptr;
    bool (*raycast)(void* userData, const Vec3* origin, const Vec3* direction, float maxDistance, RaycastHit* outHit) = nullptr;
    bool (*copyObjectMaterialPath)(void* userData, ObjectId objectId, char* buffer, uint32_t bufferSize) = nullptr;
    bool (*setObjectMaterialPath)(void* userData, ObjectId objectId, const char* materialPath) = nullptr;
    bool (*getObjectColor)(void* userData, ObjectId objectId, Vec4* outColor) = nullptr;
    bool (*setObjectColor)(void* userData, ObjectId objectId, const Vec4* color) = nullptr;
    bool (*getLightIntensity)(void* userData, ObjectId objectId, float* outIntensity) = nullptr;
    bool (*setLightIntensity)(void* userData, ObjectId objectId, float intensity) = nullptr;
    bool (*getMainCameraFov)(void* userData, float* outFovDegrees) = nullptr;
    bool (*setMainCameraFov)(void* userData, float fovDegrees) = nullptr;
    WidgetId (*findWidgetByText)(void* userData, ObjectId ownerObjectId, const char* text) = nullptr;
    bool (*setWidgetText)(void* userData, ObjectId ownerObjectId, WidgetId widgetId, const char* text) = nullptr;
    bool (*setWidgetVisible)(void* userData, ObjectId ownerObjectId, WidgetId widgetId, bool visible) = nullptr;
    bool (*setWidgetTint)(void* userData, ObjectId ownerObjectId, WidgetId widgetId, const Vec4* color) = nullptr;
    bool (*setTimer)(void* userData, ObjectId objectId, ComponentId componentId, uint64_t timerId, float delaySeconds, bool looping) = nullptr;
    bool (*cancelTimer)(void* userData, ObjectId objectId, ComponentId componentId, uint64_t timerId) = nullptr;
    bool (*saveString)(void* userData, const char* slotName, const char* key, const char* value) = nullptr;
    bool (*saveFloat)(void* userData, const char* slotName, const char* key, float value) = nullptr;
    bool (*saveBool)(void* userData, const char* slotName, const char* key, bool value) = nullptr;
    bool (*loadString)(void* userData, const char* slotName, const char* key, char* buffer, uint32_t bufferSize) = nullptr;
    bool (*loadFloat)(void* userData, const char* slotName, const char* key, float* outValue) = nullptr;
    bool (*loadBool)(void* userData, const char* slotName, const char* key, bool* outValue) = nullptr;
    bool (*copyAnimationState)(void* userData, ObjectId objectId, char* buffer, uint32_t bufferSize) = nullptr;
    bool (*setAnimationState)(void* userData, ObjectId objectId, const char* stateName) = nullptr;
    bool (*triggerAnimation)(void* userData, ObjectId objectId, const char* triggerName) = nullptr;
    AudioHandle (*playOneShot2D)(void* userData, const char* assetPath, float volume) = nullptr;
    AudioHandle (*playOneShot3D)(void* userData, const char* assetPath, Vec3 position, float volume) = nullptr;
    bool (*stopAudio)(void* userData, AudioHandle handle) = nullptr;
    bool (*setAudioVolume)(void* userData, AudioHandle handle, float volume) = nullptr;
    bool (*setAudioPitch)(void* userData, AudioHandle handle, float pitch) = nullptr;
};

class NativeScript {
public:
    virtual ~NativeScript() = default;

    virtual const char* GetClassName() const = 0;
    virtual void OnStart() {}
    virtual void OnUpdate(float deltaTime) {}
    virtual void OnStop() {}
    virtual void OnHotReloadSave(ScriptStateWriter& writer) const {}
    virtual void OnHotReloadLoad(const ScriptStateReader& reader) {}
    virtual void OnCollisionEnter(const CollisionEvent& event) {}
    virtual void OnCollisionStay(const CollisionEvent& event) {}
    virtual void OnCollisionExit(const CollisionEvent& event) {}
    virtual void OnTriggerEnter(const CollisionEvent& event) {}
    virtual void OnTriggerStay(const CollisionEvent& event) {}
    virtual void OnTriggerExit(const CollisionEvent& event) {}
    virtual void OnTimer(uint64_t timerId) {}
    virtual void OnAnimationNotify(const char* notifyName) { (void)notifyName; }

    void __CatalystBind(const ScriptHostApi* hostApi, ObjectId objectId, ComponentId componentId) {
        m_hostApi = hostApi;
        m_objectId = objectId;
        m_componentId = componentId;
    }

protected:
    void PrintToConsole(const char* message) const {
        if (m_hostApi != nullptr && m_hostApi->log != nullptr) {
            m_hostApi->log(m_hostApi->userData, message != nullptr ? message : "");
        }
    }

    bool GetKeyDown(Key key) const {
        return m_hostApi != nullptr &&
               m_hostApi->getKeyDown != nullptr &&
               m_hostApi->getKeyDown(m_hostApi->userData, static_cast<uint32_t>(key));
    }

    bool GetKeyPressed(Key key) const {
        return m_hostApi != nullptr &&
               m_hostApi->getKeyPressed != nullptr &&
               m_hostApi->getKeyPressed(m_hostApi->userData, static_cast<uint32_t>(key));
    }

    bool IsActionPressed(const char* actionName) const {
        return m_hostApi != nullptr &&
               m_hostApi->getActionDown != nullptr &&
               m_hostApi->getActionDown(m_hostApi->userData, actionName != nullptr ? actionName : "");
    }

    bool WasActionJustPressed(const char* actionName) const {
        return m_hostApi != nullptr &&
               m_hostApi->getActionPressed != nullptr &&
               m_hostApi->getActionPressed(m_hostApi->userData, actionName != nullptr ? actionName : "");
    }

    float GetAxis(const char* axisName) const {
        return m_hostApi != nullptr && m_hostApi->getAxis != nullptr
            ? m_hostApi->getAxis(m_hostApi->userData, axisName != nullptr ? axisName : "")
            : 0.0f;
    }

    float GetScaledDeltaTime() const {
        return m_hostApi != nullptr && m_hostApi->getDeltaTime != nullptr
            ? m_hostApi->getDeltaTime(m_hostApi->userData)
            : 0.0f;
    }

    float GetUnscaledDeltaTime() const {
        return m_hostApi != nullptr && m_hostApi->getUnscaledDeltaTime != nullptr
            ? m_hostApi->getUnscaledDeltaTime(m_hostApi->userData)
            : 0.0f;
    }

    float GetTimeScale() const {
        return m_hostApi != nullptr && m_hostApi->getTimeScale != nullptr
            ? m_hostApi->getTimeScale(m_hostApi->userData)
            : 1.0f;
    }

    bool SetTimeScale(float timeScale) const {
        return m_hostApi != nullptr &&
               m_hostApi->setTimeScale != nullptr &&
               m_hostApi->setTimeScale(m_hostApi->userData, timeScale);
    }

    TransformData GetTransform() const {
        TransformData transform;
        if (m_hostApi != nullptr && m_hostApi->getObjectTransform != nullptr) {
            (void)m_hostApi->getObjectTransform(m_hostApi->userData, m_objectId, &transform);
        }
        return transform;
    }

    bool TryGetTransform(ObjectId objectId, TransformData& outTransform) const {
        return m_hostApi != nullptr &&
               m_hostApi->getObjectTransform != nullptr &&
               m_hostApi->getObjectTransform(m_hostApi->userData, objectId, &outTransform);
    }

    void SetTransform(const TransformData& transform) const {
        if (m_hostApi != nullptr && m_hostApi->setObjectTransform != nullptr) {
            (void)m_hostApi->setObjectTransform(m_hostApi->userData, m_objectId, &transform);
        }
    }

    bool TrySetTransform(ObjectId objectId, const TransformData& transform) const {
        return m_hostApi != nullptr &&
               m_hostApi->setObjectTransform != nullptr &&
               m_hostApi->setObjectTransform(m_hostApi->userData, objectId, &transform);
    }

    void Translate(float x, float y, float z) const {
        if (m_hostApi != nullptr && m_hostApi->translateObject != nullptr) {
            (void)m_hostApi->translateObject(m_hostApi->userData, m_objectId, Vec3{x, y, z});
        }
    }

    bool TryTranslate(ObjectId objectId, float x, float y, float z) const {
        return m_hostApi != nullptr &&
               m_hostApi->translateObject != nullptr &&
               m_hostApi->translateObject(m_hostApi->userData, objectId, Vec3{x, y, z});
    }

    ObjectType GetObjectType() const {
        uint32_t value = static_cast<uint32_t>(ObjectType::Mesh);
        if (m_hostApi != nullptr && m_hostApi->getObjectType != nullptr) {
            (void)m_hostApi->getObjectType(m_hostApi->userData, m_objectId, &value);
        }
        return static_cast<ObjectType>(value);
    }

    bool IsObjectEnabled(ObjectId objectId = 0) const {
        bool enabled = false;
        const ObjectId targetObjectId = objectId != 0 ? objectId : m_objectId;
        if (m_hostApi != nullptr && m_hostApi->getObjectEnabled != nullptr) {
            (void)m_hostApi->getObjectEnabled(m_hostApi->userData, targetObjectId, &enabled);
        }
        return enabled;
    }

    bool SetObjectEnabled(bool enabled, ObjectId objectId = 0) const {
        const ObjectId targetObjectId = objectId != 0 ? objectId : m_objectId;
        return m_hostApi != nullptr &&
               m_hostApi->setObjectEnabled != nullptr &&
               m_hostApi->setObjectEnabled(m_hostApi->userData, targetObjectId, enabled);
    }

    ColliderData GetCollider() const {
        ColliderData collider;
        if (m_hostApi != nullptr && m_hostApi->getObjectCollider != nullptr) {
            (void)m_hostApi->getObjectCollider(m_hostApi->userData, m_objectId, &collider);
        }
        return collider;
    }

    bool TryGetCollider(ObjectId objectId, ColliderData& outCollider) const {
        return m_hostApi != nullptr &&
               m_hostApi->getObjectCollider != nullptr &&
               m_hostApi->getObjectCollider(m_hostApi->userData, objectId, &outCollider);
    }

    void SetCollider(const ColliderData& collider) const {
        if (m_hostApi != nullptr && m_hostApi->setObjectCollider != nullptr) {
            (void)m_hostApi->setObjectCollider(m_hostApi->userData, m_objectId, &collider);
        }
    }

    bool TrySetCollider(ObjectId objectId, const ColliderData& collider) const {
        return m_hostApi != nullptr &&
               m_hostApi->setObjectCollider != nullptr &&
               m_hostApi->setObjectCollider(m_hostApi->userData, objectId, &collider);
    }

    RigidbodyData GetRigidBody() const {
        RigidbodyData rigidBody;
        if (m_hostApi != nullptr && m_hostApi->getObjectRigidBody != nullptr) {
            (void)m_hostApi->getObjectRigidBody(m_hostApi->userData, m_objectId, &rigidBody);
        }
        return rigidBody;
    }

    bool TryGetRigidBody(ObjectId objectId, RigidbodyData& outRigidBody) const {
        return m_hostApi != nullptr &&
               m_hostApi->getObjectRigidBody != nullptr &&
               m_hostApi->getObjectRigidBody(m_hostApi->userData, objectId, &outRigidBody);
    }

    void SetRigidBody(const RigidbodyData& rigidBody) const {
        if (m_hostApi != nullptr && m_hostApi->setObjectRigidBody != nullptr) {
            (void)m_hostApi->setObjectRigidBody(m_hostApi->userData, m_objectId, &rigidBody);
        }
    }

    bool TrySetRigidBody(ObjectId objectId, const RigidbodyData& rigidBody) const {
        return m_hostApi != nullptr &&
               m_hostApi->setObjectRigidBody != nullptr &&
               m_hostApi->setObjectRigidBody(m_hostApi->userData, objectId, &rigidBody);
    }

    bool AddForce(const Vec3& force, ObjectId objectId = 0) const {
        const ObjectId targetObjectId = objectId != 0 ? objectId : m_objectId;
        return m_hostApi != nullptr &&
               m_hostApi->addForce != nullptr &&
               m_hostApi->addForce(m_hostApi->userData, targetObjectId, force);
    }

    bool AddImpulse(const Vec3& impulse, ObjectId objectId = 0) const {
        const ObjectId targetObjectId = objectId != 0 ? objectId : m_objectId;
        return m_hostApi != nullptr &&
               m_hostApi->addImpulse != nullptr &&
               m_hostApi->addImpulse(m_hostApi->userData, targetObjectId, impulse);
    }

    bool Raycast(const Vec3& origin, const Vec3& direction, float maxDistance, RaycastHit& outHit) const {
        return m_hostApi != nullptr &&
               m_hostApi->raycast != nullptr &&
               m_hostApi->raycast(m_hostApi->userData, &origin, &direction, maxDistance, &outHit);
    }

    ObjectId FindObjectByName(const char* name) const {
        return m_hostApi != nullptr && m_hostApi->findObjectByName != nullptr
            ? m_hostApi->findObjectByName(m_hostApi->userData, name != nullptr ? name : "")
            : 0;
    }

    ObjectId FindFirstObjectWithTag(const char* tag) const {
        return m_hostApi != nullptr && m_hostApi->findFirstObjectWithTag != nullptr
            ? m_hostApi->findFirstObjectWithTag(m_hostApi->userData, tag != nullptr ? tag : "")
            : 0;
    }

    std::vector<ObjectId> GetObjectsWithTag(const char* tag) const {
        std::vector<ObjectId> objects;
        if (m_hostApi == nullptr || m_hostApi->getObjectsWithTag == nullptr) {
            return objects;
        }

        ObjectId temp[256] = {};
        const uint32_t count = m_hostApi->getObjectsWithTag(m_hostApi->userData, tag != nullptr ? tag : "",
                                                            temp, static_cast<uint32_t>(sizeof(temp) / sizeof(temp[0])));
        objects.assign(temp, temp + count);
        return objects;
    }

    std::string GetTag(ObjectId objectId = 0) const {
        char buffer[256] = {};
        const ObjectId targetObjectId = objectId != 0 ? objectId : m_objectId;
        if (m_hostApi != nullptr &&
            m_hostApi->copyObjectTag != nullptr &&
            m_hostApi->copyObjectTag(m_hostApi->userData, targetObjectId, buffer, static_cast<uint32_t>(sizeof(buffer)))) {
            return buffer;
        }
        return {};
    }

    bool SetTag(const char* tag, ObjectId objectId = 0) const {
        const ObjectId targetObjectId = objectId != 0 ? objectId : m_objectId;
        return m_hostApi != nullptr &&
               m_hostApi->setObjectTag != nullptr &&
               m_hostApi->setObjectTag(m_hostApi->userData, targetObjectId, tag != nullptr ? tag : "");
    }

    uint32_t GetLayer(ObjectId objectId = 0) const {
        uint32_t layer = 0;
        const ObjectId targetObjectId = objectId != 0 ? objectId : m_objectId;
        if (m_hostApi != nullptr && m_hostApi->getObjectLayer != nullptr) {
            (void)m_hostApi->getObjectLayer(m_hostApi->userData, targetObjectId, &layer);
        }
        return layer;
    }

    bool SetLayer(uint32_t layer, ObjectId objectId = 0) const {
        const ObjectId targetObjectId = objectId != 0 ? objectId : m_objectId;
        return m_hostApi != nullptr &&
               m_hostApi->setObjectLayer != nullptr &&
               m_hostApi->setObjectLayer(m_hostApi->userData, targetObjectId, layer);
    }

    bool AttachToParent(ObjectId parentObjectId, bool keepWorldTransform = true) const {
        return m_hostApi != nullptr &&
               m_hostApi->attachObject != nullptr &&
               m_hostApi->attachObject(m_hostApi->userData, m_objectId, parentObjectId, keepWorldTransform);
    }

    bool DetachFromParent(bool keepWorldTransform = true) const {
        return m_hostApi != nullptr &&
               m_hostApi->detachObject != nullptr &&
               m_hostApi->detachObject(m_hostApi->userData, m_objectId, keepWorldTransform);
    }

    ObjectId DuplicateObject(ObjectId objectId = 0) const {
        const ObjectId targetObjectId = objectId != 0 ? objectId : m_objectId;
        return m_hostApi != nullptr && m_hostApi->duplicateObject != nullptr
            ? m_hostApi->duplicateObject(m_hostApi->userData, targetObjectId)
            : 0;
    }

    bool DestroyObject(ObjectId objectId = 0) const {
        const ObjectId targetObjectId = objectId != 0 ? objectId : m_objectId;
        return m_hostApi != nullptr &&
               m_hostApi->destroyObject != nullptr &&
               m_hostApi->destroyObject(m_hostApi->userData, targetObjectId);
    }

    ObjectId InstantiateFromAssetPath(const char* assetPath, const TransformData& transform) const {
        return m_hostApi != nullptr && m_hostApi->instantiateObjectFromAssetPath != nullptr
            ? m_hostApi->instantiateObjectFromAssetPath(m_hostApi->userData, assetPath != nullptr ? assetPath : "", &transform)
            : 0;
    }

    std::string GetMaterialPath(ObjectId objectId = 0) const {
        char buffer[512] = {};
        const ObjectId targetObjectId = objectId != 0 ? objectId : m_objectId;
        if (m_hostApi != nullptr &&
            m_hostApi->copyObjectMaterialPath != nullptr &&
            m_hostApi->copyObjectMaterialPath(m_hostApi->userData, targetObjectId, buffer, static_cast<uint32_t>(sizeof(buffer)))) {
            return buffer;
        }
        return {};
    }

    bool SetMaterialPath(const char* materialPath, ObjectId objectId = 0) const {
        const ObjectId targetObjectId = objectId != 0 ? objectId : m_objectId;
        return m_hostApi != nullptr &&
               m_hostApi->setObjectMaterialPath != nullptr &&
               m_hostApi->setObjectMaterialPath(m_hostApi->userData, targetObjectId, materialPath != nullptr ? materialPath : "");
    }

    Vec4 GetColor(ObjectId objectId = 0) const {
        Vec4 color;
        const ObjectId targetObjectId = objectId != 0 ? objectId : m_objectId;
        if (m_hostApi != nullptr && m_hostApi->getObjectColor != nullptr) {
            (void)m_hostApi->getObjectColor(m_hostApi->userData, targetObjectId, &color);
        }
        return color;
    }

    bool SetColor(const Vec4& color, ObjectId objectId = 0) const {
        const ObjectId targetObjectId = objectId != 0 ? objectId : m_objectId;
        return m_hostApi != nullptr &&
               m_hostApi->setObjectColor != nullptr &&
               m_hostApi->setObjectColor(m_hostApi->userData, targetObjectId, &color);
    }

    float GetLightIntensity(ObjectId objectId = 0) const {
        float intensity = 0.0f;
        const ObjectId targetObjectId = objectId != 0 ? objectId : m_objectId;
        if (m_hostApi != nullptr && m_hostApi->getLightIntensity != nullptr) {
            (void)m_hostApi->getLightIntensity(m_hostApi->userData, targetObjectId, &intensity);
        }
        return intensity;
    }

    bool SetLightIntensity(float intensity, ObjectId objectId = 0) const {
        const ObjectId targetObjectId = objectId != 0 ? objectId : m_objectId;
        return m_hostApi != nullptr &&
               m_hostApi->setLightIntensity != nullptr &&
               m_hostApi->setLightIntensity(m_hostApi->userData, targetObjectId, intensity);
    }

    float GetMainCameraFov() const {
        float fov = 45.0f;
        if (m_hostApi != nullptr && m_hostApi->getMainCameraFov != nullptr) {
            (void)m_hostApi->getMainCameraFov(m_hostApi->userData, &fov);
        }
        return fov;
    }

    bool SetMainCameraFov(float fovDegrees) const {
        return m_hostApi != nullptr &&
               m_hostApi->setMainCameraFov != nullptr &&
               m_hostApi->setMainCameraFov(m_hostApi->userData, fovDegrees);
    }

    WidgetId FindWidgetByText(const char* text, ObjectId ownerObjectId = 0) const {
        const ObjectId targetObjectId = ownerObjectId != 0 ? ownerObjectId : m_objectId;
        return m_hostApi != nullptr && m_hostApi->findWidgetByText != nullptr
            ? m_hostApi->findWidgetByText(m_hostApi->userData, targetObjectId, text != nullptr ? text : "")
            : -1;
    }

    bool SetWidgetText(WidgetId widgetId, const char* text, ObjectId ownerObjectId = 0) const {
        const ObjectId targetObjectId = ownerObjectId != 0 ? ownerObjectId : m_objectId;
        return m_hostApi != nullptr &&
               m_hostApi->setWidgetText != nullptr &&
               m_hostApi->setWidgetText(m_hostApi->userData, targetObjectId, widgetId, text != nullptr ? text : "");
    }

    bool SetWidgetVisible(WidgetId widgetId, bool visible, ObjectId ownerObjectId = 0) const {
        const ObjectId targetObjectId = ownerObjectId != 0 ? ownerObjectId : m_objectId;
        return m_hostApi != nullptr &&
               m_hostApi->setWidgetVisible != nullptr &&
               m_hostApi->setWidgetVisible(m_hostApi->userData, targetObjectId, widgetId, visible);
    }

    bool SetWidgetTint(WidgetId widgetId, const Vec4& color, ObjectId ownerObjectId = 0) const {
        const ObjectId targetObjectId = ownerObjectId != 0 ? ownerObjectId : m_objectId;
        return m_hostApi != nullptr &&
               m_hostApi->setWidgetTint != nullptr &&
               m_hostApi->setWidgetTint(m_hostApi->userData, targetObjectId, widgetId, &color);
    }

    bool SetTimer(uint64_t timerId, float delaySeconds, bool looping = false) const {
        return m_hostApi != nullptr &&
               m_hostApi->setTimer != nullptr &&
               m_hostApi->setTimer(m_hostApi->userData, m_objectId, m_componentId, timerId, delaySeconds, looping);
    }

    bool CancelTimer(uint64_t timerId) const {
        return m_hostApi != nullptr &&
               m_hostApi->cancelTimer != nullptr &&
               m_hostApi->cancelTimer(m_hostApi->userData, m_objectId, m_componentId, timerId);
    }

    bool SaveString(const char* slotName, const char* key, const char* value) const {
        return m_hostApi != nullptr &&
               m_hostApi->saveString != nullptr &&
               m_hostApi->saveString(m_hostApi->userData, slotName != nullptr ? slotName : "",
                                     key != nullptr ? key : "", value != nullptr ? value : "");
    }

    bool SaveFloat(const char* slotName, const char* key, float value) const {
        return m_hostApi != nullptr &&
               m_hostApi->saveFloat != nullptr &&
               m_hostApi->saveFloat(m_hostApi->userData, slotName != nullptr ? slotName : "",
                                    key != nullptr ? key : "", value);
    }

    bool SaveBool(const char* slotName, const char* key, bool value) const {
        return m_hostApi != nullptr &&
               m_hostApi->saveBool != nullptr &&
               m_hostApi->saveBool(m_hostApi->userData, slotName != nullptr ? slotName : "",
                                   key != nullptr ? key : "", value);
    }

    std::string LoadString(const char* slotName, const char* key, const char* fallback = "") const {
        char buffer[512] = {};
        if (m_hostApi != nullptr &&
            m_hostApi->loadString != nullptr &&
            m_hostApi->loadString(m_hostApi->userData, slotName != nullptr ? slotName : "",
                                  key != nullptr ? key : "", buffer, static_cast<uint32_t>(sizeof(buffer)))) {
            return buffer;
        }
        return fallback != nullptr ? fallback : "";
    }

    float LoadFloat(const char* slotName, const char* key, float fallback) const {
        float value = fallback;
        if (m_hostApi != nullptr && m_hostApi->loadFloat != nullptr) {
            (void)m_hostApi->loadFloat(m_hostApi->userData, slotName != nullptr ? slotName : "",
                                       key != nullptr ? key : "", &value);
        }
        return value;
    }

    bool LoadBool(const char* slotName, const char* key, bool fallback) const {
        bool value = fallback;
        if (m_hostApi != nullptr && m_hostApi->loadBool != nullptr) {
            (void)m_hostApi->loadBool(m_hostApi->userData, slotName != nullptr ? slotName : "",
                                      key != nullptr ? key : "", &value);
        }
        return value;
    }

    std::string GetAnimationState(ObjectId objectId = 0) const {
        char buffer[256] = {};
        const ObjectId targetObjectId = objectId != 0 ? objectId : m_objectId;
        if (m_hostApi != nullptr &&
            m_hostApi->copyAnimationState != nullptr &&
            m_hostApi->copyAnimationState(m_hostApi->userData, targetObjectId, buffer, static_cast<uint32_t>(sizeof(buffer)))) {
            return buffer;
        }
        return {};
    }

    bool SetAnimationState(const char* stateName, ObjectId objectId = 0) const {
        const ObjectId targetObjectId = objectId != 0 ? objectId : m_objectId;
        return m_hostApi != nullptr &&
               m_hostApi->setAnimationState != nullptr &&
               m_hostApi->setAnimationState(m_hostApi->userData, targetObjectId, stateName != nullptr ? stateName : "");
    }

    bool TriggerAnimation(const char* triggerName, ObjectId objectId = 0) const {
        const ObjectId targetObjectId = objectId != 0 ? objectId : m_objectId;
        return m_hostApi != nullptr &&
               m_hostApi->triggerAnimation != nullptr &&
               m_hostApi->triggerAnimation(m_hostApi->userData, targetObjectId, triggerName != nullptr ? triggerName : "");
    }

    AudioHandle PlayOneShot2D(const char* assetPath, float volume = 1.0f) const {
        return m_hostApi != nullptr && m_hostApi->playOneShot2D != nullptr
            ? m_hostApi->playOneShot2D(m_hostApi->userData, assetPath != nullptr ? assetPath : "", volume)
            : 0;
    }

    AudioHandle PlayOneShot3D(const char* assetPath, const Vec3& position, float volume = 1.0f) const {
        return m_hostApi != nullptr && m_hostApi->playOneShot3D != nullptr
            ? m_hostApi->playOneShot3D(m_hostApi->userData, assetPath != nullptr ? assetPath : "", position, volume)
            : 0;
    }

    bool StopAudio(AudioHandle handle) const {
        return m_hostApi != nullptr &&
               m_hostApi->stopAudio != nullptr &&
               m_hostApi->stopAudio(m_hostApi->userData, handle);
    }

    bool SetAudioVolume(AudioHandle handle, float volume) const {
        return m_hostApi != nullptr &&
               m_hostApi->setAudioVolume != nullptr &&
               m_hostApi->setAudioVolume(m_hostApi->userData, handle, volume);
    }

    bool SetAudioPitch(AudioHandle handle, float pitch) const {
        return m_hostApi != nullptr &&
               m_hostApi->setAudioPitch != nullptr &&
               m_hostApi->setAudioPitch(m_hostApi->userData, handle, pitch);
    }

    ObjectId GetObjectId() const {
        return m_objectId;
    }

    ComponentId GetComponentId() const {
        return m_componentId;
    }

    std::string GetObjectName() const {
        char buffer[256] = {};
        if (m_hostApi != nullptr &&
            m_hostApi->copyObjectName != nullptr &&
            m_hostApi->copyObjectName(m_hostApi->userData, m_objectId, buffer, static_cast<uint32_t>(sizeof(buffer)))) {
            return buffer;
        }
        return {};
    }

private:
    const ScriptHostApi* m_hostApi = nullptr;
    ObjectId m_objectId = 0;
    ComponentId m_componentId = 0;
};

struct ScriptClassDesc {
    const char* className = nullptr;
};

struct ScriptModuleApi {
    uint32_t apiVersion = kScriptApiVersion;
    void (*installHostApi)(const ScriptHostApi* hostApi) = nullptr;
    uint32_t (*getScriptClassCount)() = nullptr;
    const ScriptClassDesc* (*getScriptClassDescs)() = nullptr;
    NativeScript* (*createScript)(const char* className) = nullptr;
    void (*destroyScript)(NativeScript* script) = nullptr;
};

namespace detail {
struct RegisteredScript {
    const char* className = nullptr;
    NativeScript* (*createInstance)() = nullptr;
};

inline std::vector<RegisteredScript>& GetRegistry() {
    static std::vector<RegisteredScript> registry;
    return registry;
}

inline std::vector<ScriptClassDesc>& GetClassDescs() {
    static std::vector<ScriptClassDesc> descs;
    return descs;
}

inline const ScriptHostApi*& GetInstalledHostApi() {
    static const ScriptHostApi* api = nullptr;
    return api;
}

inline void RegisterScriptClass(const char* className, NativeScript* (*createInstance)()) {
    if (className == nullptr || createInstance == nullptr) {
        return;
    }

    auto& registry = GetRegistry();
    for (const RegisteredScript& script : registry) {
        if (std::strcmp(script.className, className) == 0) {
            return;
        }
    }

    registry.push_back({className, createInstance});
    GetClassDescs().push_back({className});
}

inline void InstallHostApi(const ScriptHostApi* hostApi) {
    GetInstalledHostApi() = hostApi;
}

inline uint32_t GetScriptClassCount() {
    return static_cast<uint32_t>(GetClassDescs().size());
}

inline const ScriptClassDesc* GetScriptClassDescs() {
    const auto& descs = GetClassDescs();
    return descs.empty() ? nullptr : descs.data();
}

inline NativeScript* CreateScript(const char* className) {
    if (className == nullptr) {
        return nullptr;
    }

    for (const RegisteredScript& script : GetRegistry()) {
        if (std::strcmp(script.className, className) == 0) {
            NativeScript* instance = script.createInstance();
            if (instance != nullptr) {
                instance->__CatalystBind(GetInstalledHostApi(), 0, 0);
            }
            return instance;
        }
    }

    return nullptr;
}

inline void DestroyScript(NativeScript* script) {
    delete script;
}

inline const ScriptModuleApi& GetModuleApi() {
    static const ScriptModuleApi api = {
        kScriptApiVersion,
        &InstallHostApi,
        &GetScriptClassCount,
        &GetScriptClassDescs,
        &CreateScript,
        &DestroyScript
    };
    return api;
}
}
}

#define REGISTER_SCRIPT(Type) \
    namespace { \
    ::Catalyst::NativeScript* Create##Type##ScriptInstance() { return new Type(); } \
    struct Type##CatalystScriptRegistrar { \
        Type##CatalystScriptRegistrar() { ::Catalyst::detail::RegisterScriptClass(#Type, &Create##Type##ScriptInstance); } \
    } g_##Type##CatalystScriptRegistrar; \
    }

#define CATALYST_EXPORT_SCRIPT_MODULE() \
    extern "C" __declspec(dllexport) const ::Catalyst::ScriptModuleApi* GetCatalystScriptModuleApi() { \
        return &::Catalyst::detail::GetModuleApi(); \
    }
