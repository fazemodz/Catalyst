#pragma once

#include <string>

struct BuildRunResult {
    bool ok = false;
    std::wstring stagedFolder;
    std::wstring stagedExePath;
    std::wstring buildLogPath;
    std::wstring error;
};

BuildRunResult BuildStageAndRunPlayableExe(const std::wstring& repoRoot,
                                          const std::wstring& projectFilePath,
                                          const std::wstring& mapPath,
                                          bool buildRelease);
