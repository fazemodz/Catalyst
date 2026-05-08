#include "BlueprintEditor_Internal.h"

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
            node.visibleInGame = GetJsonBool(nodeValue, "visibleInGame", true);
            node.canDelete = GetJsonBool(nodeValue, "canDelete", true);
            node.field = ResolveFieldDescriptor(node.fieldCategory, node.fieldName);
            if (const BlueprintComponent* component = FindComponent(node.componentId)) {
                node.componentName = component->name;
                node.componentKind = component->kind;
            }
            // When creating a UI element node without explicit display text,
            // default to an empty string. Existing files that intentionally
            // store placeholder text will keep that value, but new nodes are
            // now blank until the user enters a label.
            if (node.displayText.empty()) {
                if (node.uiElementKind == UIElementKind::Button) {
                    node.displayText = "";
                } else if (node.uiElementKind == UIElementKind::TextBlock) {
                    node.displayText = "";
                } else if (node.uiElementKind == UIElementKind::Canvas) {
                    node.displayText = "";
                } else if (node.uiElementKind == UIElementKind::Image) {
                    node.displayText = "";
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
            file << "          \"visibleInGame\": " << (node.visibleInGame ? "true" : "false") << ",\n";
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
        file << "      \"visibleInGame\": " << (node.visibleInGame ? "true" : "false") << ",\n";
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
