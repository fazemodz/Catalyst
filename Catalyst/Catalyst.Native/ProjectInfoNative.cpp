#include "pch.h"
#include "ProjectInfoNative.h"

#include "ProjectFields.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

// Copies `value` into `buffer`, truncating (not overflowing) if it doesn't
// fit, and always leaves the buffer null-terminated.
void CopyIntoBuffer(const std::wstring& value, wchar_t* buffer, int capacity)
{
    if (buffer == nullptr || capacity <= 0)
        return;

    wcsncpy_s(buffer, static_cast<size_t>(capacity), value.c_str(), _TRUNCATE);
}

} // namespace

int __cdecl ParseCatalystProjectInfo(
    const wchar_t* projectFilePath,
    wchar_t* outProjectName, int projectNameCapacity,
    wchar_t* outEngineVersion, int engineVersionCapacity,
    wchar_t* outStartupScene, int startupSceneCapacity)
{
    if (projectFilePath == nullptr)
        return 0;

    std::wifstream file(projectFilePath);
    if (!file.is_open())
        return 0;

    std::wstring content((std::istreambuf_iterator<wchar_t>(file)), std::istreambuf_iterator<wchar_t>());

    // Same defaults ParseProjectFile seeds before parsing: the filename stem
    // stands in for a missing ProjectName, "Unknown" for a missing version.
    std::wstring projectName = std::filesystem::path(projectFilePath).stem().wstring();
    std::wstring engineVersion = L"Unknown";
    std::wstring startupScene;

    ParseProjectFieldsFromJson(content, projectName, engineVersion, startupScene);

    CopyIntoBuffer(projectName, outProjectName, projectNameCapacity);
    CopyIntoBuffer(engineVersion, outEngineVersion, engineVersionCapacity);
    CopyIntoBuffer(startupScene, outStartupScene, startupSceneCapacity);

    return 1;
}
