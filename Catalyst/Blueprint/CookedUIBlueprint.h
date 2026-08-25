#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace CookedUIBlueprint {
enum class ElementType : uint8_t {
    Canvas = 1,
    Button = 2,
    Image = 3,
    TextBlock = 4
};

enum class Opcode : uint8_t {
    SetTextColor = 1,
    Delay = 2,
    Print = 3,
    OpenLevel = 4,
    SwapWidget = 5
};

enum class EventType : uint8_t {
    Construct = 1,
    ButtonPressed = 2
};

struct Element {
    int id = 0;
    ElementType type = ElementType::Canvas;
    std::string displayText;
    float canvasX = 0.0f;
    float canvasY = 0.0f;
    float canvasWidth = 0.0f;
    float canvasHeight = 0.0f;
    uint32_t tintColor = 0xFFFFFFFF;
    bool visibleInGame = true;
};

struct Instruction {
    Opcode opcode = Opcode::SetTextColor;
    int targetNodeId = 0;
    uint32_t color = 0xFFFFFFFF;
    float durationSeconds = 0.0f;
    std::string text;
};

struct EventHandler {
    EventType type = EventType::Construct;
    int sourceNodeId = 0;
    uint32_t firstInstructionIndex = 0;
    uint32_t instructionCount = 0;
};

struct Asset {
    std::wstring sourceAssetPath;
    std::vector<Element> elements;
    std::vector<Instruction> instructions;
    std::vector<EventHandler> events;
};

bool IsUIBlueprintPath(const std::wstring& assetPath);
std::wstring GetCookedAssetPath(const std::wstring& sourceAssetPath);

bool CompileFromJsonFile(const std::wstring& sourceAssetPath, Asset& outAsset, std::string* outError = nullptr);
bool CompileFromXmlFile(const std::wstring& sourceAssetPath,  Asset& outAsset, std::string* outError = nullptr);
bool SaveToBinaryFile(const std::wstring& cookedAssetPath, const Asset& asset, std::string* outError = nullptr);
bool LoadFromBinaryFile(const std::wstring& cookedAssetPath, Asset& outAsset, std::string* outError = nullptr);
bool LoadFromBinaryBuffer(const std::vector<uint8_t>& bytes, Asset& outAsset, std::string* outError = nullptr);
}
