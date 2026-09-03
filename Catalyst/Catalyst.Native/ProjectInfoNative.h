#pragma once

// C API the launcher P/Invokes into. Reads a .CatalystProj file and fills the
// caller-supplied buffers with ProjectName / EngineVersion / StartupScene,
// using the exact same field-extraction logic (Lib/ProjectFields.h) that the
// engine's own ParseProjectFile uses — so the launcher's project list reflects
// what the engine would read, not a second parser.
//
// Returns 1 if the file was opened and read, 0 if it could not be opened (the
// output buffers are left untouched in that case).
extern "C" __declspec(dllexport)
int __cdecl ParseCatalystProjectInfo(
    const wchar_t* projectFilePath,
    wchar_t* outProjectName, int projectNameCapacity,
    wchar_t* outEngineVersion, int engineVersionCapacity,
    wchar_t* outStartupScene, int startupSceneCapacity
);
