#include "BlueprintEditor_Internal.h"

void BlueprintEditor::RefreshAssetBrowserItems() {
    m_assetBrowserItems.clear();
    m_assetBrowserDirty = false;

    if (IsUIAsset()) {
        return;
    }

    const std::wstring assetsRoot = GetProjectAssetsRoot();
    if (assetsRoot.empty()) {
        return;
    }

    std::error_code ec;
    if (!fs::exists(assetsRoot, ec) || ec) {
        return;
    }

    fs::recursive_directory_iterator iter(assetsRoot, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator endIter;
    while (!ec && iter != endIter) {
        const fs::directory_entry& entry = *iter;
        std::error_code entryError;
        if (entry.is_regular_file(entryError) && !entryError && IsUIBlueprintPath(entry.path().wstring())) {
            AssetBrowserItem item;
            item.path = entry.path().lexically_normal().wstring();
            item.label = entry.path().stem().string();
            item.subtitle = entry.path().parent_path().filename().string();
            m_assetBrowserItems.push_back(item);
        }
        iter.increment(ec);
    }

    std::sort(m_assetBrowserItems.begin(), m_assetBrowserItems.end(), [](const AssetBrowserItem& left, const AssetBrowserItem& right) {
        return ToLowerCopy(left.label) < ToLowerCopy(right.label);
    });
}

bool BlueprintEditor::TryAssignDraggedAsset(float screenX, float screenY, float canvasX, float canvasY) {
    if (m_draggingAssetBrowserIndex < 0 ||
        m_draggingAssetBrowserIndex >= static_cast<int>(m_assetBrowserItems.size())) {
        return false;
    }

    const std::wstring assetPath = m_assetBrowserItems[static_cast<size_t>(m_draggingAssetBrowserIndex)].path;
    PinReference hitPin = HitTestPin(screenX, screenY, canvasX, canvasY);
    if (hitPin.nodeId >= 0 && hitPin.kind == PinKind::Data && hitPin.direction == PinDirection::Input) {
        Node* hitNode = FindNode(hitPin.nodeId);
        if (hitNode != nullptr && NodeAcceptsAssetDrop(*hitNode)) {
            return AssignAssetToNode(*hitNode, assetPath);
        }
    }

    Node* hitNode = HitTestNode(screenX, screenY, canvasX, canvasY);
    if (hitNode != nullptr && NodeAcceptsAssetDrop(*hitNode)) {
        return AssignAssetToNode(*hitNode, assetPath);
    }

    return false;
}

bool BlueprintEditor::AssignAssetToNode(Node& node, const std::wstring& assetPath) {
    if (!NodeAcceptsAssetDrop(node)) {
        return false;
    }

    node.assetPath = NormalizeAssetPath(assetPath);
    if (node.assetPath.empty()) {
        return false;
    }

    SetStatus("Assigned " + fs::path(node.assetPath).filename().string() + " to " + node.title + ".",
              0xFF89D185, 2600);
    return true;
}

bool BlueprintEditor::NodeAcceptsAssetDrop(const Node& node) const {
    return IsCreateWidgetNodeType(node.nodeTypeId) || IsSwapWidgetNodeType(node.nodeTypeId);
}

std::wstring BlueprintEditor::GetProjectAssetsRoot() const {
    std::wstring sourcePath = m_assetPath;
    if (sourcePath.empty()) {
        return L"";
    }

    fs::path current = fs::path(sourcePath).parent_path();
    while (!current.empty()) {
        std::wstring folderName = current.filename().wstring();
        std::transform(folderName.begin(), folderName.end(), folderName.begin(), towlower);
        if (folderName == L"assets") {
            return current.wstring();
        }

        fs::path parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }

    return fs::path(sourcePath).parent_path().wstring();
}
