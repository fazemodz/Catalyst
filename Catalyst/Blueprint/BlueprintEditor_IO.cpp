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
    Json::Value rootValue;
    Json::Parser parser(content);
    if (!parser.Parse(rootValue) || rootValue.type != Json::ValueType::Object) {
        return false;
    }

    const std::string assetType = Json::GetString(rootValue, "type", "CatalystBlueprint");
    m_assetKind = assetType == "CatalystUIBlueprint" ? AssetKind::UI : AssetKind::Actor;
    const bool isUIAsset = IsUIAsset();
    const Json::Value* graphRoot = &rootValue;
    if (isUIAsset) {
        const Json::Value* graphValue = Json::FindField(rootValue, "graph");
        if (graphValue != nullptr && graphValue->type == Json::ValueType::Object) {
            graphRoot = graphValue;
        }
    }

    if ((!isUIAsset && Json::GetBool(rootValue, "seedDefaultGraph", false)) ||
        (isUIAsset && Json::GetBool(*graphRoot, "seedDefaultGraph", false))) {
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

    const Json::Value* viewValue = Json::FindField(*graphRoot, "view");
    if (viewValue != nullptr && viewValue->type == Json::ValueType::Object) {
        m_pan.x = Json::GetNumber(*viewValue, "panX", 24.0f);
        m_pan.y = Json::GetNumber(*viewValue, "panY", 42.0f);
        m_zoom = Json::GetNumber(*viewValue, "zoom", 1.0f);
    } else {
        m_pan = {24.0f, 42.0f};
        m_zoom = 1.0f;
    }
    m_zoom = (std::max)(0.75f, (std::min)(m_zoom, 1.45f));

    int highestComponentId = 0;
    const Json::Value* componentsValue = isUIAsset ? nullptr : Json::FindField(rootValue, "components");
    if (!isUIAsset && componentsValue != nullptr && componentsValue->type == Json::ValueType::Array) {
        for (const Json::Value& componentValue : componentsValue->arrayValue) {
            if (componentValue.type != Json::ValueType::Object) {
                continue;
            }

            BlueprintComponent component;
            component.id = Json::GetInt(componentValue, "id", highestComponentId + 1);
            component.name = Json::GetString(componentValue, "name", "Component");
            component.kind = ComponentKindFromString(Json::GetString(componentValue, "kind", "StaticMesh"));
            component.assetPath = ResolveBlueprintReferencePath(m_assetPath, Json::GetString(componentValue, "assetPath"));
            component.location = Json::GetFloat3(componentValue, "location", {0.0f, 0.0f, 0.0f});
            component.rotation = Json::GetFloat3(componentValue, "rotation", {0.0f, 0.0f, 0.0f});
            component.scale = Json::GetFloat3(componentValue, "scale", {1.0f, 1.0f, 1.0f});
            component.possessOnPlay = Json::GetBool(componentValue, "possessOnPlay", false);
            component.triggerShape =
                PhysicsColliderShapeFromString(Json::GetString(componentValue, "triggerShape", "Box"));
            component.triggerExtents = Json::GetFloat3(componentValue, "triggerExtents", {0.75f, 0.75f, 0.75f});
            component.triggerRadius = Json::GetNumber(componentValue, "triggerRadius", 0.75f);
            component.exposeToGraph = Json::GetBool(componentValue, "exposeToGraph", true);
            m_components.push_back(component);
            highestComponentId = (std::max)(highestComponentId, component.id);
        }
    }

    int highestNodeId = 0;
    const Json::Value* nodesValue = Json::FindField(*graphRoot, "nodes");
    if (nodesValue != nullptr && nodesValue->type == Json::ValueType::Array) {
        for (const Json::Value& nodeValue : nodesValue->arrayValue) {
            if (nodeValue.type != Json::ValueType::Object) {
                continue;
            }

            Node node;
            node.id = Json::GetInt(nodeValue, "id", highestNodeId + 1);
            node.title = Json::GetString(nodeValue, "title", "Node");
            node.subtitle = Json::GetString(nodeValue, "subtitle", "");
            node.x = Json::GetNumber(nodeValue, "x", 0.0f);
            node.y = Json::GetNumber(nodeValue, "y", 0.0f);
            node.width = Json::GetNumber(nodeValue, "width", 210.0f);
            node.height = Json::GetNumber(nodeValue, "height", 92.0f);
            node.visual = NodeVisualFromString(Json::GetString(nodeValue, "visual", "Function"));
            node.nodeTypeId = Json::GetString(nodeValue, "nodeTypeId");
            node.fieldCategory = Json::GetString(nodeValue, "fieldCategory");
            node.fieldName = Json::GetString(nodeValue, "fieldName");
            node.componentId = Json::GetInt(nodeValue, "componentId", -1);
            node.componentName = Json::GetString(nodeValue, "componentName");
            node.componentKind = ComponentKindFromString(Json::GetString(nodeValue, "componentKind", "StaticMesh"));
            node.uiElementKind = UIElementKindFromString(Json::GetString(nodeValue, "uiElementKind"));
            if (node.uiElementKind == UIElementKind::None) {
                node.uiElementKind = UIElementKindFromNodeType(node.nodeTypeId);
            }
            node.canvasX = Json::GetNumber(nodeValue, "canvasX", 32.0f);
            node.canvasY = Json::GetNumber(nodeValue, "canvasY", 32.0f);
            node.canvasWidth = Json::GetNumber(nodeValue, "canvasWidth", 180.0f);
            node.canvasHeight = Json::GetNumber(nodeValue, "canvasHeight", 56.0f);
            node.displayText = Json::GetString(nodeValue, "displayText");
            node.tint = Json::GetFloat4(nodeValue, "tint", UIntColorToFloat4(UIElementAccentColor(node.uiElementKind)));
            node.assetPath = ResolveBlueprintReferencePath(m_assetPath, Json::GetString(nodeValue, "assetPath"));
            node.visibleInGame = Json::GetBool(nodeValue, "visibleInGame", true);
            node.canDelete = Json::GetBool(nodeValue, "canDelete", true);
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

    const Json::Value* linksValue = Json::FindField(*graphRoot, "links");
    if (linksValue != nullptr && linksValue->type == Json::ValueType::Array) {
        for (const Json::Value& linkValue : linksValue->arrayValue) {
            if (linkValue.type != Json::ValueType::Object) {
                continue;
            }

            PinReference outputPin;
            outputPin.nodeId = Json::GetInt(linkValue, "fromNodeId", 0);
            outputPin.kind = PinKindFromString(Json::GetString(linkValue, "fromPinKind", "Exec"));
            outputPin.direction = PinDirection::Output;

            PinReference inputPin;
            inputPin.nodeId = Json::GetInt(linkValue, "toNodeId", 0);
            inputPin.kind = PinKindFromString(Json::GetString(linkValue, "toPinKind", "Exec"));
            inputPin.direction = PinDirection::Input;

            AddLink(outputPin, inputPin, Json::GetUInt(linkValue, "color", 0xFF4B4B4B));
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
    const int savedSelection = Json::GetInt(*graphRoot, "selectedNodeId", -1);
    m_selectedNodeId = (FindNode(savedSelection) != nullptr) ? savedSelection : (m_nodes.empty() ? -1 : m_nodes.front().id);
    const int savedComponentSelection = isUIAsset ? -1 : Json::GetInt(rootValue, "selectedComponentId", -1);
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
            file << "          \"title\": \"" << Json::Escape(node.title) << "\",\n";
            file << "          \"subtitle\": \"" << Json::Escape(node.subtitle) << "\",\n";
            file << "          \"x\": " << node.x << ",\n";
            file << "          \"y\": " << node.y << ",\n";
            file << "          \"width\": " << node.width << ",\n";
            file << "          \"height\": " << node.height << ",\n";
            file << "          \"visual\": \"" << NodeVisualToString(node.visual) << "\",\n";
            file << "          \"nodeTypeId\": \"" << Json::Escape(node.nodeTypeId) << "\",\n";
            file << "          \"fieldCategory\": \"" << Json::Escape(fieldCategory) << "\",\n";
            file << "          \"fieldName\": \"" << Json::Escape(fieldName) << "\",\n";
            file << "          \"componentId\": " << node.componentId << ",\n";
            file << "          \"componentName\": \"" << Json::Escape(node.componentName) << "\",\n";
            file << "          \"componentKind\": \"" << ComponentKindToString(node.componentKind) << "\",\n";
            file << "          \"uiElementKind\": \"" << UIElementKindToString(node.uiElementKind) << "\",\n";
            file << "          \"canvasX\": " << node.canvasX << ",\n";
            file << "          \"canvasY\": " << node.canvasY << ",\n";
            file << "          \"canvasWidth\": " << node.canvasWidth << ",\n";
            file << "          \"canvasHeight\": " << node.canvasHeight << ",\n";
            file << "          \"displayText\": \"" << Json::Escape(node.displayText) << "\",\n";
            file << "          \"tint\": ";
            Json::WriteFloat4(file, node.tint);
            file << ",\n";
            file << "          \"assetPath\": \"" << Json::Escape(WideToUtf8(storedNodeAssetPath)) << "\",\n";
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
        file << "      \"name\": \"" << Json::Escape(component.name) << "\",\n";
        file << "      \"kind\": \"" << ComponentKindToString(component.kind) << "\",\n";
        file << "      \"assetPath\": \"" << Json::Escape(WideToUtf8(storedAssetPath)) << "\",\n";
        file << "      \"location\": ";
        Json::WriteFloat3(file, component.location);
        file << ",\n";
        file << "      \"rotation\": ";
        Json::WriteFloat3(file, component.rotation);
        file << ",\n";
        file << "      \"scale\": ";
        Json::WriteFloat3(file, component.scale);
        file << ",\n";
        file << "      \"possessOnPlay\": " << (component.possessOnPlay ? "true" : "false") << ",\n";
        file << "      \"triggerShape\": \"" << PhysicsColliderShapeToString(component.triggerShape) << "\",\n";
        file << "      \"triggerExtents\": ";
        Json::WriteFloat3(file, component.triggerExtents);
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
        file << "      \"title\": \"" << Json::Escape(node.title) << "\",\n";
        file << "      \"subtitle\": \"" << Json::Escape(node.subtitle) << "\",\n";
        file << "      \"x\": " << node.x << ",\n";
        file << "      \"y\": " << node.y << ",\n";
        file << "      \"width\": " << node.width << ",\n";
        file << "      \"height\": " << node.height << ",\n";
        file << "      \"visual\": \"" << NodeVisualToString(node.visual) << "\",\n";
        file << "      \"nodeTypeId\": \"" << Json::Escape(node.nodeTypeId) << "\",\n";
        file << "      \"fieldCategory\": \"" << Json::Escape(fieldCategory) << "\",\n";
        file << "      \"fieldName\": \"" << Json::Escape(fieldName) << "\",\n";
        file << "      \"componentId\": " << node.componentId << ",\n";
        file << "      \"componentName\": \"" << Json::Escape(node.componentName) << "\",\n";
        file << "      \"componentKind\": \"" << ComponentKindToString(node.componentKind) << "\",\n";
        file << "      \"uiElementKind\": \"" << UIElementKindToString(node.uiElementKind) << "\",\n";
        file << "      \"canvasX\": " << node.canvasX << ",\n";
        file << "      \"canvasY\": " << node.canvasY << ",\n";
        file << "      \"canvasWidth\": " << node.canvasWidth << ",\n";
        file << "      \"canvasHeight\": " << node.canvasHeight << ",\n";
        file << "      \"displayText\": \"" << Json::Escape(node.displayText) << "\",\n";
        file << "      \"tint\": ";
        Json::WriteFloat4(file, node.tint);
        file << ",\n";
        file << "      \"assetPath\": \"" << Json::Escape(WideToUtf8(storedNodeAssetPath)) << "\",\n";
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
