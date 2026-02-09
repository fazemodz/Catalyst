#include "Theme.h"

void UI::ApplyUnrealTheme() {
    ImGuiStyle& style = ImGui::GetStyle();

    // --- ROUNDING & PADDING ---
    style.WindowRounding = 6.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 6.0f;
    
    style.FramePadding = ImVec2(6, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.WindowPadding = ImVec2(10, 10);
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 12.0f;

    // --- UNREAL ENGINE 5 COLOR PALETTE ---
    ImVec4* colors = style.Colors;
    
    // Backgrounds (Dark Gray)
    colors[ImGuiCol_WindowBg]           = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_ChildBg]            = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_PopupBg]            = ImVec4(0.10f, 0.10f, 0.10f, 0.98f);
    colors[ImGuiCol_Border]             = ImVec4(0.00f, 0.00f, 0.00f, 0.30f); // Subtle borders
    colors[ImGuiCol_BorderShadow]       = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    
    // Headers (Lighter Gray)
    colors[ImGuiCol_Header]             = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_HeaderHovered]      = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_HeaderActive]       = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    
    // Titles (The strip at the top of windows)
    colors[ImGuiCol_TitleBg]            = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive]      = ImVec4(0.10f, 0.10f, 0.10f, 1.00f); // Keep uniform
    colors[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    
    // Buttons (Standard Gray -> Hover White-ish)
    colors[ImGuiCol_Button]             = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_ButtonHovered]      = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    colors[ImGuiCol_ButtonActive]       = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    
    // Tabs (Unreal Style)
    colors[ImGuiCol_Tab]                = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_TabHovered]         = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    colors[ImGuiCol_TabActive]          = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_TabUnfocused]       = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    
    // Text
    colors[ImGuiCol_Text]               = ImVec4(0.90f, 0.90f, 0.90f, 1.00f); // Off-white
    colors[ImGuiCol_TextDisabled]       = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    
    // Selection / Highlighting (Unreal Orange/Gold)
    // You can change this to Blue (0.0f, 0.45f, 0.8f) if you prefer Unity style
    ImVec4 accentColor = ImVec4(0.92f, 0.55f, 0.15f, 1.00f); 
    
    colors[ImGuiCol_FrameBg]            = ImVec4(0.08f, 0.08f, 0.08f, 1.00f); // Input fields
    colors[ImGuiCol_FrameBgHovered]     = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_FrameBgActive]      = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    
    colors[ImGuiCol_SliderGrab]         = accentColor;
    colors[ImGuiCol_SliderGrabActive]   = ImVec4(accentColor.x * 1.1f, accentColor.y * 1.1f, accentColor.z * 1.1f, 1.0f);
    colors[ImGuiCol_CheckMark]          = accentColor;
    colors[ImGuiCol_ResizeGrip]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f); // Hide resizing grip
    colors[ImGuiCol_ResizeGripHovered]  = accentColor;
    colors[ImGuiCol_ResizeGripActive]   = accentColor;
    colors[ImGuiCol_Separator]          = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
}