#include "EditorSettings.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <windows.h>

#include "Json.h"

namespace fs = std::filesystem;

namespace {
float Clamp(float value, float low, float high) {
    // Rejects NaN as well: a NaN that reached a viewport size or a physics step
    // would poison every frame after it.
    if (!(value >= low)) return low;
    if (!(value <= high)) return high;
    return value;
}

DirectX::XMFLOAT3 ReadFloat3(const Json::Value& object, const char* key, const DirectX::XMFLOAT3& fallback) {
    return Json::GetFloat3(object, key, fallback);
}

void WriteFloat(std::ostream& out, const char* key, float value, bool trailingComma) {
    out << "    \"" << key << "\": " << value << (trailingComma ? ",\n" : "\n");
}
}

void EditorPreferences::ClampToValidRanges() {
    cameraMoveSpeed   = Clamp(cameraMoveSpeed,   0.1f,   200.0f);
    cameraTurnSpeed   = Clamp(cameraTurnSpeed,   0.0001f, 0.05f);
    viewportFov       = Clamp(viewportFov,       10.0f,  140.0f);
    rightPanelWidth   = Clamp(rightPanelWidth,   180.0f, 900.0f);
    outlinerSplit     = Clamp(outlinerSplit,     0.1f,   0.9f);
    bottomPanelHeight = Clamp(bottomPanelHeight, 80.0f,  900.0f);
}

void ProjectSettings::ClampToValidRanges() {
    exposure       = Clamp(exposure,       0.0f, 16.0f);
    colorTint.x    = Clamp(colorTint.x,    0.0f, 4.0f);
    colorTint.y    = Clamp(colorTint.y,    0.0f, 4.0f);
    colorTint.z    = Clamp(colorTint.z,    0.0f, 4.0f);
    bloomThreshold = Clamp(bloomThreshold, 0.0f, 16.0f);
    bloomIntensity = Clamp(bloomIntensity, 0.0f, 8.0f);
    gravity.x      = Clamp(gravity.x,     -200.0f, 200.0f);
    gravity.y      = Clamp(gravity.y,     -200.0f, 200.0f);
    gravity.z      = Clamp(gravity.z,     -200.0f, 200.0f);
    // A zero or negative step would spin the fixed-step loop forever.
    fixedTimeStep  = Clamp(fixedTimeStep,  1.0f / 480.0f, 1.0f / 15.0f);
    raytraceShadowStrength      = Clamp(raytraceShadowStrength,      0.0f,   1.0f);
    raytraceReflectionIntensity = Clamp(raytraceReflectionIntensity, 0.0f,   2.0f);
    raytraceReflectionDistance  = Clamp(raytraceReflectionDistance,  1.0f,   5000.0f);
    raytraceSurfaceBias         = Clamp(raytraceSurfaceBias,         0.0001f, 1.0f);
    raytraceAoStrength          = Clamp(raytraceAoStrength,          0.0f,   1.0f);
    raytraceAoRadius            = Clamp(raytraceAoRadius,            0.05f,  200.0f);
    raytraceAoSamples           = Clamp(raytraceAoSamples,           1.0f,   32.0f);
    raytraceShadowSoftness      = Clamp(raytraceShadowSoftness,      0.0f,   0.5f);
    raytraceShadowSamples       = Clamp(raytraceShadowSamples,       1.0f,   32.0f);
}

namespace EditorSettingsIO {

std::wstring PreferencesPath() {
    wchar_t modulePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L"editor_settings.json";
    }
    return (fs::path(modulePath).parent_path() / L"editor_settings.json").wstring();
}

bool LoadPreferences(EditorPreferences& outPreferences) {
    const std::wstring path = PreferencesPath();

    std::ifstream file(fs::path(path), std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Json::Parser parser(content);
    Json::Value root;
    if (!parser.Parse(root) || root.type != Json::ValueType::Object) {
        return false;
    }

    const EditorPreferences defaults;
    outPreferences.cameraMoveSpeed   = Json::GetNumber(root, "CameraMoveSpeed",   defaults.cameraMoveSpeed);
    outPreferences.cameraTurnSpeed   = Json::GetNumber(root, "CameraTurnSpeed",   defaults.cameraTurnSpeed);
    outPreferences.viewportFov       = Json::GetNumber(root, "ViewportFov",       defaults.viewportFov);
    outPreferences.rightPanelWidth   = Json::GetNumber(root, "RightPanelWidth",   defaults.rightPanelWidth);
    outPreferences.outlinerSplit     = Json::GetNumber(root, "OutlinerSplit",     defaults.outlinerSplit);
    outPreferences.bottomPanelHeight = Json::GetNumber(root, "BottomPanelHeight", defaults.bottomPanelHeight);
    outPreferences.buildRelease      = Json::GetBool(root,   "BuildRelease",      defaults.buildRelease);
    outPreferences.showFpsCounter      = Json::GetBool(root, "ShowFpsCounter",      defaults.showFpsCounter);
    outPreferences.showRaytracingStats = Json::GetBool(root, "ShowRaytracingStats", defaults.showRaytracingStats);
    outPreferences.ClampToValidRanges();
    return true;
}

bool SavePreferences(const EditorPreferences& preferences) {
    EditorPreferences clamped = preferences;
    clamped.ClampToValidRanges();

    const std::wstring path = PreferencesPath();
    std::ofstream file(fs::path(path), std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file << "{\n";
    WriteFloat(file, "CameraMoveSpeed",   clamped.cameraMoveSpeed,   true);
    WriteFloat(file, "CameraTurnSpeed",   clamped.cameraTurnSpeed,   true);
    WriteFloat(file, "ViewportFov",       clamped.viewportFov,       true);
    WriteFloat(file, "RightPanelWidth",   clamped.rightPanelWidth,   true);
    WriteFloat(file, "OutlinerSplit",     clamped.outlinerSplit,     true);
    WriteFloat(file, "BottomPanelHeight", clamped.bottomPanelHeight, true);
    file << "    \"BuildRelease\": " << (clamped.buildRelease ? "true" : "false") << ",\n";
    file << "    \"ShowFpsCounter\": " << (clamped.showFpsCounter ? "true" : "false") << ",\n";
    file << "    \"ShowRaytracingStats\": " << (clamped.showRaytracingStats ? "true" : "false") << "\n";
    file << "}\n";
    return file.good();
}

}
