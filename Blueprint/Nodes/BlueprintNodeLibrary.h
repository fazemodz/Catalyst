#pragma once

#include <span>
#include <string>

namespace BlueprintNodes {
inline constexpr const char* kPlayerCharacterControllerNodeId = "Gameplay.PlayerCharacterController";
inline constexpr const char* kPlayerSpaceJumpNodeId = "Gameplay.PlayerSpaceJump";
inline constexpr const char* kCreateWidgetNodeId = "UI.CreateWidget";
inline constexpr const char* kAddToViewportNodeId = "UI.AddToViewport";
inline constexpr const char* kWidgetConstructNodeId = "UI.EventConstruct";
inline constexpr const char* kSetTextColorNodeId = "UI.SetTextColor";
inline constexpr const char* kCanvasElementNodeId = "UIElement.Canvas";
inline constexpr const char* kButtonElementNodeId = "UIElement.Button";
inline constexpr const char* kImageElementNodeId = "UIElement.Image";
inline constexpr const char* kTextBlockElementNodeId = "UIElement.TextBlock";

struct BlueprintNodeTemplate {
    const char* typeId = "";
    const char* title = "";
    const char* subtitle = "";
    float width = 250.0f;
    float height = 150.0f;
};

struct BlueprintRuntimeBinding {
    bool playerCharacterController = false;
    bool playerSpaceJump = false;
    float playerMoveSpeed = 6.0f;
    float playerJumpImpulse = 5.0f;
};

std::span<const BlueprintNodeTemplate> GetGameplayNodeTemplates();
const BlueprintNodeTemplate* FindBlueprintNodeTemplate(const std::string& typeId);
bool LoadBlueprintRuntimeBinding(const std::wstring& assetPath, BlueprintRuntimeBinding& outBinding);
}
