#include "BlueprintNodeLibrary.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>

namespace fs = std::filesystem;

namespace BlueprintNodes {
namespace {
const std::array<BlueprintNodeTemplate, 6> kGameplayNodeTemplates = {{
    {kPlayerCharacterControllerNodeId, "Player Character", "Makes the owning object controllable with WASD", 270.0f, 156.0f},
    {kPlayerSpaceJumpNodeId, "Jump On Space", "Makes the Player Character jump when Space is pressed", 270.0f, 156.0f},
    {kCreateWidgetNodeId, "Create Widget", "Creates a UI Blueprint widget that can be added to the viewport", 282.0f, 164.0f},
    {kAddToViewportNodeId, "Add To Viewport", "Adds a created widget to the active viewport overlay", 278.0f, 160.0f},
    {kSwapWidgetNodeId, "Swap Widget", "Replaces the current widget with another UI Blueprint", 278.0f, 160.0f},
    {kOpenLevelNodeId, "Open Level", "Loads a different map during play", 268.0f, 156.0f}
}};

std::string EscapeRegex(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() * 2);

    for (char ch : value) {
        switch (ch) {
        case '\\':
        case '^':
        case '$':
        case '.':
        case '|':
        case '?':
        case '*':
        case '+':
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '}':
            escaped.push_back('\\');
            escaped.push_back(ch);
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }

    return escaped;
}

bool ReadTextFile(const std::wstring& assetPath, std::string& outContent) {
    if (assetPath.empty()) {
        return false;
    }

    std::ifstream inputFile(fs::path(assetPath), std::ios::binary);
    if (!inputFile.is_open()) {
        return false;
    }

    outContent.assign(std::istreambuf_iterator<char>(inputFile), std::istreambuf_iterator<char>());
    return true;
}
}

std::span<const BlueprintNodeTemplate> GetGameplayNodeTemplates() {
    return kGameplayNodeTemplates;
}

const BlueprintNodeTemplate* FindBlueprintNodeTemplate(const std::string& typeId) {
    for (const BlueprintNodeTemplate& nodeTemplate : kGameplayNodeTemplates) {
        if (typeId == nodeTemplate.typeId) {
            return &nodeTemplate;
        }
    }
    return nullptr;
}

bool LoadBlueprintRuntimeBinding(const std::wstring& assetPath, BlueprintRuntimeBinding& outBinding) {
    outBinding = {};

    std::string content;
    if (!ReadTextFile(assetPath, content)) {
        return false;
    }

    const std::regex playerControllerPattern(
        std::string("\"nodeTypeId\"\\s*:\\s*\"") + EscapeRegex(kPlayerCharacterControllerNodeId) + "\"",
        std::regex_constants::icase);
    const std::regex playerJumpPattern(
        std::string("\"nodeTypeId\"\\s*:\\s*\"") + EscapeRegex(kPlayerSpaceJumpNodeId) + "\"",
        std::regex_constants::icase);
    outBinding.playerCharacterController = std::regex_search(content, playerControllerPattern);
    outBinding.playerSpaceJump = std::regex_search(content, playerJumpPattern);
    return true;
}
}
