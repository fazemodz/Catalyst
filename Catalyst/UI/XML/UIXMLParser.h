#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>

// Forward-declare only — tinyxml2.h is included only inside UIXMLParser.cpp
namespace tinyxml2 { class XMLElement; }

namespace UIXML {

struct StyleSheet; // forward-declared; full def in UIXMLStyleSheet.h

enum class NodeType {
    Canvas,
    Button,
    Image,
    TextBlock,
    Panel,
    Unknown
};

using AttrMap = std::map<std::string, std::string>;

struct Node {
    NodeType    type        = NodeType::Unknown;
    std::string rawTag;
    std::string id;
    std::string styleClass;
    std::string text;       // trimmed inner text content
    std::string src;        // src="" for Image nodes
    AttrMap     attrs;      // all raw attributes (verbatim)
    std::vector<Node> children;

    // Resolved geometry
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

    // Resolved colors (ARGB: 0xAARRGGBB)
    uint32_t background = 0x00000000; // transparent default
    uint32_t textColor  = 0xFFFFFFFF;
    uint32_t tint       = 0xFFFFFFFF;

    bool visible = true;
};

struct Document {
    Node        root;
    bool        valid = false;
    std::string errorMessage;
};

// Parse an XML string into a Document.
Document ParseFromText(const std::string& xmlText,
                       const StyleSheet* styles = nullptr);

// Convenience: load from a file path (UTF-16 on Windows).
Document ParseFromFile(const std::wstring& path,
                       const StyleSheet* styles = nullptr);

// Parse a hex color string (#RGB, #RRGGBB, #AARRGGBB) → ARGB uint32.
// Returns defaultColor if the string is invalid.
uint32_t ParseHexColor(const char* str, uint32_t defaultColor = 0xFF000000);

} // namespace UIXML
