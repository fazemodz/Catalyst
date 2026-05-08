#pragma once

#include "CatalystAPI.h"

#include <string>
#include <vector>
#include <windows.h>

class ScriptModuleHost {
public:
    enum class Status {
        Missing,
        Loaded,
        Stale,
        ReloadFailed
    };

    ScriptModuleHost() = default;
    ~ScriptModuleHost();

    void SetHostApi(const Catalyst::ScriptHostApi& hostApi);
    void SetProject(const std::wstring& projectFilePath);
    void ClearProject();
    void Shutdown();

    void PollSourceChanges(bool allowAutomaticReload);
    bool PerformPendingReload(std::wstring* outError = nullptr);
    bool EnsureModuleLoaded(std::wstring* outError = nullptr);

    bool HasPendingReload() const { return m_hasPendingReload; }
    bool IsLoaded() const { return m_moduleHandle != nullptr && m_moduleApi != nullptr; }
    bool HasClass(const std::string& className) const;
    Catalyst::NativeScript* CreateScript(const std::string& className) const;
    void DestroyScript(Catalyst::NativeScript* script) const;

    Status GetStatus() const { return m_status; }
    const std::wstring& GetLastError() const { return m_lastError; }
    const std::vector<std::string>& GetAvailableClasses() const { return m_availableClasses; }
    const std::wstring& GetSourceDllPath() const { return m_sourceDllPath; }
    std::wstring GetStatusLabel() const;

private:
    bool ResolveSourceDllPath(std::wstring& outPath) const;
    bool LoadModuleFromPath(const std::wstring& sourceDllPath, std::wstring* outError);
    void UnloadCurrentModule();
    void RefreshAvailableClasses();
    std::wstring BuildShadowCopyPath(const std::wstring& sourceDllPath);
    static std::wstring NormalizePath(const std::wstring& path);

    Catalyst::ScriptHostApi m_hostApi{};
    std::wstring m_projectFilePath;
    std::wstring m_sourceDllPath;
    std::wstring m_loadedShadowDllPath;
    std::wstring m_loadedShadowPdbPath;
    HMODULE m_moduleHandle = nullptr;
    const Catalyst::ScriptModuleApi* m_moduleApi = nullptr;
    Status m_status = Status::Missing;
    std::wstring m_lastError;
    std::vector<std::string> m_availableClasses;
    bool m_hasPendingReload = false;
    ULONGLONG m_pendingReloadReadyTick = 0;
    long long m_loadedWriteTimeTicks = 0;
    long long m_pendingWriteTimeTicks = 0;
    uint64_t m_shadowCopyCounter = 0;
};
