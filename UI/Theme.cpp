#include "Theme.h"

void UI::ApplyUnrealTheme() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding    = 4.0f;
    style.ChildRounding     = 4.0f;
    style.FrameRounding     = 3.0f;
    style.PopupRounding     = 3.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 4.0f;

    style.FramePadding  = ImVec2(6, 4);
    style.ItemSpacing   = ImVec2(8, 5);
    style.WindowPadding = ImVec2(10, 10);
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize   = 10.0f;

    ImVec4* c = style.Colors;
    const ImVec4 accent  = ImVec4(0.878f, 0.439f, 0.126f, 1.0f); // #E07020
    const ImVec4 accentH = ImVec4(1.000f, 0.565f, 0.200f, 1.0f); // brighter hover

    // Backgrounds
    c[ImGuiCol_WindowBg]           = ImVec4(0.149f, 0.149f, 0.149f, 1.0f); // #262626
    c[ImGuiCol_ChildBg]            = ImVec4(0.165f, 0.165f, 0.165f, 1.0f); // #2A2A2A
    c[ImGuiCol_PopupBg]            = ImVec4(0.122f, 0.122f, 0.122f, 0.98f); // #1F1F1F
    c[ImGuiCol_Border]             = ImVec4(0.227f, 0.227f, 0.227f, 1.0f); // #3A3A3A
    c[ImGuiCol_BorderShadow]       = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    // Headers
    c[ImGuiCol_Header]             = ImVec4(0.220f, 0.220f, 0.220f, 1.0f);
    c[ImGuiCol_HeaderHovered]      = ImVec4(0.282f, 0.282f, 0.282f, 1.0f);
    c[ImGuiCol_HeaderActive]       = ImVec4(0.333f, 0.333f, 0.333f, 1.0f);

    // Titles
    c[ImGuiCol_TitleBg]            = ImVec4(0.110f, 0.110f, 0.110f, 1.0f); // #1C1C1C
    c[ImGuiCol_TitleBgActive]      = ImVec4(0.110f, 0.110f, 0.110f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.110f, 0.110f, 0.110f, 1.0f);

    // Buttons
    c[ImGuiCol_Button]             = ImVec4(0.220f, 0.220f, 0.220f, 1.0f); // #383838
    c[ImGuiCol_ButtonHovered]      = ImVec4(0.282f, 0.282f, 0.282f, 1.0f); // #484848
    c[ImGuiCol_ButtonActive]       = accent;

    // Tabs
    c[ImGuiCol_Tab]                = ImVec4(0.149f, 0.149f, 0.149f, 1.0f);
    c[ImGuiCol_TabHovered]         = ImVec4(0.247f, 0.247f, 0.247f, 1.0f);
    c[ImGuiCol_TabActive]          = ImVec4(0.196f, 0.196f, 0.196f, 1.0f);
    c[ImGuiCol_TabUnfocused]       = ImVec4(0.149f, 0.149f, 0.149f, 1.0f);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.196f, 0.196f, 0.196f, 1.0f);

    // Text
    c[ImGuiCol_Text]               = ImVec4(0.910f, 0.910f, 0.910f, 1.0f); // #E8E8E8
    c[ImGuiCol_TextDisabled]       = ImVec4(0.400f, 0.400f, 0.400f, 1.0f);

    // Frames
    c[ImGuiCol_FrameBg]            = ImVec4(0.118f, 0.118f, 0.118f, 1.0f); // #1E1E1E
    c[ImGuiCol_FrameBgHovered]     = ImVec4(0.165f, 0.165f, 0.165f, 1.0f);
    c[ImGuiCol_FrameBgActive]      = ImVec4(0.200f, 0.200f, 0.200f, 1.0f);

    // Accent-colored controls
    c[ImGuiCol_SliderGrab]         = accent;
    c[ImGuiCol_SliderGrabActive]   = accentH;
    c[ImGuiCol_CheckMark]          = accent;
    c[ImGuiCol_ResizeGrip]         = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ResizeGripHovered]  = accent;
    c[ImGuiCol_ResizeGripActive]   = accentH;
    c[ImGuiCol_Separator]          = ImVec4(0.227f, 0.227f, 0.227f, 1.0f);
    c[ImGuiCol_SeparatorHovered]   = accent;
    c[ImGuiCol_SeparatorActive]    = accentH;

    // Scrollbar
    c[ImGuiCol_ScrollbarBg]        = ImVec4(0.110f, 0.110f, 0.110f, 1.0f);
    c[ImGuiCol_ScrollbarGrab]      = ImVec4(0.270f, 0.270f, 0.270f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.350f, 0.350f, 0.350f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.420f, 0.420f, 0.420f, 1.0f);
}
