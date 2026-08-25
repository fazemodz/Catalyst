#include "BlueprintEditor_Internal.h"

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
        // Default text for new TextBlock nodes is now empty to allow users to set their own text.
        node.displayText = "";
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
