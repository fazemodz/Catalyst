#include "tinyxml2.h"
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cassert>
#include <fstream>
#include <sstream>

namespace tinyxml2 {

// ── Entity decoding ──────────────────────────────────────────────────────────
static std::string DecodeEntities(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '&') {
            if (s.compare(i, 5, "&amp;")  == 0) { out += '&';  i += 5; }
            else if (s.compare(i, 4, "&lt;")   == 0) { out += '<';  i += 4; }
            else if (s.compare(i, 4, "&gt;")   == 0) { out += '>';  i += 4; }
            else if (s.compare(i, 6, "&quot;") == 0) { out += '"';  i += 6; }
            else if (s.compare(i, 6, "&apos;") == 0) { out += '\''; i += 6; }
            else { out += s[i++]; }
        } else {
            out += s[i++];
        }
    }
    return out;
}

static std::string TrimWhitespace(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// ── XMLAttribute ─────────────────────────────────────────────────────────────
int XMLAttribute::IntValue(int defaultVal) const {
    int v = defaultVal;
    QueryIntValue(&v);
    return v;
}
float XMLAttribute::FloatValue(float defaultVal) const {
    float v = defaultVal;
    QueryFloatValue(&v);
    return v;
}
bool XMLAttribute::BoolValue(bool defaultVal) const {
    bool v = defaultVal;
    QueryBoolValue(&v);
    return v;
}
XMLError XMLAttribute::QueryIntValue(int* v) const {
    if (!v) return XML_WRONG_ATTRIBUTE_TYPE;
    char* end;
    long val = std::strtol(m_value.c_str(), &end, 10);
    if (end == m_value.c_str()) return XML_WRONG_ATTRIBUTE_TYPE;
    *v = static_cast<int>(val);
    return XML_SUCCESS;
}
XMLError XMLAttribute::QueryFloatValue(float* v) const {
    if (!v) return XML_WRONG_ATTRIBUTE_TYPE;
    char* end;
    float val = std::strtof(m_value.c_str(), &end);
    if (end == m_value.c_str()) return XML_WRONG_ATTRIBUTE_TYPE;
    *v = val;
    return XML_SUCCESS;
}
XMLError XMLAttribute::QueryBoolValue(bool* v) const {
    if (!v) return XML_WRONG_ATTRIBUTE_TYPE;
    if (m_value == "true"  || m_value == "1" || m_value == "yes") { *v = true;  return XML_SUCCESS; }
    if (m_value == "false" || m_value == "0" || m_value == "no")  { *v = false; return XML_SUCCESS; }
    return XML_WRONG_ATTRIBUTE_TYPE;
}

// ── XMLNode ──────────────────────────────────────────────────────────────────
XMLNode::XMLNode() = default;
XMLNode::~XMLNode() {
    XMLNode* child = m_firstChild;
    while (child) {
        XMLNode* next = child->m_nextSibling;
        delete child;
        child = next;
    }
}

void XMLNode::LinkChild(XMLNode* child) {
    child->m_parent = this;
    if (!m_firstChild) {
        m_firstChild = m_lastChild = child;
    } else {
        child->m_prevSibling = m_lastChild;
        m_lastChild->m_nextSibling = child;
        m_lastChild = child;
    }
}

XMLElement* XMLNode::FirstChildElement(const char* name) const {
    for (XMLNode* n = m_firstChild; n; n = n->m_nextSibling) {
        XMLElement* e = n->ToElement();
        if (e && (!name || strcmp(e->Name(), name) == 0))
            return e;
    }
    return nullptr;
}

XMLElement* XMLNode::NextSiblingElement(const char* name) const {
    for (XMLNode* n = m_nextSibling; n; n = n->m_nextSibling) {
        XMLElement* e = n->ToElement();
        if (e && (!name || strcmp(e->Name(), name) == 0))
            return e;
    }
    return nullptr;
}

// ── XMLElement ───────────────────────────────────────────────────────────────
XMLElement::XMLElement(const char* name) { m_value = name; }

XMLElement::~XMLElement() {
    XMLAttribute* a = m_firstAttr;
    while (a) {
        XMLAttribute* next = a->m_next;
        delete a;
        a = next;
    }
}

void XMLElement::AddAttribute(const char* name, const char* value) {
    auto* a = new XMLAttribute();
    a->m_name  = name;
    a->m_value = DecodeEntities(value);
    if (!m_firstAttr) {
        m_firstAttr = m_lastAttr = a;
    } else {
        m_lastAttr->m_next = a;
        m_lastAttr = a;
    }
}

const XMLAttribute* XMLElement::FindAttribute(const char* name) const {
    for (auto* a = m_firstAttr; a; a = a->m_next)
        if (strcmp(a->m_name.c_str(), name) == 0) return a;
    return nullptr;
}

const char* XMLElement::Attribute(const char* name, const char* defaultValue) const {
    const XMLAttribute* a = FindAttribute(name);
    return a ? a->Value() : defaultValue;
}

int   XMLElement::IntAttribute(const char* name, int v)     const { const XMLAttribute* a = FindAttribute(name); return a ? a->IntValue(v) : v; }
float XMLElement::FloatAttribute(const char* name, float v) const { const XMLAttribute* a = FindAttribute(name); return a ? a->FloatValue(v) : v; }
bool  XMLElement::BoolAttribute(const char* name, bool v)   const { const XMLAttribute* a = FindAttribute(name); return a ? a->BoolValue(v) : v; }

XMLError XMLElement::QueryIntAttribute(const char* name, int* v)     const { const XMLAttribute* a = FindAttribute(name); return a ? a->QueryIntValue(v)   : XML_NO_ATTRIBUTE; }
XMLError XMLElement::QueryFloatAttribute(const char* name, float* v) const { const XMLAttribute* a = FindAttribute(name); return a ? a->QueryFloatValue(v) : XML_NO_ATTRIBUTE; }
XMLError XMLElement::QueryBoolAttribute(const char* name, bool* v)   const { const XMLAttribute* a = FindAttribute(name); return a ? a->QueryBoolValue(v)  : XML_NO_ATTRIBUTE; }

const char* XMLElement::GetText() const {
    for (XMLNode* n = m_firstChild; n; n = n->m_nextSibling) {
        XMLText* t = dynamic_cast<XMLText*>(n);
        if (t) return t->Value();
    }
    return nullptr;
}

XMLElement* XMLElement::FirstChildElement(const char* name) const {
    return XMLNode::FirstChildElement(name);
}
XMLElement* XMLElement::NextSiblingElement(const char* name) const {
    return XMLNode::NextSiblingElement(name);
}

// ── Recursive descent parser ─────────────────────────────────────────────────
struct XMLDocument::Parser {
    const char* p   = nullptr;
    const char* end = nullptr;
    XMLDocument* doc = nullptr;

    void SkipWhitespace() {
        while (p < end && std::isspace((unsigned char)*p)) ++p;
    }

    bool Match(const char* s) {
        size_t len = strlen(s);
        if (p + len > end) return false;
        if (strncmp(p, s, len) != 0) return false;
        p += len;
        return true;
    }

    // Skip <!-- ... -->
    bool SkipComment() {
        if (!Match("<!--")) return false;
        while (p + 2 < end && !(p[0] == '-' && p[1] == '-' && p[2] == '>')) ++p;
        if (p + 2 < end) p += 3;
        return true;
    }

    // Skip <?...?>
    bool SkipProcessingInstruction() {
        if (p >= end || *p != '<') return false;
        if (p + 1 >= end || *(p+1) != '?') return false;
        p += 2;
        while (p + 1 < end && !(p[0] == '?' && p[1] == '>')) ++p;
        if (p + 1 < end) p += 2;
        return true;
    }

    std::string ReadName() {
        const char* start = p;
        while (p < end && (std::isalnum((unsigned char)*p) || *p == '_' || *p == '-' || *p == '.' || *p == ':'))
            ++p;
        return std::string(start, p - start);
    }

    std::string ReadAttributeValue() {
        if (p >= end) return {};
        char quote = *p;
        if (quote != '"' && quote != '\'') return {};
        ++p;
        const char* start = p;
        while (p < end && *p != quote) ++p;
        std::string val(start, p - start);
        if (p < end) ++p; // skip closing quote
        return val;
    }

    // Returns nullptr on error; sets doc->m_error
    XMLElement* ParseElement(XMLNode* parent) {
        // We're positioned just after '<'
        SkipWhitespace();
        std::string name = ReadName();
        if (name.empty()) {
            doc->m_error    = XML_ERROR_PARSING_ELEMENT;
            doc->m_errorStr = "Expected element name";
            return nullptr;
        }
        auto* elem = new XMLElement(name.c_str());

        // Parse attributes
        while (p < end) {
            SkipWhitespace();
            if (p >= end) break;
            if (*p == '/' || *p == '>') break;
            std::string attrName = ReadName();
            if (attrName.empty()) break;
            SkipWhitespace();
            if (p < end && *p == '=') {
                ++p;
                SkipWhitespace();
                std::string val = ReadAttributeValue();
                elem->AddAttribute(attrName.c_str(), val.c_str());
            } else {
                // Boolean attribute (no value)
                elem->AddAttribute(attrName.c_str(), "true");
            }
        }

        if (p < end && *p == '/') {
            // Self-closing
            ++p;
            if (p < end && *p == '>') ++p;
            parent->LinkChild(elem);
            return elem;
        }
        if (p < end && *p == '>') ++p;

        // Parse children
        while (p < end) {
            SkipWhitespace();
            if (p >= end) break;
            if (*p == '<') {
                if (p + 1 < end && *(p+1) == '/') {
                    // End tag
                    p += 2;
                    std::string closeName = ReadName();
                    SkipWhitespace();
                    if (p < end && *p == '>') ++p;
                    break;
                } else if (p + 3 < end && strncmp(p, "<!--", 4) == 0) {
                    SkipComment();
                } else if (p + 1 < end && *(p+1) == '?') {
                    SkipProcessingInstruction();
                } else {
                    ++p; // skip '<'
                    XMLElement* child = ParseElement(elem);
                    if (!child && doc->m_error != XML_SUCCESS) {
                        delete elem;
                        return nullptr;
                    }
                }
            } else {
                // Text content
                const char* start = p;
                while (p < end && *p != '<') ++p;
                std::string text = TrimWhitespace(std::string(start, p - start));
                if (!text.empty()) {
                    auto* t = new XMLText(DecodeEntities(text).c_str());
                    elem->LinkChild(t);
                }
            }
        }

        parent->LinkChild(elem);
        return elem;
    }

    XMLError Run() {
        while (p < end) {
            SkipWhitespace();
            if (p >= end) break;
            if (*p == '<') {
                if (p + 3 < end && strncmp(p, "<!--", 4) == 0) {
                    SkipComment();
                } else if (p + 1 < end && *(p+1) == '?') {
                    SkipProcessingInstruction();
                } else {
                    ++p; // skip '<'
                    if (p < end && *p == '/') {
                        // Unexpected end tag at root — skip
                        while (p < end && *p != '>') ++p;
                        if (p < end) ++p;
                    } else {
                        ParseElement(doc);
                    }
                }
            } else {
                ++p; // skip stray text at root level
            }
            if (doc->m_error != XML_SUCCESS) return doc->m_error;
        }
        return XML_SUCCESS;
    }
};

// ── XMLDocument ──────────────────────────────────────────────────────────────
XMLDocument::XMLDocument()  = default;
XMLDocument::~XMLDocument() = default;

void XMLDocument::Clear() {
    XMLNode* child = m_firstChild;
    while (child) {
        XMLNode* next = child->m_nextSibling;
        delete child;
        child = next;
    }
    m_firstChild = m_lastChild = nullptr;
    m_error    = XML_SUCCESS;
    m_errorStr = {};
}

XMLError XMLDocument::Parse(const char* xml, size_t nBytes) {
    Clear();
    if (!xml || nBytes == 0) return XML_SUCCESS;
    size_t len = (nBytes == static_cast<size_t>(-1)) ? strlen(xml) : nBytes;
    Parser parser;
    parser.p   = xml;
    parser.end = xml + len;
    parser.doc = this;
    m_error = parser.Run();
    return m_error;
}

XMLError XMLDocument::LoadFile(const char* filename) {
    Clear();
    std::ifstream f(filename, std::ios::binary);
    if (!f) {
        m_error    = XML_ERROR_FILE_NOT_FOUND;
        m_errorStr = std::string("File not found: ") + filename;
        return m_error;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string content = ss.str();
    return Parse(content.c_str(), content.size());
}

XMLElement* XMLDocument::RootElement() const {
    return FirstChildElement();
}

XMLElement* XMLDocument::FirstChildElement(const char* name) const {
    return XMLNode::FirstChildElement(name);
}

} // namespace tinyxml2
