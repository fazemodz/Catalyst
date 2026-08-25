#include "UIXMLParser.h"
#include "UIXMLStyleSheet.h"
#include "tinyxml2.h"   // resolved via Lib/tinyxml2 include path
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <windows.h>  // WideCharToMultiByte

namespace UIXML {

// ── Color helper ─────────────────────────────────────────────────────────────
uint32_t ParseHexColor(const char* str, uint32_t defaultColor) {
    if (!str || str[0] != '#') return defaultColor;
    ++str; // skip '#'
    size_t len = strlen(str);
    // Expand shorthand: RGB → RRGGBB, ARGB → AARRGGBB
    char buf[9] = {};
    if (len == 3) {
        buf[0]=str[0]; buf[1]=str[0];
        buf[2]=str[1]; buf[3]=str[1];
        buf[4]=str[2]; buf[5]=str[2];
        buf[6]='F'; buf[7]='F';
        len = 8;
        str = buf;
    } else if (len == 4) {
        buf[0]=str[0]; buf[1]=str[0];
        buf[2]=str[1]; buf[3]=str[1];
        buf[4]=str[2]; buf[5]=str[2];
        buf[6]=str[3]; buf[7]=str[3];
        len = 8;
        str = buf;
    } else if (len == 6) {
        // RRGGBB — assume opaque (FF alpha)
        char tmp[9];
        tmp[0]='F'; tmp[1]='F';
        memcpy(tmp+2, str, 6);
        tmp[8]=0;
        str = tmp;
        len = 8;
    } else if (len != 8) {
        return defaultColor;
    }
    char* end;
    unsigned long v = strtoul(str, &end, 16);
    if (end != str + 8) return defaultColor;
    // Input is AARRGGBB
    return static_cast<uint32_t>(v);
}

// ── Tag → NodeType ────────────────────────────────────────────────────────────
static NodeType TagToNodeType(const char* tag) {
    // lower-case comparison
    std::string t(tag);
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c){ return std::tolower(c); });
    if (t == "canvas")    return NodeType::Canvas;
    if (t == "button")    return NodeType::Button;
    if (t == "image")     return NodeType::Image;
    if (t == "textblock") return NodeType::TextBlock;
    if (t == "panel")     return NodeType::Panel;
    return NodeType::Unknown;
}

// ── Attribute resolution helper ───────────────────────────────────────────────
static const char* ResolveAttr(const tinyxml2::XMLElement* elem,
                                const char* attrName,
                                const StyleSheet* styles,
                                const char* styleClass) {
    // 1. Inline attribute wins
    const char* v = elem->Attribute(attrName);
    if (v) return v;
    // 2. Style class
    if (styles && styleClass && *styleClass) {
        const auto& cls = styles->Find(styleClass);
        auto it = cls.find(attrName);
        if (it != cls.end()) return it->second.c_str();
    }
    return nullptr;
}

// ── Recursive node builder ────────────────────────────────────────────────────
static Node BuildNode(const tinyxml2::XMLElement* elem, const StyleSheet* styles) {
    Node node;
    node.rawTag = elem->Name();
    node.type   = TagToNodeType(elem->Name());

    // Collect all raw attributes
    for (auto* a = elem->FirstAttribute(); a; a = a->Next())
        node.attrs[a->Name()] = a->Value();

    node.id         = node.attrs.count("id")    ? node.attrs["id"]    : "";
    node.styleClass = node.attrs.count("class") ? node.attrs["class"] : "";

    auto resolve = [&](const char* name) -> const char* {
        return ResolveAttr(elem, name, styles, node.styleClass.c_str());
    };

    // Geometry
    if (const char* v = resolve("x"))      node.x = std::strtof(v, nullptr);
    if (const char* v = resolve("y"))      node.y = std::strtof(v, nullptr);
    if (const char* v = resolve("width"))  node.w = std::strtof(v, nullptr);
    if (const char* v = resolve("height")) node.h = std::strtof(v, nullptr);

    // Colors
    if (const char* v = resolve("background"))  node.background = ParseHexColor(v, 0x00000000);
    if (const char* v = resolve("textColor"))   node.textColor  = ParseHexColor(v, 0xFFFFFFFF);
    if (const char* v = resolve("tint"))        node.tint       = ParseHexColor(v, 0xFFFFFFFF);

    // Source (Image)
    if (const char* v = resolve("src")) node.src = v;

    // Visibility
    if (const char* v = resolve("visible")) {
        std::string vis(v);
        std::transform(vis.begin(), vis.end(), vis.begin(), ::tolower);
        node.visible = (vis != "false" && vis != "0");
    }

    // Inner text content
    const char* text = elem->GetText();
    if (text) {
        std::string raw(text);
        // trim
        size_t s = raw.find_first_not_of(" \t\r\n");
        size_t e = raw.find_last_not_of(" \t\r\n");
        if (s != std::string::npos) node.text = raw.substr(s, e - s + 1);
    }

    // Recurse children
    for (auto* child = elem->FirstChildElement(); child; child = child->NextSiblingElement())
        node.children.push_back(BuildNode(child, styles));

    return node;
}

// ── Public API ────────────────────────────────────────────────────────────────
Document ParseFromText(const std::string& xmlText, const StyleSheet* styles) {
    Document doc;
    if (xmlText.empty()) {
        doc.errorMessage = "Empty XML input";
        return doc;
    }
    tinyxml2::XMLDocument xml;
    auto err = xml.Parse(xmlText.c_str(), xmlText.size());
    if (xml.Error()) {
        doc.errorMessage = xml.ErrorStr();
        return doc;
    }
    auto* root = xml.RootElement();
    if (!root) {
        doc.errorMessage = "No root element found";
        return doc;
    }
    doc.root  = BuildNode(root, styles);
    doc.valid = true;
    return doc;
}

Document ParseFromFile(const std::wstring& path, const StyleSheet* styles) {
    // Convert wide path to UTF-8 for file I/O
    int needed = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string narrow(needed - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, narrow.data(), needed, nullptr, nullptr);

    std::ifstream f(narrow, std::ios::binary);
    if (!f) {
        Document doc;
        doc.errorMessage = "Cannot open file: " + narrow;
        return doc;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ParseFromText(ss.str(), styles);
}

} // namespace UIXML
