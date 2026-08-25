#include "BlueprintEditor_Internal.h"

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
