#pragma once
#include <string>
#include <map>

namespace UIXML {

// A style class maps attribute names to string values,
// identical in semantics to inline XML attributes.
using StyleClass = std::map<std::string, std::string>;

struct StyleSheet {
    std::map<std::string, StyleClass> classes;

    // Returns an empty StyleClass (never throws) if name is not found.
    const StyleClass& Find(const std::string& className) const;

    // Parse from an .xmlstyle XML text:
    //   <StyleSheet>
    //     <Style class="btn" background="#2A2A2A" textColor="#FFF" />
    //   </StyleSheet>
    static StyleSheet ParseFromText(const std::string& text);
    static StyleSheet ParseFromFile(const std::wstring& path);

    bool Empty() const { return classes.empty(); }
};

} // namespace UIXML
