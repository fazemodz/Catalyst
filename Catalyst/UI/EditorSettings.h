#pragma once

#include <DirectXMath.h>
#include <string>

// Per-machine editor preferences. Saved next to the executable so they follow
// the install rather than whichever project happens to be open.
struct EditorPreferences {
    float cameraMoveSpeed    = 6.0f;
    float cameraTurnSpeed    = 0.0025f;
    float viewportFov        = 45.0f;

    float rightPanelWidth    = 350.0f;
    float outlinerSplit      = 0.45f;  // fraction of the right column given to the outliner
    float bottomPanelHeight  = 250.0f;

    bool  buildRelease       = false;

    // Viewport overlays.
    bool  showFpsCounter      = true;
    bool  showRaytracingStats = false;

    void ClampToValidRanges();
};

// Settings that belong to the project and travel with it inside the
// .CatalystProj file.
struct ProjectSettings {
    float exposure        = 1.0f;
    DirectX::XMFLOAT3 colorTint = {1.0f, 1.0f, 1.0f};
    float bloomThreshold  = 1.0f;
    float bloomIntensity  = 0.5f;

    DirectX::XMFLOAT3 gravity = {0.0f, -9.81f, 0.0f};
    float fixedTimeStep   = 1.0f / 60.0f;

    bool  raytraceEnabled             = true;   // master switch
    bool  raytraceShadowsEnabled      = true;
    bool  raytraceReflectionsEnabled  = true;
    bool  raytraceAoEnabled           = true;
    float raytraceShadowStrength      = 0.75f;  // 0 = no darkening, 1 = fully black shadows
    float raytraceReflectionIntensity = 0.35f;
    float raytraceReflectionDistance  = 250.0f;
    float raytraceSurfaceBias         = 0.02f;
    float raytraceAoStrength          = 0.65f;
    float raytraceAoRadius            = 2.5f;
    float raytraceAoSamples           = 8.0f;
    // Angular radius of the light as seen from a surface. 0 gives razor-sharp
    // shadows; larger values widen the penumbra.
    float raytraceShadowSoftness      = 0.035f;
    float raytraceShadowSamples       = 8.0f;

    void ClampToValidRanges();
};

namespace EditorSettingsIO {
// Resolves <exe folder>/editor_settings.json.
std::wstring PreferencesPath();

// Missing or unreadable files leave `outPreferences` at its defaults and
// return false, which is not an error on first run.
bool LoadPreferences(EditorPreferences& outPreferences);
bool SavePreferences(const EditorPreferences& preferences);
}
