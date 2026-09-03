#pragma once

#include <string>

// Extracts ProjectName / EngineVersion / StartupScene out of a .CatalystProj
// JSON document. Each output parameter keeps its incoming value when the
// field is absent from the JSON, so callers should pre-seed defaults before
// calling. Shared between the engine (Launcher.cpp's ParseProjectFile) and
// Catalyst.Native.dll, which the launcher P/Invokes into so its project list
// reflects the same parsing the engine itself does rather than a second,
// drifting implementation.
void ParseProjectFieldsFromJson(
    const std::wstring& jsonContent,
    std::wstring& projectName,
    std::wstring& engineVersion,
    std::wstring& startupScene);
