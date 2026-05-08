#include "ScriptModuleHost.h"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace {
constexpr wchar_t kGameDllFileName[] = L"GameLogic.dll";
constexpr wchar_t kModuleExportName[] = L"GetCatalystScriptModuleApi";

std::wstring CopyAnsiToWide(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}
}

ScriptModuleHost::~ScriptModuleHost() {
    Shutdown();
}

void ScriptModuleHost::SetHostApi(const Catalyst::ScriptHostApi& hostApi) {
    m_hostApi = hostApi;
    if (m_moduleApi != nullptr && m_moduleApi->installHostApi != nullptr) {
        m_moduleApi->installHostApi(&m_hostApi);
    }
}

void ScriptModuleHost::SetProject(const std::wstring& projectFilePath) {
    const std::wstring normalizedProjectPath = NormalizePath(projectFilePath);
    if (normalizedProjectPath == m_projectFilePath) {
        return;
    }

    ClearProject();
    m_projectFilePath = normalizedProjectPath;
    (void)ResolveSourceDllPath(m_sourceDllPath);
}

void ScriptModuleHost::ClearProject() {
    Shutdown();
    m_projectFilePath.clear();
    m_sourceDllPath.clear();
    m_loadedWriteTimeTicks = 0;
    m_pendingWriteTimeTicks = 0;
}

void ScriptModuleHost::Shutdown() {
    m_hasPendingReload = false;
    m_pendingReloadReadyTick = 0;
    m_lastError.clear();
    m_availableClasses.clear();
    UnloadCurrentModule();
    if (m_sourceDllPath.empty()) {
        m_status = Status::Missing;
    }
}

bool ScriptModuleHost::EnsureModuleLoaded(std::wstring* outError) {
    if (IsLoaded()) {
        return true;
    }

    if (m_sourceDllPath.empty()) {
        (void)ResolveSourceDllPath(m_sourceDllPath);
    }

    if (!m_sourceDllPath.empty()) {
        m_hasPendingReload = true;
        m_pendingReloadReadyTick = 0;
        m_pendingWriteTimeTicks = 0;
        return PerformPendingReload(outError);
    }

    if (outError != nullptr) {
        *outError = L"GameLogic.dll is missing.";
    }
    return false;
}

void ScriptModuleHost::PollSourceChanges(bool allowAutomaticReload) {
    ResolveSourceDllPath(m_sourceDllPath);
    if (m_sourceDllPath.empty()) {
        if (!IsLoaded()) {
            m_status = Status::Missing;
        }
        return;
    }

    std::error_code ec;
    if (!fs::exists(m_sourceDllPath, ec) || ec) {
        if (!IsLoaded()) {
            m_status = Status::Missing;
        }
        return;
    }

    const fs::file_time_type writeTime = fs::last_write_time(m_sourceDllPath, ec);
    if (ec) {
        return;
    }

    const long long writeTimeTicks = writeTime.time_since_epoch().count();
    if (!IsLoaded()) {
        m_hasPendingReload = true;
        m_pendingWriteTimeTicks = writeTimeTicks;
        if (allowAutomaticReload) {
            (void)PerformPendingReload(nullptr);
        } else {
            m_status = Status::Stale;
        }
        return;
    }

    if (writeTimeTicks != m_loadedWriteTimeTicks) {
        if (!m_hasPendingReload || m_pendingWriteTimeTicks != writeTimeTicks) {
            m_hasPendingReload = true;
            m_pendingWriteTimeTicks = writeTimeTicks;
            m_pendingReloadReadyTick = GetTickCount64() + 350;
            m_status = Status::Stale;
        }

        if (allowAutomaticReload && GetTickCount64() >= m_pendingReloadReadyTick) {
            (void)PerformPendingReload(nullptr);
        }
    }
}

bool ScriptModuleHost::PerformPendingReload(std::wstring* outError) {
    if (m_sourceDllPath.empty()) {
        if (outError != nullptr) {
            *outError = L"GameLogic.dll is missing.";
        }
        m_status = Status::Missing;
        return false;
    }

    std::error_code ec;
    if (!fs::exists(m_sourceDllPath, ec) || ec) {
        if (outError != nullptr) {
            *outError = L"GameLogic.dll is missing.";
        }
        m_status = Status::Missing;
        return false;
    }

    const fs::file_time_type writeTime = fs::last_write_time(m_sourceDllPath, ec);
    if (ec) {
        if (outError != nullptr) {
            *outError = L"Could not read GameLogic.dll timestamp.";
        }
        m_status = Status::ReloadFailed;
        return false;
    }

    const long long writeTimeTicks = writeTime.time_since_epoch().count();
    if (m_hasPendingReload && m_pendingReloadReadyTick != 0 && GetTickCount64() < m_pendingReloadReadyTick) {
        return false;
    }

    const std::wstring previousShadowDllPath = m_loadedShadowDllPath;
    const std::wstring previousShadowPdbPath = m_loadedShadowPdbPath;
    HMODULE previousModuleHandle = m_moduleHandle;
    const Catalyst::ScriptModuleApi* previousModuleApi = m_moduleApi;

    if (!LoadModuleFromPath(m_sourceDllPath, outError)) {
        m_moduleHandle = previousModuleHandle;
        m_moduleApi = previousModuleApi;
        m_loadedShadowDllPath = previousShadowDllPath;
        m_loadedShadowPdbPath = previousShadowPdbPath;
        return false;
    }

    HMODULE newModuleHandle = m_moduleHandle;
    const Catalyst::ScriptModuleApi* newModuleApi = m_moduleApi;
    const std::wstring newShadowDllPath = m_loadedShadowDllPath;
    const std::wstring newShadowPdbPath = m_loadedShadowPdbPath;

    m_moduleHandle = previousModuleHandle;
    m_moduleApi = previousModuleApi;
    m_loadedShadowDllPath = previousShadowDllPath;
    m_loadedShadowPdbPath = previousShadowPdbPath;

    UnloadCurrentModule();

    m_moduleHandle = newModuleHandle;
    m_moduleApi = newModuleApi;
    m_loadedShadowDllPath = newShadowDllPath;
    m_loadedShadowPdbPath = newShadowPdbPath;
    m_loadedWriteTimeTicks = writeTimeTicks;
    m_pendingWriteTimeTicks = 0;
    m_hasPendingReload = false;
    m_pendingReloadReadyTick = 0;
    m_lastError.clear();
    m_status = Status::Loaded;
    RefreshAvailableClasses();
    return true;
}

bool ScriptModuleHost::HasClass(const std::string& className) const {
    return std::find(m_availableClasses.begin(), m_availableClasses.end(), className) != m_availableClasses.end();
}

Catalyst::NativeScript* ScriptModuleHost::CreateScript(const std::string& className) const {
    return (m_moduleApi != nullptr && m_moduleApi->createScript != nullptr)
        ? m_moduleApi->createScript(className.c_str())
        : nullptr;
}

void ScriptModuleHost::DestroyScript(Catalyst::NativeScript* script) const {
    if (script == nullptr) {
        return;
    }

    if (m_moduleApi != nullptr && m_moduleApi->destroyScript != nullptr) {
        m_moduleApi->destroyScript(script);
    }
}

std::wstring ScriptModuleHost::GetStatusLabel() const {
    switch (m_status) {
    case Status::Loaded:
        return L"Loaded";
    case Status::Stale:
        return L"Stale";
    case Status::ReloadFailed:
        return L"Reload failed";
    case Status::Missing:
    default:
        return L"Missing";
    }
}

bool ScriptModuleHost::ResolveSourceDllPath(std::wstring& outPath) const {
    outPath.clear();
    if (m_projectFilePath.empty()) {
        return false;
    }

    fs::path projectPath(m_projectFilePath);
    std::wstring extension = projectPath.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);

    if (extension == L".pak") {
        outPath = (projectPath.parent_path() / kGameDllFileName).lexically_normal().wstring();
        return true;
    }

    outPath = (projectPath.parent_path() / L"Binaries" / L"Debug" / kGameDllFileName).lexically_normal().wstring();
    return true;
}

bool ScriptModuleHost::LoadModuleFromPath(const std::wstring& sourceDllPath, std::wstring* outError) {
    const std::wstring shadowPath = BuildShadowCopyPath(sourceDllPath);
    const std::wstring pdbSourcePath = fs::path(sourceDllPath).replace_extension(L".pdb").wstring();
    const std::wstring pdbShadowPath = fs::path(shadowPath).replace_extension(L".pdb").wstring();

    std::error_code ec;
    fs::create_directories(fs::path(shadowPath).parent_path(), ec);
    if (ec) {
        const std::wstring error = L"Could not create DLL shadow-copy folder.";
        if (outError != nullptr) {
            *outError = error;
        }
        m_lastError = error;
        m_status = Status::ReloadFailed;
        return false;
    }

    fs::copy_file(sourceDllPath, shadowPath, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        const std::wstring error = L"Could not shadow-copy GameLogic.dll. Visual Studio may still be writing it.";
        if (outError != nullptr) {
            *outError = error;
        }
        m_lastError = error;
        m_status = Status::ReloadFailed;
        return false;
    }

    ec.clear();
    if (fs::exists(pdbSourcePath, ec) && !ec) {
        std::error_code pdbEc;
        fs::copy_file(pdbSourcePath, pdbShadowPath, fs::copy_options::overwrite_existing, pdbEc);
    }

    HMODULE moduleHandle = LoadLibraryW(shadowPath.c_str());
    if (moduleHandle == nullptr) {
        const std::wstring error = L"LoadLibrary failed for shadow-copied GameLogic.dll.";
        if (outError != nullptr) {
            *outError = error;
        }
        m_lastError = error;
        m_status = Status::ReloadFailed;
        return false;
    }

    auto* getApiAddress = reinterpret_cast<const Catalyst::ScriptModuleApi* (*)()>(
        GetProcAddress(moduleHandle, "GetCatalystScriptModuleApi"));
    if (getApiAddress == nullptr) {
        FreeLibrary(moduleHandle);
        const std::wstring error = L"GameLogic.dll does not export GetCatalystScriptModuleApi.";
        if (outError != nullptr) {
            *outError = error;
        }
        m_lastError = error;
        m_status = Status::ReloadFailed;
        return false;
    }

    const Catalyst::ScriptModuleApi* moduleApi = getApiAddress();
    if (moduleApi == nullptr || moduleApi->apiVersion != Catalyst::kScriptApiVersion) {
        FreeLibrary(moduleHandle);
        const std::wstring error = L"GameLogic.dll uses an incompatible Catalyst script API version.";
        if (outError != nullptr) {
            *outError = error;
        }
        m_lastError = error;
        m_status = Status::ReloadFailed;
        return false;
    }

    if (moduleApi->installHostApi != nullptr) {
        moduleApi->installHostApi(&m_hostApi);
    }

    m_moduleHandle = moduleHandle;
    m_moduleApi = moduleApi;
    m_loadedShadowDllPath = shadowPath;
    m_loadedShadowPdbPath = fs::exists(pdbShadowPath, ec) && !ec ? pdbShadowPath : L"";
    return true;
}

void ScriptModuleHost::UnloadCurrentModule() {
    if (m_moduleHandle != nullptr) {
        FreeLibrary(m_moduleHandle);
        m_moduleHandle = nullptr;
        m_moduleApi = nullptr;
    }

    std::error_code ec;
    if (!m_loadedShadowDllPath.empty()) {
        fs::remove(m_loadedShadowDllPath, ec);
        m_loadedShadowDllPath.clear();
    }

    if (!m_loadedShadowPdbPath.empty()) {
        fs::remove(m_loadedShadowPdbPath, ec);
        m_loadedShadowPdbPath.clear();
    }
}

void ScriptModuleHost::RefreshAvailableClasses() {
    m_availableClasses.clear();
    if (m_moduleApi == nullptr || m_moduleApi->getScriptClassCount == nullptr || m_moduleApi->getScriptClassDescs == nullptr) {
        return;
    }

    const uint32_t classCount = m_moduleApi->getScriptClassCount();
    const Catalyst::ScriptClassDesc* classDescs = m_moduleApi->getScriptClassDescs();
    if (classCount == 0 || classDescs == nullptr) {
        return;
    }

    m_availableClasses.reserve(classCount);
    for (uint32_t index = 0; index < classCount; ++index) {
        if (classDescs[index].className != nullptr && classDescs[index].className[0] != '\0') {
            m_availableClasses.emplace_back(classDescs[index].className);
        }
    }
}

std::wstring ScriptModuleHost::BuildShadowCopyPath(const std::wstring& sourceDllPath) {
    fs::path sourcePath(sourceDllPath);
    fs::path shadowFolder = sourcePath.parent_path() / L"ScriptCache";
    if (!m_projectFilePath.empty()) {
        fs::path projectPath(m_projectFilePath);
        std::wstring extension = projectPath.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
        if (extension != L".pak") {
            shadowFolder = projectPath.parent_path() / L"Intermediate" / L"ScriptCache";
        }
    }

    const std::wstring stem = sourcePath.stem().wstring();
    return (shadowFolder / (stem + L"_shadow_" + std::to_wstring(m_shadowCopyCounter++) + L".dll")).wstring();
}

std::wstring ScriptModuleHost::NormalizePath(const std::wstring& path) {
    if (path.empty()) {
        return L"";
    }

    std::error_code ec;
    fs::path normalized = fs::absolute(fs::path(path), ec);
    if (ec) {
        normalized = fs::path(path);
    }
    return normalized.lexically_normal().wstring();
}
