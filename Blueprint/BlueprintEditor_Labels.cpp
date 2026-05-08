#include "BlueprintEditor_Internal.h"

#include "../Scene/GameObject.h"

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

