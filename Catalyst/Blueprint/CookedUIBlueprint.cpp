#include "CookedUIBlueprint.h"

#include "Json.h"
#include "Nodes/BlueprintNodeLibrary.h"
#include "../UI/XML/UIXMLParser.h"
#include "../UI/XML/UIXMLStyleSheet.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace CookedUIBlueprint {
namespace {

uint32_t Float4ToUIntColor(const std::vector<float>& color) {
    const auto ToByte = [](float value) {
        const float clamped = (std::max)(0.0f, (std::min)(1.0f, value));
        return static_cast<uint32_t>(clamped * 255.0f + 0.5f);
    };

    const uint32_t r = ToByte(color.size() > 0 ? color[0] : 1.0f);
    const uint32_t g = ToByte(color.size() > 1 ? color[1] : 1.0f);
    const uint32_t b = ToByte(color.size() > 2 ? color[2] : 1.0f);
    const uint32_t a = ToByte(color.size() > 3 ? color[3] : 1.0f);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

struct AuthorNode {
    int id = 0;
    std::string nodeTypeId;
    std::string displayText;
    std::string assetPath;
    float canvasX = 0.0f;
    float canvasY = 0.0f;
    float canvasWidth = 0.0f;
    float canvasHeight = 0.0f;
    uint32_t tintColor = 0xFFFFFFFF;
    bool visibleInGame = true;
};

struct AuthorLink {
    int fromNodeId = 0;
    int toNodeId = 0;
    std::string fromPinKind;
    std::string toPinKind;
};

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

ElementType ElementTypeFromNodeType(const std::string& nodeTypeId) {
    if (IsUIButtonNodeType(nodeTypeId)) {
        return ElementType::Button;
    }
    if (IsUIImageNodeType(nodeTypeId)) {
        return ElementType::Image;
    }
    if (IsUITextBlockNodeType(nodeTypeId)) {
        return ElementType::TextBlock;
    }
    return ElementType::Canvas;
}

std::string DefaultDisplayTextForNodeType(const std::string& nodeTypeId) {
    if (IsUIButtonNodeType(nodeTypeId)) {
        return "Button";
    }
    if (IsUITextBlockNodeType(nodeTypeId)) {
        return "Text";
    }
    if (IsUIImageNodeType(nodeTypeId)) {
        return "Image";
    }
    if (IsUICanvasNodeType(nodeTypeId)) {
        return "Canvas";
    }
    return "";
}

std::string ResolveInstructionAssetPath(const std::wstring& sourceAssetPath, const std::string& storedAssetPath) {
    if (storedAssetPath.empty()) {
        return "";
    }

    fs::path resolvedPath = fs::path(storedAssetPath);
    if (resolvedPath.is_relative() && !sourceAssetPath.empty()) {
        resolvedPath = fs::path(sourceAssetPath).parent_path() / resolvedPath;
    }
    return resolvedPath.lexically_normal().string();
}

std::vector<int> CollectExecTargets(int nodeId, const std::vector<AuthorLink>& links) {
    std::vector<int> targets;
    for (const AuthorLink& link : links) {
        if (link.fromNodeId == nodeId && link.fromPinKind == "Exec" && link.toPinKind == "Exec") {
            targets.push_back(link.toNodeId);
        }
    }

    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    return targets;
}

void AppendInstructionsForNode(const AuthorNode& node,
                               const std::wstring& sourceAssetPath,
                               const std::map<int, AuthorNode>& nodes,
                               const std::vector<AuthorLink>& links,
                               Asset& outAsset) {
    if (node.nodeTypeId == BlueprintNodes::kSetTextColorNodeId) {
        for (const AuthorLink& link : links) {
            if (link.toNodeId != node.id || link.fromPinKind != "Data" || link.toPinKind != "Data") {
                continue;
            }

            auto targetNode = nodes.find(link.fromNodeId);
            if (targetNode == nodes.end() || !IsUITextBlockNodeType(targetNode->second.nodeTypeId)) {
                continue;
            }

            Instruction instruction;
            instruction.opcode = Opcode::SetTextColor;
            instruction.targetNodeId = targetNode->second.id;
            instruction.color = node.tintColor;
            outAsset.instructions.push_back(instruction);
        }
        return;
    }

    if (node.nodeTypeId == BlueprintNodes::kOpenLevelNodeId && !node.assetPath.empty()) {
        Instruction instruction;
        instruction.opcode = Opcode::OpenLevel;
        instruction.text = ResolveInstructionAssetPath(sourceAssetPath, node.assetPath);
        outAsset.instructions.push_back(instruction);
        return;
    }

    if (node.nodeTypeId == BlueprintNodes::kSwapWidgetNodeId && !node.assetPath.empty()) {
        Instruction instruction;
        instruction.opcode = Opcode::SwapWidget;
        instruction.text = ResolveInstructionAssetPath(sourceAssetPath, node.assetPath);
        outAsset.instructions.push_back(instruction);
        return;
    }

    if (node.nodeTypeId == BlueprintNodes::kPrintLogNodeId) {
        Instruction instruction;
        instruction.opcode = Opcode::Print;
        instruction.text = node.displayText.empty() ? "(empty message)" : node.displayText;
        outAsset.instructions.push_back(instruction);
    }
}

void CompileExecChain(int nodeId,
                      const std::wstring& sourceAssetPath,
                      const std::map<int, AuthorNode>& nodes,
                      const std::vector<AuthorLink>& links,
                      Asset& outAsset,
                      std::set<int>& visitedNodes) {
    if (!visitedNodes.insert(nodeId).second) {
        return;
    }

    auto currentNode = nodes.find(nodeId);
    if (currentNode == nodes.end()) {
        return;
    }

    AppendInstructionsForNode(currentNode->second, sourceAssetPath, nodes, links, outAsset);
    for (int nextNodeId : CollectExecTargets(nodeId, links)) {
        CompileExecChain(nextNodeId, sourceAssetPath, nodes, links, outAsset, visitedNodes);
    }
}

std::wstring ToLowerExtension(const std::wstring& extension) {
    std::wstring lowered = extension;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), towlower);
    return lowered;
}

template <typename T>
void WriteScalar(std::ostream& stream, const T& value) {
    stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

void WriteString(std::ostream& stream, const std::string& value) {
    const uint32_t length = static_cast<uint32_t>(value.size());
    WriteScalar(stream, length);
    if (length > 0) {
        stream.write(value.data(), length);
    }
}

class BufferReader {
public:
    explicit BufferReader(const std::vector<uint8_t>& bytes)
        : m_bytes(bytes) {
    }

    template <typename T>
    bool Read(T& outValue) {
        if (m_offset + sizeof(T) > m_bytes.size()) {
            return false;
        }

        std::memcpy(&outValue, m_bytes.data() + m_offset, sizeof(T));
        m_offset += sizeof(T);
        return true;
    }

    bool ReadString(std::string& outValue) {
        uint32_t length = 0;
        if (!Read(length) || m_offset + length > m_bytes.size()) {
            return false;
        }

        outValue.assign(reinterpret_cast<const char*>(m_bytes.data() + m_offset), length);
        m_offset += length;
        return true;
    }

private:
    const std::vector<uint8_t>& m_bytes;
    size_t m_offset = 0;
};

struct BinaryHeader {
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t elementCount = 0;
    uint32_t instructionCount = 0;
    uint32_t eventCount = 0;
};

constexpr uint32_t kCookedMagic = 0x42554943; // CUIB
constexpr uint32_t kCookedVersion = 2;
}

bool IsUIBlueprintPath(const std::wstring& assetPath) {
    const auto ext = ToLowerExtension(fs::path(assetPath).extension().wstring());
    return ext == L".catalystuiblueprint" || ext == L".xml";
}

std::wstring GetCookedAssetPath(const std::wstring& sourceAssetPath) {
    fs::path cookedPath(sourceAssetPath);
    cookedPath.replace_extension(L".bp_c");
    return cookedPath.lexically_normal().wstring();
}

bool CompileFromJsonFile(const std::wstring& sourceAssetPath, Asset& outAsset, std::string* outError) {
    outAsset = {};
    outAsset.sourceAssetPath = sourceAssetPath;

    std::ifstream inputFile(fs::path(sourceAssetPath), std::ios::binary);
    if (!inputFile.is_open()) {
        if (outError != nullptr) {
            *outError = "Could not open UI Blueprint.";
        }
        return false;
    }

    const std::string content((std::istreambuf_iterator<char>(inputFile)), std::istreambuf_iterator<char>());
    Json::Value rootValue;
    Json::Parser parser(content);
    if (!parser.Parse(rootValue) || rootValue.type != Json::ValueType::Object) {
        if (outError != nullptr) {
            *outError = "UI Blueprint JSON could not be parsed.";
        }
        return false;
    }

    if (Json::GetString(rootValue, "type") != "CatalystUIBlueprint") {
        if (outError != nullptr) {
            *outError = "Asset is not a Catalyst UI Blueprint.";
        }
        return false;
    }

    const Json::Value* graphValue = Json::FindField(rootValue, "graph");
    if (graphValue == nullptr || graphValue->type != Json::ValueType::Object) {
        if (outError != nullptr) {
            *outError = "UI Blueprint graph block is missing.";
        }
        return false;
    }

    std::map<int, AuthorNode> nodes;
    const Json::Value* nodesValue = Json::FindField(*graphValue, "nodes");
    if (nodesValue != nullptr && nodesValue->type == Json::ValueType::Array) {
        for (const Json::Value& nodeValue : nodesValue->arrayValue) {
            if (nodeValue.type != Json::ValueType::Object) {
                continue;
            }

            AuthorNode node;
            node.id = static_cast<int>(Json::GetNumber(nodeValue, "id", 0.0f));
            node.nodeTypeId = Json::GetString(nodeValue, "nodeTypeId");
            node.displayText = Json::GetString(nodeValue, "displayText");
            node.assetPath = Json::GetString(nodeValue, "assetPath");
            node.canvasX = Json::GetNumber(nodeValue, "canvasX", 0.0f);
            node.canvasY = Json::GetNumber(nodeValue, "canvasY", 0.0f);
            node.canvasWidth = Json::GetNumber(nodeValue, "canvasWidth", 120.0f);
            node.canvasHeight = Json::GetNumber(nodeValue, "canvasHeight", 40.0f);
            node.tintColor = Float4ToUIntColor(Json::GetFloatArray(nodeValue, "tint", 4, {1.0f, 1.0f, 1.0f, 1.0f}));
            const Json::Value* visibleField = Json::FindField(nodeValue, "visibleInGame");
            node.visibleInGame = visibleField == nullptr || (visibleField->type == Json::ValueType::Bool && visibleField->boolValue);
            if (node.displayText.empty()) {
                node.displayText = DefaultDisplayTextForNodeType(node.nodeTypeId);
            }

            nodes[node.id] = node;
            if (IsUIElementNodeType(node.nodeTypeId)) {
                Element element;
                element.id = node.id;
                element.type = ElementTypeFromNodeType(node.nodeTypeId);
                element.displayText = node.displayText;
                element.canvasX = node.canvasX;
                element.canvasY = node.canvasY;
                element.canvasWidth = node.canvasWidth;
                element.canvasHeight = node.canvasHeight;
                element.tintColor = node.tintColor;
                element.visibleInGame = node.visibleInGame;
                outAsset.elements.push_back(element);
            }
        }
    }

    std::vector<AuthorLink> links;
    const Json::Value* linksValue = Json::FindField(*graphValue, "links");
    if (linksValue != nullptr && linksValue->type == Json::ValueType::Array) {
        links.reserve(linksValue->arrayValue.size());
        for (const Json::Value& linkValue : linksValue->arrayValue) {
            if (linkValue.type != Json::ValueType::Object) {
                continue;
            }

            AuthorLink link;
            link.fromNodeId = static_cast<int>(Json::GetNumber(linkValue, "fromNodeId", 0.0f));
            link.toNodeId = static_cast<int>(Json::GetNumber(linkValue, "toNodeId", 0.0f));
            link.fromPinKind = Json::GetString(linkValue, "fromPinKind", "Exec");
            link.toPinKind = Json::GetString(linkValue, "toPinKind", "Exec");
            links.push_back(link);
        }
    }

    for (const auto& [nodeId, node] : nodes) {
        if (node.nodeTypeId == BlueprintNodes::kWidgetConstructNodeId) {
            const uint32_t firstInstructionIndex = static_cast<uint32_t>(outAsset.instructions.size());
            std::set<int> visitedNodes;
            for (int nextNodeId : CollectExecTargets(nodeId, links)) {
                CompileExecChain(nextNodeId, sourceAssetPath, nodes, links, outAsset, visitedNodes);
            }

            EventHandler eventHandler;
            eventHandler.type = EventType::Construct;
            eventHandler.sourceNodeId = nodeId;
            eventHandler.firstInstructionIndex = firstInstructionIndex;
            eventHandler.instructionCount = static_cast<uint32_t>(outAsset.instructions.size()) - firstInstructionIndex;
            outAsset.events.push_back(eventHandler);
        } else if (IsUIButtonNodeType(node.nodeTypeId)) {
            const uint32_t firstInstructionIndex = static_cast<uint32_t>(outAsset.instructions.size());
            std::set<int> visitedNodes;
            for (int nextNodeId : CollectExecTargets(nodeId, links)) {
                CompileExecChain(nextNodeId, sourceAssetPath, nodes, links, outAsset, visitedNodes);
            }

            EventHandler eventHandler;
            eventHandler.type = EventType::ButtonPressed;
            eventHandler.sourceNodeId = nodeId;
            eventHandler.firstInstructionIndex = firstInstructionIndex;
            eventHandler.instructionCount = static_cast<uint32_t>(outAsset.instructions.size()) - firstInstructionIndex;
            outAsset.events.push_back(eventHandler);
        }
    }

    return true;
}

bool SaveToBinaryFile(const std::wstring& cookedAssetPath, const Asset& asset, std::string* outError) {
    try {
        std::error_code ec;
        const fs::path outputPath(cookedAssetPath);
        fs::create_directories(outputPath.parent_path(), ec);

        std::ofstream outputFile(outputPath, std::ios::binary | std::ios::trunc);
        if (!outputFile.is_open()) {
            if (outError != nullptr) {
                *outError = "Could not open cooked output file.";
            }
            return false;
        }

        BinaryHeader header;
        header.magic = kCookedMagic;
        header.version = kCookedVersion;
        header.elementCount = static_cast<uint32_t>(asset.elements.size());
        header.instructionCount = static_cast<uint32_t>(asset.instructions.size());
        header.eventCount = static_cast<uint32_t>(asset.events.size());
        WriteScalar(outputFile, header);

        for (const Element& element : asset.elements) {
            WriteScalar(outputFile, element.id);
            WriteScalar(outputFile, element.type);
            WriteScalar(outputFile, element.canvasX);
            WriteScalar(outputFile, element.canvasY);
            WriteScalar(outputFile, element.canvasWidth);
            WriteScalar(outputFile, element.canvasHeight);
            WriteScalar(outputFile, element.tintColor);
            WriteScalar(outputFile, element.visibleInGame);
            WriteString(outputFile, element.displayText);
        }

        for (const Instruction& instruction : asset.instructions) {
            WriteScalar(outputFile, instruction.opcode);
            WriteScalar(outputFile, instruction.targetNodeId);
            WriteScalar(outputFile, instruction.color);
            WriteScalar(outputFile, instruction.durationSeconds);
            WriteString(outputFile, instruction.text);
        }

        for (const EventHandler& eventHandler : asset.events) {
            WriteScalar(outputFile, eventHandler.type);
            WriteScalar(outputFile, eventHandler.sourceNodeId);
            WriteScalar(outputFile, eventHandler.firstInstructionIndex);
            WriteScalar(outputFile, eventHandler.instructionCount);
        }

        return true;
    } catch (...) {
        if (outError != nullptr) {
            *outError = "Cooked asset write failed.";
        }
        return false;
    }
}

bool LoadFromBinaryBuffer(const std::vector<uint8_t>& bytes, Asset& outAsset, std::string* outError) {
    outAsset = {};
    BufferReader reader(bytes);

    BinaryHeader header;
    if (!reader.Read(header) || header.magic != kCookedMagic || header.version != kCookedVersion) {
        if (outError != nullptr) {
            *outError = "Cooked UI Blueprint header is invalid.";
        }
        return false;
    }

    outAsset.elements.reserve(header.elementCount);
    for (uint32_t index = 0; index < header.elementCount; ++index) {
        Element element;
        if (!reader.Read(element.id) ||
            !reader.Read(element.type) ||
            !reader.Read(element.canvasX) ||
            !reader.Read(element.canvasY) ||
            !reader.Read(element.canvasWidth) ||
            !reader.Read(element.canvasHeight) ||
            !reader.Read(element.tintColor) ||
            !reader.Read(element.visibleInGame) ||
            !reader.ReadString(element.displayText)) {
            if (outError != nullptr) {
                *outError = "Cooked UI element data is truncated.";
            }
            return false;
        }

        outAsset.elements.push_back(element);
    }

    outAsset.instructions.reserve(header.instructionCount);
    for (uint32_t index = 0; index < header.instructionCount; ++index) {
        Instruction instruction;
        if (!reader.Read(instruction.opcode) ||
            !reader.Read(instruction.targetNodeId) ||
            !reader.Read(instruction.color) ||
            !reader.Read(instruction.durationSeconds) ||
            !reader.ReadString(instruction.text)) {
            if (outError != nullptr) {
                *outError = "Cooked instruction data is truncated.";
            }
            return false;
        }

        outAsset.instructions.push_back(instruction);
    }

    outAsset.events.reserve(header.eventCount);
    for (uint32_t index = 0; index < header.eventCount; ++index) {
        EventHandler eventHandler;
        if (!reader.Read(eventHandler.type) ||
            !reader.Read(eventHandler.sourceNodeId) ||
            !reader.Read(eventHandler.firstInstructionIndex) ||
            !reader.Read(eventHandler.instructionCount)) {
            if (outError != nullptr) {
                *outError = "Cooked event data is truncated.";
            }
            return false;
        }

        outAsset.events.push_back(eventHandler);
    }

    return true;
}

bool LoadFromBinaryFile(const std::wstring& cookedAssetPath, Asset& outAsset, std::string* outError) {
    std::ifstream inputFile(fs::path(cookedAssetPath), std::ios::binary);
    if (!inputFile.is_open()) {
        if (outError != nullptr) {
            *outError = "Cooked UI Blueprint could not be opened.";
        }
        return false;
    }

    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(inputFile)), std::istreambuf_iterator<char>());
    outAsset.sourceAssetPath = cookedAssetPath;
    return LoadFromBinaryBuffer(bytes, outAsset, outError);
}

// ── XML authoring pipeline ────────────────────────────────────────────────────
namespace {

ElementType XmlNodeTypeToElementType(UIXML::NodeType t) {
    switch (t) {
    case UIXML::NodeType::Button:    return ElementType::Button;
    case UIXML::NodeType::Image:     return ElementType::Image;
    case UIXML::NodeType::TextBlock: return ElementType::TextBlock;
    case UIXML::NodeType::Canvas:
    case UIXML::NodeType::Panel:
    default:                         return ElementType::Canvas;
    }
}

void CollectXmlElements(const UIXML::Node& node, int& nextId, Asset& outAsset) {
    if (node.type == UIXML::NodeType::Unknown) {
        for (const auto& child : node.children)
            CollectXmlElements(child, nextId, outAsset);
        return;
    }

    Element elem;
    elem.id          = nextId++;
    elem.type        = XmlNodeTypeToElementType(node.type);
    elem.displayText = node.text;
    elem.canvasX     = node.x;
    elem.canvasY     = node.y;
    elem.canvasWidth  = node.w;
    elem.canvasHeight = node.h;
    elem.tintColor    = (node.type == UIXML::NodeType::Image) ? node.tint : node.background;
    elem.visibleInGame = node.visible;

    // Default display text if empty
    if (elem.displayText.empty()) {
        switch (node.type) {
        case UIXML::NodeType::Button:    elem.displayText = "Button"; break;
        case UIXML::NodeType::TextBlock: elem.displayText = "Text";   break;
        case UIXML::NodeType::Image:     elem.displayText = "Image";  break;
        default:                         elem.displayText = "Canvas"; break;
        }
    }

    outAsset.elements.push_back(elem);

    for (const auto& child : node.children)
        CollectXmlElements(child, nextId, outAsset);
}

} // anonymous namespace

bool CompileFromXmlFile(const std::wstring& sourceAssetPath, Asset& outAsset, std::string* outError) {
    outAsset = {};
    outAsset.sourceAssetPath = sourceAssetPath;

    // Load optional companion stylesheet
    UIXML::StyleSheet sheet;
    fs::path stylePath = fs::path(sourceAssetPath).replace_extension(L".xmlstyle");
    if (fs::exists(stylePath))
        sheet = UIXML::StyleSheet::ParseFromFile(stylePath.wstring());

    UIXML::Document doc = UIXML::ParseFromFile(sourceAssetPath,
                                                sheet.Empty() ? nullptr : &sheet);
    if (!doc.valid) {
        if (outError) *outError = "XML parse error: " + doc.errorMessage;
        return false;
    }

    int nextId = 1;
    CollectXmlElements(doc.root, nextId, outAsset);
    // instructions and events remain empty — logic lives in the Blueprint event graph
    return true;
}
}
