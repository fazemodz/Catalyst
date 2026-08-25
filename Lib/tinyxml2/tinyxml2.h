#pragma once
// Minimal XML parser — TinyXML2-compatible subset (MIT)
// Supports: nested elements, attributes, text content, comments, entities,
// self-closing tags, XML declarations. No namespaces, no CDATA, no DTD.
#include <string>
#include <vector>
#include <cstring>

namespace tinyxml2 {

class XMLDocument;
class XMLElement;

// ── Error codes ──────────────────────────────────────────────────────────────
enum XMLError {
    XML_SUCCESS              = 0,
    XML_NO_ATTRIBUTE,
    XML_WRONG_ATTRIBUTE_TYPE,
    XML_ERROR_FILE_NOT_FOUND,
    XML_ERROR_FILE_COULD_NOT_BE_OPENED,
    XML_ERROR_PARSING_ELEMENT,
    XML_ERROR_PARSING_ATTRIBUTE,
    XML_ERROR_PARSING,
    XML_CAN_NOT_CONVERT_TEXT,
    XML_NO_TEXT_NODE,
    XML_ELEMENT_DEPTH_EXCEEDED,
};

// ── XMLAttribute ─────────────────────────────────────────────────────────────
class XMLAttribute {
    friend class XMLElement;
public:
    const char* Name()  const { return m_name.c_str(); }
    const char* Value() const { return m_value.c_str(); }

    int   IntValue(int   defaultVal = 0)    const;
    float FloatValue(float defaultVal = 0)  const;
    bool  BoolValue(bool  defaultVal = false) const;

    XMLError QueryIntValue(int* v)     const;
    XMLError QueryFloatValue(float* v) const;
    XMLError QueryBoolValue(bool* v)   const;

    const XMLAttribute* Next() const { return m_next; }

private:
    std::string   m_name;
    std::string   m_value;
    XMLAttribute* m_next = nullptr;
};

// ── XMLNode (base) ───────────────────────────────────────────────────────────
class XMLNode {
    friend class XMLDocument;
    friend class XMLElement;
public:
    virtual ~XMLNode();
    virtual XMLElement* ToElement() { return nullptr; }

    XMLNode* Parent()          const { return m_parent; }
    XMLNode* FirstChild()      const { return m_firstChild; }
    XMLNode* LastChild()       const { return m_lastChild; }
    XMLNode* NextSibling()     const { return m_nextSibling; }
    XMLNode* PreviousSibling() const { return m_prevSibling; }

    XMLElement* FirstChildElement(const char* name = nullptr) const;
    XMLElement* NextSiblingElement(const char* name = nullptr) const;

    const char* Value() const { return m_value.c_str(); }

protected:
    XMLNode();
    void LinkChild(XMLNode* child);
    void Unlink(XMLNode* child);

    XMLNode* m_parent      = nullptr;
    XMLNode* m_firstChild  = nullptr;
    XMLNode* m_lastChild   = nullptr;
    XMLNode* m_nextSibling = nullptr;
    XMLNode* m_prevSibling = nullptr;
    std::string m_value;
};

// ── XMLElement ───────────────────────────────────────────────────────────────
class XMLElement : public XMLNode {
    friend class XMLDocument;
public:
    ~XMLElement() override;
    XMLElement* ToElement() override { return this; }

    const char* Name() const { return m_value.c_str(); }

    // Attribute access
    const XMLAttribute* FindAttribute(const char* name) const;
    const char*         Attribute(const char* name, const char* defaultValue = nullptr) const;
    int                 IntAttribute(const char* name, int   defaultVal = 0)    const;
    float               FloatAttribute(const char* name, float defaultVal = 0)  const;
    bool                BoolAttribute(const char* name, bool  defaultVal = false) const;

    XMLError QueryIntAttribute(const char* name, int* v)     const;
    XMLError QueryFloatAttribute(const char* name, float* v) const;
    XMLError QueryBoolAttribute(const char* name, bool* v)   const;

    const XMLAttribute* FirstAttribute() const { return m_firstAttr; }

    // Text content of the first text child
    const char* GetText() const;

    // Named child / sibling (overrides base to return XMLElement*)
    XMLElement* FirstChildElement(const char* name = nullptr) const;
    XMLElement* NextSiblingElement(const char* name = nullptr) const;

private:
    XMLElement(const char* name);
    void AddAttribute(const char* name, const char* value);

    XMLAttribute* m_firstAttr = nullptr;
    XMLAttribute* m_lastAttr  = nullptr;
};

// ── XMLText ──────────────────────────────────────────────────────────────────
class XMLText : public XMLNode {
    friend class XMLDocument;
public:
    const char* Value() const { return m_value.c_str(); }
private:
    explicit XMLText(const char* text) { m_value = text; }
};

// ── XMLDocument ──────────────────────────────────────────────────────────────
class XMLDocument : public XMLNode {
public:
    XMLDocument();
    ~XMLDocument() override;

    XMLError Parse(const char* xml, size_t nBytes = static_cast<size_t>(-1));
    XMLError LoadFile(const char* filename);

    XMLElement* RootElement() const;
    XMLElement* FirstChildElement(const char* name = nullptr) const;

    bool Error() const { return m_error != XML_SUCCESS; }
    XMLError ErrorID() const { return m_error; }
    const char* ErrorStr() const { return m_errorStr.c_str(); }

private:
    struct Parser;
    void Clear();

    XMLError    m_error = XML_SUCCESS;
    std::string m_errorStr;
};

} // namespace tinyxml2
