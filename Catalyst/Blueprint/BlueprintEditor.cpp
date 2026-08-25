#include "BlueprintEditor_Internal.h"

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

    DrawLayout L = ComputeLayout(width, height);
    L.interactionsBlocked = interactionsBlocked;
    L.windowHandle = windowHandle;

    drawList.AddRectFilled(0.0f, 0.0f, width, height, 0xFF111114);
    drawList.AddRectFilled(0.0f, 0.0f, width, L.menuBarH, 0xFF0D0D0F);
    drawList.AddRectFilled(0.0f, L.menuBarH, width, L.toolBarH, 0xFF17181B);
    drawList.AddRectFilled(0.0f, L.menuBarH + L.toolBarH, width, L.graphTabH, 0xFF1C1D21);
    drawList.AddRectFilled(0.0f, L.menuBarH + L.toolBarH + L.graphTabH, width, L.graphToolsH, 0xFF202228);

    DrawMenuToolbar(drawList, uiCtx, fontManager, inputManager, L, selectedObject);
    DrawPalettePanel(drawList, uiCtx, fontManager, inputManager, L);
    DrawInspectorPanel(drawList, uiCtx, fontManager, inputManager, L, selectedObject);

    if (L.isViewportTab) {
        DrawViewportTab(drawList, uiCtx, fontManager, inputManager, L);
    } else if (L.isConstructionTab) {
        DrawConstructionTab(drawList, uiCtx, fontManager, inputManager, L);
    } else {
        DrawEventGraphTab(drawList, uiCtx, fontManager, inputManager, L, selectedObject);
    }

    drawList.AddRectFilled(L.canvasX - 1.0f, L.workspaceTop, 1.0f, L.canvasH, 0xFF000000);
    drawList.AddRectFilled(L.inspectorX - 1.0f, L.workspaceTop, 1.0f, L.canvasH, 0xFF000000);
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
