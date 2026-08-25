#include "UIXMLStyleSheet.h"
#include "tinyxml2.h"
#include <fstream>
#include <sstream>
#include <windows.h>

namespace UIXML {

static const StyleClass s_emptyClass;

const StyleClass& StyleSheet::Find(const std::string& className) const {
    auto it = classes.find(className);
    return (it != classes.end()) ? it->second : s_emptyClass;
}

StyleSheet StyleSheet::ParseFromText(const std::string& text) {
    StyleSheet sheet;
    if (text.empty()) return sheet;

    tinyxml2::XMLDocument doc;
    doc.Parse(text.c_str(), text.size());
    if (doc.Error()) return sheet;

    auto* root = doc.RootElement();
    if (!root) return sheet;

    for (auto* style = root->FirstChildElement("Style"); style;
         style = style->NextSiblingElement("Style")) {
        const char* cls = style->Attribute("class");
        if (!cls) continue;
        StyleClass sc;
        for (auto* a = style->FirstAttribute(); a; a = a->Next()) {
            if (strcmp(a->Name(), "class") != 0)
                sc[a->Name()] = a->Value();
        }
        sheet.classes[cls] = std::move(sc);
    }
    return sheet;
}

StyleSheet StyleSheet::ParseFromFile(const std::wstring& path) {
    int needed = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string narrow(needed - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, narrow.data(), needed, nullptr, nullptr);

    std::ifstream f(narrow, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ParseFromText(ss.str());
}

} // namespace UIXML
