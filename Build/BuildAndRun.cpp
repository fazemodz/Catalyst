#include "BuildAndRun.h"

#include "../Blueprint/CookedUIBlueprint.h"
#include "../Launcher.h"
#include "../Resources/PakFile.h"

#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <vector>
#include <sstream>
#include <fstream>
#include <chrono>

namespace fs = std::filesystem;

namespace {
constexpr const wchar_t* kBuildManifestLogicalPath = L"build_manifest.json";

std::wstring QuoteArg(const std::wstring& arg) {
    if (arg.empty()) return L"\"\"";
    if (arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
    std::wstring escaped;
    escaped.reserve(arg.size() + 2);
    escaped.push_back(L'"');
    for (wchar_t ch : arg) {
        if (ch == L'"') escaped += L"\\\"";
        else escaped.push_back(ch);
    }
    escaped.push_back(L'"');
    return escaped;
}

std::wstring NarrowToWideLossy(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

bool CopyDirectoryRecursive(const fs::path& src, const fs::path& dst, std::wstring* outError) {
    std::error_code ec;
    if (!fs::exists(src, ec) || ec) {
        if (outError) *outError = L"Missing folder: " + src.wstring();
        return false;
    }
    fs::create_directories(dst, ec);
    if (ec) {
        if (outError) *outError = L"Failed to create folder: " + dst.wstring();
        return false;
    }
    for (fs::recursive_directory_iterator it(src, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end;
         it.increment(ec)) {
        const fs::path rel = fs::relative(it->path(), src, ec);
        if (ec) continue;
        const fs::path outPath = dst / rel;
        std::error_code entryEc;
        if (it->is_directory(entryEc) && !entryEc) {
            fs::create_directories(outPath, entryEc);
        } else if (it->is_regular_file(entryEc) && !entryEc) {
            fs::create_directories(outPath.parent_path(), entryEc);
            fs::copy_file(it->path(), outPath, fs::copy_options::overwrite_existing, entryEc);
        }
    }
    if (ec) {
        if (outError) *outError = L"Failed to copy folder: " + src.wstring();
        return false;
    }
    return true;
}

bool CopyFileIfExists(const fs::path& src, const fs::path& dst, std::wstring* outError) {
    std::error_code ec;
    if (!fs::exists(src, ec) || ec) {
        return true;
    }

    fs::create_directories(dst.parent_path(), ec);
    if (ec) {
        if (outError) *outError = L"Failed to create folder: " + dst.parent_path().wstring();
        return false;
    }

    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        if (outError) *outError = L"Failed to copy file: " + src.wstring();
        return false;
    }

    return true;
}

bool ReadFileBytes(const fs::path& path, std::vector<uint8_t>& outBytes, std::wstring* outError) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        if (outError) *outError = L"Failed to open file for pak: " + path.wstring();
        return false;
    }

    outBytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

bool AppendFileToPakEntries(const fs::path& stageRoot,
                            const fs::path& filePath,
                            std::vector<CatalystPak::EntryToWrite>& outEntries,
                            std::wstring* outError) {
    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(filePath, bytes, outError)) {
        return false;
    }

    std::error_code ec;
    fs::path relativePath = fs::relative(filePath, stageRoot, ec);
    if (ec || relativePath.empty()) {
        relativePath = filePath.filename();
    }

    CatalystPak::EntryToWrite entry;
    entry.logicalPath = relativePath.generic_wstring();
    entry.data = std::move(bytes);
    outEntries.push_back(std::move(entry));
    return true;
}

bool AppendFolderToPakEntries(const fs::path& stageRoot,
                              const fs::path& folderPath,
                              std::vector<CatalystPak::EntryToWrite>& outEntries,
                              std::wstring* outError) {
    std::error_code ec;
    if (!fs::exists(folderPath, ec) || ec) {
        return true;
    }

    for (fs::recursive_directory_iterator it(folderPath, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end;
         it.increment(ec)) {
        std::error_code entryEc;
        if (!it->is_regular_file(entryEc) || entryEc) {
            continue;
        }

        if (!AppendFileToPakEntries(stageRoot, it->path(), outEntries, outError)) {
            return false;
        }
    }

    if (ec) {
        if (outError) *outError = L"Failed to walk folder for pak: " + folderPath.wstring();
        return false;
    }

    return true;
}

bool AppendBuildManifestToPakEntries(const fs::path& projectFilePath,
                                     const fs::path& stageRoot,
                                     const fs::path& launchMapPath,
                                     std::vector<CatalystPak::EntryToWrite>& outEntries) {
    std::wstring projectName = fs::path(projectFilePath).stem().wstring();
    fs::path storedStartupMap = launchMapPath;
    std::error_code relEc;
    const fs::path relativeMapPath = fs::relative(launchMapPath, stageRoot, relEc);
    if (!relEc && !relativeMapPath.empty()) {
        storedStartupMap = relativeMapPath.lexically_normal();
    }

    std::wstringstream manifest;
    manifest << L"{\n";
    manifest << L"  \"ProjectName\": \"" << projectName << L"\",\n";
    manifest << L"  \"StartupScene\": \"" << storedStartupMap.generic_wstring() << L"\"\n";
    manifest << L"}\n";

    const std::wstring manifestWide = manifest.str();
    std::string manifestUtf8;
    manifestUtf8.reserve(manifestWide.size());
    for (wchar_t ch : manifestWide) {
        manifestUtf8.push_back(static_cast<char>(ch));
    }

    CatalystPak::EntryToWrite entry;
    entry.logicalPath = kBuildManifestLogicalPath;
    entry.data.assign(manifestUtf8.begin(), manifestUtf8.end());
    outEntries.push_back(std::move(entry));
    return true;
}

bool CookUIBlueprints(const fs::path& stagedAssetsRoot, std::wstring* outError) {
    std::error_code ec;
    if (!fs::exists(stagedAssetsRoot, ec) || ec) {
        return true;
    }

    for (fs::recursive_directory_iterator it(stagedAssetsRoot, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end;
         it.increment(ec)) {
        std::error_code entryEc;
        if (!it->is_regular_file(entryEc) || entryEc) {
            continue;
        }

        std::wstring extension = it->path().extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
        if (extension != L".catalystuiblueprint") {
            continue;
        }

        CookedUIBlueprint::Asset cookedAsset;
        std::string cookError;
        if (!CookedUIBlueprint::CompileFromJsonFile(it->path().wstring(), cookedAsset, &cookError)) {
            if (outError) {
                *outError = L"Failed to cook UI Blueprint: " + it->path().wstring() + L" (" + NarrowToWideLossy(cookError) + L")";
            }
            return false;
        }

        const std::wstring cookedPath = CookedUIBlueprint::GetCookedAssetPath(it->path().wstring());
        if (!CookedUIBlueprint::SaveToBinaryFile(cookedPath, cookedAsset, &cookError)) {
            if (outError) {
                *outError = L"Failed to write cooked UI Blueprint: " + cookedPath + L" (" + NarrowToWideLossy(cookError) + L")";
            }
            return false;
        }
    }

    if (ec) {
        if (outError) *outError = L"Failed to scan Assets for UI Blueprints.";
        return false;
    }

    return true;
}

bool BuildDataPak(const fs::path& stageRoot,
                  const fs::path& projectFilePath,
                  const fs::path& launchMapPath,
                  std::wstring* outError) {
    std::vector<CatalystPak::EntryToWrite> entries;
    if (!AppendFolderToPakEntries(stageRoot, stageRoot / L"Assets", entries, outError)) {
        return false;
    }
    AppendBuildManifestToPakEntries(projectFilePath, stageRoot, launchMapPath, entries);

    return CatalystPak::WritePakFile((stageRoot / L"data.pak").wstring(), entries, outError);
}

bool RunProcess(const std::wstring& exePath,
                const std::wstring& args,
                const std::wstring& workingDir,
                DWORD* outExitCode,
                std::wstring* outError) {
    std::wstring cmdLine = QuoteArg(exePath) + L" " + args;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back(L'\0');

    BOOL ok = CreateProcessW(
        nullptr,
        mutableCmd.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        workingDir.empty() ? nullptr : workingDir.c_str(),
        &si,
        &pi);

    if (!ok) {
        if (outError) {
            *outError = L"CreateProcess failed for: " + exePath;
        }
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (outExitCode) *outExitCode = exitCode;
    return true;
}

fs::path FindVsWhere() {
    const wchar_t* candidates[] = {
        L"C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe",
        L"C:\\Program Files\\Microsoft Visual Studio\\Installer\\vswhere.exe"
    };
    for (const wchar_t* candidate : candidates) {
        std::error_code ec;
        if (fs::exists(candidate, ec) && !ec) {
            return fs::path(candidate);
        }
    }
    return {};
}

// Minimal, pragmatic: try common MSBuild locations first. If not found, fall back to
// using vswhere to locate VS and then MSBuild under it.
fs::path FindMSBuild() {
    const wchar_t* candidates[] = {
        L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe",
        L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\MSBuild\\Current\\Bin\\MSBuild.exe",
        L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\MSBuild\\Current\\Bin\\MSBuild.exe",
    };
    for (const wchar_t* candidate : candidates) {
        std::error_code ec;
        if (fs::exists(candidate, ec) && !ec) return fs::path(candidate);
    }

    const fs::path vswhere = FindVsWhere();
    if (vswhere.empty()) return {};

    // Ask vswhere for the installation path; keep it simple by writing to a temp file.
    wchar_t tmpPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmpPath);
    fs::path outFile = fs::path(tmpPath) / L"catalyst_vswhere.txt";

    std::wstring args = L"-latest -products * -property installationPath -format value -nologo > " + QuoteArg(outFile.wstring());
    DWORD exitCode = 1;
    std::wstring err;
    // Use cmd.exe for redirection.
    RunProcess(L"cmd.exe", L"/c " + args, L"", &exitCode, &err);

    if (exitCode != 0) return {};

    std::wifstream in(outFile);
    std::wstring installPath;
    std::getline(in, installPath);
    in.close();

    std::error_code ec;
    fs::remove(outFile, ec);

    if (installPath.empty()) return {};

    fs::path msbuild = fs::path(installPath) / L"MSBuild" / L"Current" / L"Bin" / L"MSBuild.exe";
    if (fs::exists(msbuild, ec) && !ec) return msbuild;
    return {};
}

BuildRunResult BuildPlayerExecutable(const fs::path& repo,
                                     const fs::path& projectBuildRoot,
                                     const std::wstring& config) {
    BuildRunResult result;
    result.buildLogPath = (projectBuildRoot / L"msbuild.log").wstring();

    const fs::path playerExe = repo / L"x64" / config / L"CatalystPlayer.exe";
    std::error_code ec;
    if (fs::exists(playerExe, ec) && !ec) {
        result.ok = true;
        result.stagedExePath = playerExe.wstring();
        return result;
    }

    const fs::path msbuild = FindMSBuild();
    if (msbuild.empty()) {
        result.error = L"CatalystPlayer.exe was not found, and MSBuild.exe is not available.";
        return result;
    }

    const fs::path playerProject = repo / L"CatalystPlayer.vcxproj";
    std::wstringstream args;
    args << QuoteArg(playerProject.wstring())
         << L" /t:Build /m"
         << L" /p:Configuration=" << config
         << L" /p:Platform=x64"
         << L" /nologo /verbosity:minimal"
         << L" /fileLogger"
         << L" /fileLoggerParameters:LogFile=" << QuoteArg(result.buildLogPath) << L";Verbosity=diagnostic;Encoding=UTF-8";

    DWORD exitCode = 1;
    std::wstring err;
    if (!RunProcess(msbuild.wstring(), args.str(), repo.wstring(), &exitCode, &err) || exitCode != 0) {
        result.error = L"Player build failed (MSBuild exit code " + std::to_wstring(exitCode) +
                       L"). Check log: " + result.buildLogPath;
        if (!err.empty()) {
            result.error += L" " + err;
        }
        return result;
    }

    if (!fs::exists(playerExe, ec) || ec) {
        result.error = L"Built player EXE not found: " + playerExe.wstring();
        return result;
    }

    result.ok = true;
    result.stagedExePath = playerExe.wstring();
    return result;
}

std::wstring TimestampFolderName(const std::wstring& projectName) {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto secs = duration_cast<seconds>(now.time_since_epoch()).count();
    return projectName + L"_Build_" + std::to_wstring(secs);
}

} // namespace

BuildRunResult BuildStageAndRunPlayableExe(const std::wstring& repoRoot,
                                          const std::wstring& projectFilePath,
                                          const std::wstring& mapPath,
                                          bool buildRelease) {
    BuildRunResult result;

    if (repoRoot.empty()) {
        result.error = L"Repo root is empty.";
        return result;
    }
    if (projectFilePath.empty()) {
        result.error = L"No project file selected.";
        return result;
    }
    if (mapPath.empty()) {
        result.error = L"No map selected.";
        return result;
    }

    const fs::path repo(repoRoot);
    const fs::path projectRoot = fs::path(projectFilePath).parent_path();
    const std::wstring config = buildRelease ? L"Release" : L"Debug";
    const fs::path buildsRoot = projectRoot / L"builds";
    const fs::path projectBuildRoot = buildsRoot / config;
    const fs::path buildLogPath = projectBuildRoot / L"msbuild.log";
    result.buildLogPath = buildLogPath.wstring();

    std::error_code dirEc;
    fs::create_directories(projectBuildRoot, dirEc);
    if (dirEc) {
        result.error = L"Failed to create builds folder: " + projectBuildRoot.wstring();
        return result;
    }

    BuildRunResult playerBuild = BuildPlayerExecutable(repo, projectBuildRoot, config);
    if (!playerBuild.ok) {
        return playerBuild;
    }
    const fs::path sourceExe = playerBuild.stagedExePath;

    // Stage.
    std::error_code ec;
    if (!fs::exists(sourceExe, ec) || ec) {
        result.error = L"Built EXE not found: " + sourceExe.wstring();
        return result;
    }

    const fs::path projectName = fs::path(projectFilePath).stem();
    const fs::path stageRoot = projectBuildRoot / TimestampFolderName(projectName.wstring());
    const fs::path stageExe = stageRoot / (fs::path(projectFilePath).stem().wstring() + L".exe");
    fs::create_directories(stageRoot, ec);
    if (ec) {
        result.error = L"Failed to create staging folder: " + stageRoot.wstring();
        return result;
    }

    fs::copy_file(sourceExe, stageExe, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        result.error = L"Failed to copy EXE to staging folder.";
        return result;
    }

    // Copy runtime folders.
    std::wstring copyErr;
    if (!CopyDirectoryRecursive(repo / L"Shaders", stageRoot / L"Shaders", &copyErr)) {
        result.error = copyErr;
        return result;
    }
    if (!CopyFileIfExists(repo / L"Assets" / L"sky.hdr", stageRoot / L"sky.hdr", &copyErr)) {
        result.error = copyErr;
        return result;
    }

    fs::path projectGameDllPath = ResolveProjectGameDllPath(projectFilePath, buildRelease);
    if (!CopyFileIfExists(projectGameDllPath, stageRoot / L"GameLogic.dll", &copyErr)) {
        result.error = copyErr;
        return result;
    }
    if (!CopyFileIfExists(projectGameDllPath.replace_extension(L".pdb"), stageRoot / L"GameLogic.pdb", &copyErr)) {
        result.error = copyErr;
        return result;
    }

    // Copy project Assets/ into staged Assets/. (Project root = folder containing .CatalystProj)
    if (!CopyDirectoryRecursive(projectRoot / L"Assets", stageRoot / L"Assets", &copyErr)) {
        result.error = copyErr;
        return result;
    }

    if (!CookUIBlueprints(stageRoot / L"Assets", &copyErr)) {
        result.error = copyErr;
        return result;
    }

    fs::path stagedMapPath = mapPath;
    std::error_code relEc;
    const fs::path relativeMapPath = fs::relative(fs::path(mapPath), projectRoot, relEc);
    if (!relEc && !relativeMapPath.empty()) {
        stagedMapPath = (stageRoot / relativeMapPath).lexically_normal();
    }

    if (!BuildDataPak(stageRoot, projectFilePath, stagedMapPath, &copyErr)) {
        result.error = copyErr;
        return result;
    }

    const fs::path stagedProjectManifestPath = stageRoot / L"data.pak";

    // Launch staged EXE directly into play mode.
    {
        std::wstringstream args;
        args << L"--play"
             << L" --project " << QuoteArg(stagedProjectManifestPath.wstring())
             << L" --map " << QuoteArg(stagedMapPath.wstring());

        DWORD exitCode = 0;
        std::wstring err;
        // Launch without waiting.
        std::wstring cmdLine = QuoteArg(stageExe.wstring()) + L" " + args.str();
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
        mutableCmd.push_back(L'\0');
        BOOL ok = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, 0, nullptr, stageRoot.wstring().c_str(), &si, &pi);
        if (!ok) {
            result.error = L"Failed to launch staged EXE: " + stageExe.wstring();
            return result;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        (void)exitCode;
        (void)err;
    }

    result.ok = true;
    result.stagedFolder = stageRoot.wstring();
    result.stagedExePath = stageExe.wstring();
    return result;
}
