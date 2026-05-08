#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <windows.h>
#include <DirectXMath.h>

#include "../Physics/PhysicsTypes.h"

struct GameObject;
class UIDrawList;
class UIContext;
class FontManager;
class InputManager;

class BlueprintEditor {
public:
    enum class AssetKind {
        Actor,
        UI
    };

    enum class DocumentTab {
        Viewport,
        ConstructionScript,
        EventGraph
    };

    enum class NodeVisualKind {
        Event,
        Function,
        Field,
        Component,
        UIElement,
        Comment
    };

    enum class PinKind {
        Exec,
        Data
    };

    enum class PinDirection {
        Input,
        Output
    };

    enum class ComponentKind {
        StaticMesh,
        SkeletalMesh,
        Camera,
        Trigger
    };

    enum class UIElementKind {
        None,
        Canvas,
        Button,
        Image,
        TextBlock
    };

    void Open(const GameObject* targetObject);
    void OpenAsset(const std::wstring& assetPath);
    void Close();
    bool IsOpen() const { return m_isOpen; }
    bool HasOpenAsset() const { return !m_assetPath.empty(); }
    bool IsUIAsset() const { return m_assetKind == AssetKind::UI; }
    bool HasUnsavedChanges() const;
    bool SaveOpenAsset();
    std::wstring GetDisplayName() const;
    const std::wstring& GetAssetPath() const { return m_assetPath; }

    void Draw(UIDrawList& drawList, UIContext& uiCtx, FontManager& fontManager, InputManager& inputManager,
              float width, float height, const GameObject* selectedObject,
              bool isStandaloneWindow = false, HWND windowHandle = nullptr, bool interactionsBlocked = false);

private:
    struct BlueprintComponent {
        int id = 0;
        std::string name;
        ComponentKind kind = ComponentKind::StaticMesh;
        std::wstring assetPath;
        DirectX::XMFLOAT3 location = {0.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT3 rotation = {0.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT3 scale = {1.0f, 1.0f, 1.0f};
        bool possessOnPlay = false;
        PhysicsColliderShape triggerShape = PhysicsColliderShape::Box;
        DirectX::XMFLOAT3 triggerExtents = {0.75f, 0.75f, 0.75f};
        float triggerRadius = 0.75f;
        bool exposeToGraph = true;
    };

    struct Node {
        int id = 0;
        std::string title;
        std::string subtitle;
        float x = 0.0f;
        float y = 0.0f;
        float width = 210.0f;
        float height = 92.0f;
        NodeVisualKind visual = NodeVisualKind::Function;
        std::string nodeTypeId;
        std::string fieldName;
        std::string fieldCategory;
        const BlueprintFieldDescriptor* field = nullptr;
        int componentId = -1;
        std::string componentName;
        ComponentKind componentKind = ComponentKind::StaticMesh;
        UIElementKind uiElementKind = UIElementKind::None;
        float canvasX = 32.0f;
        float canvasY = 32.0f;
        float canvasWidth = 180.0f;
        float canvasHeight = 56.0f;
        std::string displayText;
        DirectX::XMFLOAT4 tint = {0.2f, 0.24f, 0.30f, 1.0f};
        std::wstring assetPath;
        bool visibleInGame = true;
        bool canDelete = true;
    };

    struct AssetBrowserItem {
        std::wstring path;
        std::string label;
        std::string subtitle;
    };

    struct Link {
        int fromNodeId = 0;
        int toNodeId = 0;
        PinKind fromPinKind = PinKind::Exec;
        PinKind toPinKind = PinKind::Exec;
        uint32_t color = 0xFF4B4B4B;
    };

    struct PinReference {
        int nodeId = -1;
        PinKind kind = PinKind::Exec;
        PinDirection direction = PinDirection::Input;
    };

    void EnsureSeeded(const GameObject* selectedObject);
    void RebuildGraph(const GameObject* selectedObject);
    bool LoadAsset();
    bool SaveAsset();
    std::string BuildAssetDocument() const;
    int AddFieldColumn(std::span<const BlueprintFieldDescriptor> fields, float startX, float startY, float rowSpacing);
    int AddFieldNode(const BlueprintFieldDescriptor& field, float x, float y, const std::string& customTitle = "");
    int AddComponentNode(const BlueprintComponent& component, float x, float y);
    int AddUIElementNode(UIElementKind kind, float graphX, float graphY);
    int AddCommentNode(const std::string& title, float x, float y, float width = 320.0f, float height = 180.0f);
    bool AddLink(const PinReference& firstPin, const PinReference& secondPin, uint32_t color = 0);
    void AddLink(int fromNodeId, int toNodeId, uint32_t color = 0xFF4B4B4B);
    void RemoveNode(int nodeId);
    Node* FindNode(int nodeId);
    const Node* FindNode(int nodeId) const;
    BlueprintComponent* FindComponent(int componentId);
    const BlueprintComponent* FindComponent(int componentId) const;
    int AddComponent(ComponentKind kind);
    void RemoveComponent(int componentId);
    Node* HitTestNode(float screenX, float screenY, float canvasX, float canvasY);
    PinReference HitTestPin(float screenX, float screenY, float canvasX, float canvasY) const;
    PinReference FindCompatibleDropPin(const PinReference& dragStartPin, float screenX, float screenY,
                                       float canvasX, float canvasY);
    bool CanNodeUsePin(const Node& node, PinKind kind, PinDirection direction) const;
    bool GetPinScreenPosition(const Node& node, PinKind kind, PinDirection direction,
                              float canvasX, float canvasY,
                              float& outX, float& outY, uint32_t* outColor = nullptr) const;
    uint32_t GetPinColor(const Node& node, PinKind kind) const;
    bool NormalizePins(const PinReference& firstPin, const PinReference& secondPin,
                       PinReference& outOutputPin, PinReference& outInputPin) const;
    bool CanConnectPins(const PinReference& firstPin, const PinReference& secondPin) const;
    void ClearLinkDrag();
    void UpdateInteraction(InputManager& inputManager, float canvasX, float canvasY, float canvasW, float canvasH);
    void FrameNode(int nodeId, float canvasW, float canvasH);
    void OpenCreateMenu(float screenX, float screenY, float canvasX, float canvasY);
    bool SpawnNodeFromCreateMenu(const std::string& itemId);
    const BlueprintFieldDescriptor* ResolveFieldDescriptor(const std::string& category, const std::string& name) const;
    void RefreshAssetBrowserItems();
    bool TryAssignDraggedAsset(float screenX, float screenY, float canvasX, float canvasY);
    bool AssignAssetToNode(Node& node, const std::wstring& assetPath);
    bool NodeAcceptsAssetDrop(const Node& node) const;
    std::wstring GetProjectAssetsRoot() const;
    std::string BuildFieldPreview(const GameObject* selectedObject, const BlueprintFieldDescriptor& field) const;
    std::string BuildFieldTypeLabel(BlueprintFieldType type) const;
    std::string BuildComponentTypeLabel(ComponentKind kind) const;
    std::string BuildComponentSummary(const BlueprintComponent& component) const;
    std::string BuildUIElementTypeLabel(UIElementKind kind) const;
    std::string BuildTargetLabel(const GameObject* targetObject) const;
    std::string BuildVisualTypeLabel(NodeVisualKind visual) const;
    void SetStatus(const std::string& message, uint32_t color, DWORD durationMs = 2600);
    int NextNodeId();
    int NextComponentId();

    bool m_isOpen = false;
    AssetKind m_assetKind = AssetKind::Actor;
    bool m_graphDirty = true;
    DocumentTab m_activeTab = DocumentTab::EventGraph;
    std::string m_targetLabel = "Level Blueprint";
    std::wstring m_assetPath;
    std::vector<BlueprintComponent> m_components;
    std::vector<AssetBrowserItem> m_assetBrowserItems;
    std::vector<Node> m_nodes;
    std::vector<Link> m_links;
    int m_nextNodeId = 1;
    int m_nextComponentId = 1;
    int m_selectedNodeId = -1;
    int m_selectedComponentId = -1;
    int m_draggingNodeId = -1;
    bool m_isDraggingLink = false;
    PinReference m_dragLinkStartPin;
    PinReference m_dragLinkHoverPin;
    bool m_isPanning = false;
    bool m_contextClickPending = false;
    bool m_deleteWasDown = false;
    int m_lastMouseX = 0;
    int m_lastMouseY = 0;
    int m_contextPressMouseX = 0;
    int m_contextPressMouseY = 0;
    DirectX::XMFLOAT2 m_pan = {24.0f, 24.0f};
    float m_zoom = 1.0f;
    bool m_showCreateMenu = false;
    float m_createMenuScreenX = 0.0f;
    float m_createMenuScreenY = 0.0f;
    float m_createMenuGraphX = 0.0f;
    float m_createMenuGraphY = 0.0f;
    std::string m_createSearch;
    bool m_createSearchActive = false;
    bool m_showRigidBodyFields = false;
    bool m_showColliderFields = false;
    bool m_selectedNodeTextInputActive = false;
    int m_draggingDesignerNodeId = -1;
    bool m_isResizingDesignerNode = false;
    bool m_assetBrowserDirty = true;
    int m_draggingAssetBrowserIndex = -1;
    std::string m_statusMessage;
    uint32_t m_statusColor = 0xFF9A9A9A;
    DWORD m_statusUntil = 0;
    std::string m_savedAssetDocument;
    bool m_hasSavedAssetDocument = false;
};
