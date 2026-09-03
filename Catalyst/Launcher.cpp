#include "Launcher.h"
#include "Json.h"
#include "ProjectFields.h"
#include "Resources/PakFile.h"
#include "Resources/ModelLoader.h"
#include "Resources/Material.h"
#include <commdlg.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cwctype>
#include <regex>

namespace fs = std::filesystem;

namespace {
constexpr const wchar_t* kBuildManifestLogicalPath = L"build_manifest.json";
constexpr const wchar_t* kGameCodeSolutionName = L"GameLogic.sln";
constexpr const wchar_t* kGameCodeProjectName = L"GameLogic.vcxproj";
constexpr const wchar_t* kGameCodeProjectFiltersName = L"GameLogic.vcxproj.filters";
constexpr const wchar_t* kGameCodeDllName = L"GameLogic.dll";

bool IsTextureExtension(const std::wstring& extension) {
    return extension == L".png" || extension == L".jpg" || extension == L".jpeg";
}

std::wstring EscapeJsonString(const std::wstring& value) {
    std::wstring escaped;
    escaped.reserve(value.size());

    for (wchar_t ch : value) {
        switch (ch) {
        case L'\\':
            escaped += L"\\\\";
            break;
        case L'"':
            escaped += L"\\\"";
            break;
        case L'\n':
            escaped += L"\\n";
            break;
        case L'\r':
            escaped += L"\\r";
            break;
        case L'\t':
            escaped += L"\\t";
            break;
        default:
            escaped += ch;
            break;
        }
    }

    return escaped;
}

std::wstring NormalizeProjectPathString(const std::wstring& path) {
    std::wstring normalized = path;
    std::replace(normalized.begin(), normalized.end(), L'\\', L'/');
    return normalized;
}

std::wstring GuidToString(const GUID& guid) {
    wchar_t buffer[64] = {};
    const int charsWritten = StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer)));
    if (charsWritten <= 0) {
        return L"{00000000-0000-0000-0000-000000000000}";
    }
    return buffer;
}

bool WriteUtf8TextFile(const fs::path& path, const std::string& content) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    return file.good();
}

bool WriteWideTextFile(const fs::path& path, const std::wstring& content) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::wofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file << content;
    return file.good();
}

std::string ReplaceAll(std::string text, const std::string& needle, const std::string& replacement) {
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        text.replace(pos, needle.size(), replacement);
        pos += replacement.size();
    }
    return text;
}

std::wstring ResolveEngineWorkspaceRoot() {
    wchar_t modulePathBuffer[MAX_PATH] = {};
    const DWORD modulePathLength = GetModuleFileNameW(nullptr, modulePathBuffer, MAX_PATH);
    if (modulePathLength == 0 || modulePathLength >= MAX_PATH) {
        return fs::current_path().wstring();
    }

    fs::path root = fs::path(modulePathBuffer).parent_path();
    for (int level = 0; level < 2 && !root.empty(); ++level) {
        root = root.parent_path();
    }
    if (root.empty()) {
        root = fs::current_path();
    }
    return root.wstring();
}

std::string WideToNarrowLossy(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }

    std::string converted(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, converted.data(), size, nullptr, nullptr);
    converted.pop_back();
    return converted;
}

std::string ReadEngineCatalystApiHeaderTemplate() {
    const fs::path engineRoot = ResolveEngineWorkspaceRoot();
    const fs::path apiHeaderPath = engineRoot / L"Scripting" / L"CatalystAPI.h";
    std::ifstream file(apiHeaderPath, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }

    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

bool EnsureProjectCodeWorkspace(const std::wstring& projectFilePath) {
    if (projectFilePath.empty()) {
        return false;
    }

    const fs::path projectRoot = fs::path(projectFilePath).parent_path();
    const std::wstring projectName = fs::path(projectFilePath).stem().wstring();
    const fs::path codeRoot = projectRoot / L"Code";
    const fs::path scriptsRoot = codeRoot / L"Scripts";

    std::error_code ec;
    fs::create_directories(projectRoot / L"Assets", ec);
    fs::create_directories(projectRoot / L"Code", ec);
    fs::create_directories(projectRoot / L"Binaries" / L"Debug", ec);
    fs::create_directories(projectRoot / L"Binaries" / L"Release", ec);
    fs::create_directories(projectRoot / L"Intermediate", ec);
    fs::create_directories(scriptsRoot, ec);
    fs::create_directories(projectRoot / L"Config", ec);

    const std::string apiHeaderTemplate = ReadEngineCatalystApiHeaderTemplate();
    if (!apiHeaderTemplate.empty()) {
        (void)WriteUtf8TextFile(codeRoot / L"CatalystAPI.h", apiHeaderTemplate);
        (void)WriteUtf8TextFile(scriptsRoot / L"CatalystAPI.h",
                                "#pragma once\n\n#include \"../CatalystAPI.h\"\n");
    }

    const std::string moduleSource =
        "// This file exists to keep the GameLogic project compiling even if you move or delete the example script.\n";
    if (!fs::exists(codeRoot / L"GameLogicModule.cpp", ec) || ec) {
        (void)WriteUtf8TextFile(codeRoot / L"GameLogicModule.cpp", moduleSource);
    }

    const std::string exampleScript =
        "#include \"CatalystAPI.h\"\n\n"
        "class ExampleScript final : public Catalyst::NativeScript {\n"
        "public:\n"
        "    const char* GetClassName() const override {\n"
        "        return \"ExampleScript\";\n"
        "    }\n\n"
        "    void OnStart() override {\n"
        "        PrintToConsole(\"ExampleScript started\");\n"
        "        angle = LoadFloat(\"PlayerState\", \"angle\", angle);\n"
        "        SetTimer(1, 1.0f, true);\n"
        "    }\n\n"
        "    void OnUpdate(float deltaTime) override {\n"
        "        angle += deltaTime * 1.35f;\n"
        "        if (IsActionPressed(\"Jump\")) {\n"
        "            SetColor({1.0f, 0.4f, 0.25f, 1.0f});\n"
        "        } else {\n"
        "            SetColor({1.0f, 1.0f, 1.0f, 1.0f});\n"
        "        }\n"
        "        Translate(0.0f, GetAxis(\"MoveUp\") * 120.0f * deltaTime, 0.0f);\n"
        "        Catalyst::TransformData transform = GetTransform();\n"
        "        transform.rotation.y = angle;\n"
        "        SetTransform(transform);\n"
        "    }\n\n"
        "    void OnTimer(uint64_t timerId) override {\n"
        "        if (timerId == 1) {\n"
        "            PrintToConsole(\"ExampleScript timer tick\");\n"
        "        }\n"
        "    }\n\n"
        "    void OnHotReloadSave(Catalyst::ScriptStateWriter& writer) const override {\n"
        "        writer.WriteFloat(\"angle\", angle);\n"
        "    }\n\n"
        "    void OnHotReloadLoad(const Catalyst::ScriptStateReader& reader) override {\n"
        "        angle = reader.ReadFloat(\"angle\", angle);\n"
        "        SaveFloat(\"PlayerState\", \"angle\", angle);\n"
        "    }\n\n"
        "private:\n"
        "    float angle = 0.0f;\n"
        "};\n\n"
        "REGISTER_SCRIPT(ExampleScript)\n\n"
        "// DO NOT REMOVE NO MATTER WHAT\n"
        "CATALYST_EXPORT_SCRIPT_MODULE();\n";
    if (!fs::exists(scriptsRoot / L"ExampleScript.cpp", ec) || ec) {
        (void)WriteUtf8TextFile(scriptsRoot / L"ExampleScript.cpp", exampleScript);
    }

    GUID solutionGuid{};
    GUID projectGuid{};
    CoCreateGuid(&solutionGuid);
    CoCreateGuid(&projectGuid);
    const std::wstring projectGuidWide = GuidToString(projectGuid);
    const std::string projectGuidText = WideToNarrowLossy(projectGuidWide);
    const std::string vcxprojTemplateRaw = R"VCX(<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64">
      <Configuration>Debug</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64">
      <Configuration>Release</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>17.0</VCProjectVersion>
    <Keyword>Win32Proj</Keyword>
    <ProjectGuid>__PROJECT_GUID__</ProjectGuid>
    <RootNamespace>__PROJECT_NAME__</RootNamespace>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'" Label="Configuration">
    <ConfigurationType>DynamicLibrary</ConfigurationType>
    <UseDebugLibraries>true</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'" Label="Configuration">
    <ConfigurationType>DynamicLibrary</ConfigurationType>
    <UseDebugLibraries>false</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <WholeProgramOptimization>true</WholeProgramOptimization>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <ImportGroup Label="ExtensionSettings" />
  <ImportGroup Label="Shared" />
  <ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
    <Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />
  </ImportGroup>
  <ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />
  </ImportGroup>
  <PropertyGroup Label="UserMacros" />
  <PropertyGroup>
    <OutDir>$(ProjectDir)..\Binaries\$(Configuration)\</OutDir>
    <IntDir>$(ProjectDir)..\Intermediate\GameLogic\$(Configuration)\</IntDir>
    <TargetName>GameLogic</TargetName>
  </PropertyGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
    <ClCompile>
      <WarningLevel>Level3</WarningLevel>
      <SDLCheck>true</SDLCheck>
      <PreprocessorDefinitions>_DEBUG;CATALYST_GAME_DLL;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <ConformanceMode>true</ConformanceMode>
      <LanguageStandard>stdcpp20</LanguageStandard>
      <AdditionalIncludeDirectories>$(ProjectDir);$(ProjectDir)Scripts;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
    <Link>
      <SubSystem>Windows</SubSystem>
      <GenerateDebugInformation>true</GenerateDebugInformation>
    </Link>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <ClCompile>
      <WarningLevel>Level3</WarningLevel>
      <FunctionLevelLinking>true</FunctionLevelLinking>
      <IntrinsicFunctions>true</IntrinsicFunctions>
      <SDLCheck>true</SDLCheck>
      <PreprocessorDefinitions>NDEBUG;CATALYST_GAME_DLL;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <ConformanceMode>true</ConformanceMode>
      <LanguageStandard>stdcpp20</LanguageStandard>
      <AdditionalIncludeDirectories>$(ProjectDir);$(ProjectDir)Scripts;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
    <Link>
      <SubSystem>Windows</SubSystem>
      <GenerateDebugInformation>true</GenerateDebugInformation>
    </Link>
  </ItemDefinitionGroup>
  <ItemGroup>
    <ClInclude Include="CatalystAPI.h" />
    <ClInclude Include="Scripts\*.h" />
  </ItemGroup>
  <ItemGroup>
    <ClCompile Include="GameLogicModule.cpp" />
    <ClCompile Include="Scripts\*.cpp" />
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
  <ImportGroup Label="ExtensionTargets" />
</Project>
)VCX";

    const std::string vcxprojWithGuid = ReplaceAll(vcxprojTemplateRaw, "__PROJECT_GUID__", projectGuidText);
    const std::string vcxprojContent = ReplaceAll(vcxprojWithGuid, "__PROJECT_NAME__", WideToNarrowLossy(projectName));
    (void)WriteUtf8TextFile(codeRoot / kGameCodeProjectName, vcxprojContent);

    const std::string filtersContent =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<Project ToolsVersion=\"4.0\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
        "  <ItemGroup>\n"
        "    <Filter Include=\"Source Files\">\n"
        "      <UniqueIdentifier>{4FC737F1-C7A5-4376-A066-2A32D752A2FF}</UniqueIdentifier>\n"
        "    </Filter>\n"
        "    <Filter Include=\"Header Files\">\n"
        "      <UniqueIdentifier>{93995380-89BD-4B04-88EB-625FBE52EBFB}</UniqueIdentifier>\n"
        "    </Filter>\n"
        "  </ItemGroup>\n"
        "  <ItemGroup>\n"
        "    <ClInclude Include=\"CatalystAPI.h\">\n"
        "      <Filter>Header Files</Filter>\n"
        "    </ClInclude>\n"
        "    <ClInclude Include=\"Scripts\\*.h\">\n"
        "      <Filter>Header Files</Filter>\n"
        "    </ClInclude>\n"
        "  </ItemGroup>\n"
        "  <ItemGroup>\n"
        "    <ClCompile Include=\"GameLogicModule.cpp\">\n"
        "      <Filter>Source Files</Filter>\n"
        "    </ClCompile>\n"
        "    <ClCompile Include=\"Scripts\\ExampleScript.cpp\">\n"
        "      <Filter>Source Files</Filter>\n"
        "    </ClCompile>\n"
        "  </ItemGroup>\n"
        "</Project>\n";
    (void)WriteUtf8TextFile(codeRoot / kGameCodeProjectFiltersName, filtersContent);

    const std::wstring solutionGuidText = GuidToString(solutionGuid);
    const std::wstring projectGuidTextWide = GuidToString(projectGuid);
    std::wostringstream solution;
    solution << L"Microsoft Visual Studio Solution File, Format Version 12.00\n";
    solution << L"# Visual Studio Version 17\n";
    solution << L"VisualStudioVersion = 17.0.31912.275\n";
    solution << L"MinimumVisualStudioVersion = 10.0.40219.1\n";
    solution << L"Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"GameLogic\", \"GameLogic.vcxproj\", \"" << projectGuidTextWide << L"\"\n";
    solution << L"EndProject\n";
    solution << L"Global\n";
    solution << L"\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n";
    solution << L"\t\tDebug|x64 = Debug|x64\n";
    solution << L"\t\tRelease|x64 = Release|x64\n";
    solution << L"\tEndGlobalSection\n";
    solution << L"\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n";
    solution << L"\t\t" << projectGuidTextWide << L".Debug|x64.ActiveCfg = Debug|x64\n";
    solution << L"\t\t" << projectGuidTextWide << L".Debug|x64.Build.0 = Debug|x64\n";
    solution << L"\t\t" << projectGuidTextWide << L".Release|x64.ActiveCfg = Release|x64\n";
    solution << L"\t\t" << projectGuidTextWide << L".Release|x64.Build.0 = Release|x64\n";
    solution << L"\tEndGlobalSection\n";
    solution << L"\tGlobalSection(SolutionProperties) = preSolution\n";
    solution << L"\t\tHideSolutionNode = FALSE\n";
    solution << L"\tEndGlobalSection\n";
    solution << L"\tGlobalSection(ExtensibilityGlobals) = postSolution\n";
    solution << L"\t\tSolutionGuid = " << solutionGuidText << L"\n";
    solution << L"\tEndGlobalSection\n";
    solution << L"EndGlobal\n";
    (void)WriteWideTextFile(codeRoot / kGameCodeSolutionName, solution.str());

    return true;
}

std::wstring BuildDefaultStartupScenePath(const std::wstring& projectFilePath) {
    const fs::path projectRoot = fs::path(projectFilePath).parent_path();
    return (projectRoot / L"Assets" / L"StartupScene.catalystmap").lexically_normal().wstring();
}

std::wstring BuildLegacyStartupScenePath(const std::wstring& projectFilePath) {
    const fs::path projectRoot = fs::path(projectFilePath).parent_path();
    return (projectRoot / L"Config" / L"StartupScene.catalystmap").lexically_normal().wstring();
}

AppLaunchRequest g_launchRequest;
bool g_hasLaunchRequest = false;

bool TryReadProjectInfoFromPak(const std::wstring& pakPath, ProjectInfo& outInfo) {
    std::vector<uint8_t> manifestBytes;
    if (!CatalystPak::ReadEntry(pakPath, kBuildManifestLogicalPath, manifestBytes, nullptr) || manifestBytes.empty()) {
        return false;
    }

    const std::wstring manifestContent(manifestBytes.begin(), manifestBytes.end());
    outInfo.Path = pakPath;
    outInfo.ProjectName = fs::path(pakPath).stem().wstring();
    outInfo.EngineVersion = L"Packed";
    outInfo.StartupScene = L"";

    std::wsmatch match;
    std::wregex nameRegex(L"\"ProjectName\"\\s*:\\s*\"([^\"]+)\"");
    if (std::regex_search(manifestContent, match, nameRegex) && match.size() > 1) {
        outInfo.ProjectName = match[1].str();
    }

    std::wregex startupRegex(L"\"StartupScene\"\\s*:\\s*\"([^\"]*)\"");
    if (std::regex_search(manifestContent, match, startupRegex) && match.size() > 1) {
        outInfo.StartupScene = match[1].str();
    }

    return true;
}
}

std::wstring BrowseForProjectFile(HWND ownerWindow) {
    OPENFILENAMEW ofn;
    wchar_t szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ownerWindow;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"Catalyst Project (*.CatalystProj)\0*.CatalystProj\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        return std::wstring(ofn.lpstrFile);
    }
    return L"";
}

std::wstring BrowseForProjectFolder(HWND ownerWindow) {
    std::wstring folderPath = L"";
    IFileOpenDialog* pFileOpen;
    
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    if (SUCCEEDED(hr)) {
        DWORD dwOptions;
        if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions))) pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        if (SUCCEEDED(pFileOpen->Show(ownerWindow))) {
            IShellItem* pItem;
            if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
                PWSTR pszFilePath;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                    folderPath = pszFilePath;
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
    return folderPath;
}

std::wstring BrowseForAssetFile(HWND ownerWindow) {
    OPENFILENAMEW ofn;
    wchar_t szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ownerWindow;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"Supported Assets (*.obj;*.fbx;*.png;*.jpg;*.jpeg)\0*.obj;*.fbx;*.png;*.jpg;*.jpeg\0Models (*.obj;*.fbx)\0*.obj;*.fbx\0Wavefront OBJ (*.obj)\0*.obj\0Autodesk FBX (*.fbx)\0*.fbx\0Textures (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        return std::wstring(ofn.lpstrFile);
    }
    return L"";
}

std::wstring BrowseForActorAssetFile(HWND ownerWindow) {
    OPENFILENAMEW ofn;
    wchar_t szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ownerWindow;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"Catalyst Actor Assets (*.catalystactor)\0*.catalystactor\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        return std::wstring(ofn.lpstrFile);
    }
    return L"";
}

std::wstring BrowseForTextureFile(HWND ownerWindow) {
    OPENFILENAMEW ofn;
    wchar_t szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ownerWindow;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"Textures (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        return std::wstring(ofn.lpstrFile);
    }
    return L"";
}

std::wstring BrowseForMaterialFile(HWND ownerWindow) {
    OPENFILENAMEW ofn;
    wchar_t szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ownerWindow;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"Catalyst Materials (*.catalystmat)\0*.catalystmat\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        return std::wstring(ofn.lpstrFile);
    }
    return L"";
}

std::wstring BrowseForBlueprintFile(HWND ownerWindow) {
    OPENFILENAMEW ofn;
    wchar_t szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ownerWindow;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"Catalyst Blueprints (*.catalystblueprint)\0*.catalystblueprint\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        return std::wstring(ofn.lpstrFile);
    }
    return L"";
}

std::wstring BrowseForUIBlueprintFile(HWND ownerWindow) {
    OPENFILENAMEW ofn;
    wchar_t szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ownerWindow;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"Catalyst UI Blueprints (*.catalystuiblueprint)\0*.catalystuiblueprint\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        return std::wstring(ofn.lpstrFile);
    }
    return L"";
}

std::wstring BrowseForMapFile(HWND ownerWindow) {
    OPENFILENAMEW ofn;
    wchar_t szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ownerWindow;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"Catalyst Maps (*.catalystmap)\0*.catalystmap\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        return std::wstring(ofn.lpstrFile);
    }
    return L"";
}

bool IsImportableModelExtension(const std::wstring& extension) {
    return extension == L".obj" || extension == L".fbx";
}

namespace {

// An FBX records both a relative path and the absolute one from whichever
// machine authored it. The absolute one is almost never valid here, so the
// search works outward from the model's own folder.
fs::path ResolveTextureOnDisk(const fs::path& modelFolder, const CatalystImport::FbxTextureReference& reference) {
    std::vector<fs::path> candidates;

    if (!reference.RelativePath.empty()) {
        // FBX writes these with backslashes regardless of platform.
        std::string relative = reference.RelativePath;
        std::replace(relative.begin(), relative.end(), '\\', '/');
        candidates.push_back(modelFolder / fs::path(relative));
        candidates.push_back(modelFolder / fs::path(relative).filename());
    }

    if (!reference.AbsolutePath.empty()) {
        std::string absolute = reference.AbsolutePath;
        std::replace(absolute.begin(), absolute.end(), '\\', '/');
        const fs::path asWritten(absolute);
        candidates.push_back(asWritten);
        // The filename alone is the part most likely to still be true.
        candidates.push_back(modelFolder / asWritten.filename());
        for (const wchar_t* folder : {L"textures", L"Textures", L"tex", L"maps", L"Maps"}) {
            candidates.push_back(modelFolder / folder / asWritten.filename());
        }
    }

    std::error_code error;
    for (const fs::path& candidate : candidates) {
        if (!candidate.empty() && fs::exists(candidate, error) && fs::is_regular_file(candidate, error)) {
            return candidate;
        }
    }
    return {};
}

std::string TextureDisplayName(const CatalystImport::FbxTextureReference& reference) {
    std::string name = reference.RelativePath.empty() ? reference.AbsolutePath : reference.RelativePath;
    const size_t slash = name.find_last_of("/\\");
    return slash == std::string::npos ? name : name.substr(slash + 1);
}

std::wstring ToLowerWide(const std::wstring& value) {
    std::wstring lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), towlower);
    return lowered;
}

// Prefix with the asset name so two models with a texture called "diffuse.png"
// cannot overwrite one another. A map already named after the model keeps its
// own name rather than carrying it twice.
fs::path MakeImportedTextureName(const fs::path& assetPath, const fs::path& sourceTexture) {
    const std::wstring assetStem = assetPath.stem().wstring();
    if (ToLowerWide(sourceTexture.stem().wstring()).rfind(ToLowerWide(assetStem), 0) == 0) {
        return sourceTexture.filename();
    }
    return fs::path(assetStem + L"_" + sourceTexture.filename().wstring());
}

// Names the slot a map belongs to from its filename. Only the tokens listed
// here count: a cavity, displacement, AO, specular or gloss map has no slot to
// go in, and guessing one for it is worse than leaving it out.
CatalystImport::FbxTextureSlot ClassifyTextureFileName(const std::wstring& stem) {
    std::vector<std::wstring> tokens;
    std::wstring current;
    for (wchar_t ch : ToLowerWide(stem)) {
        if (ch == L'_' || ch == L'-' || ch == L'.' || ch == L' ') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += ch;
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }

    // Read back from the end, so a trailing UDIM or resolution tag is stepped
    // over rather than mistaken for the slot name.
    for (size_t index = tokens.size(); index-- > 0;) {
        const std::wstring& token = tokens[index];
        if (token == L"basecolor" || token == L"basecolour" || token == L"albedo" ||
            token == L"diffuse" || token == L"diff" ||
            token == L"color" || token == L"colour" || token == L"col") {
            return CatalystImport::FbxTextureSlot::Albedo;
        }
        if (token == L"normal" || token == L"normalgl" || token == L"normaldx" ||
            token == L"nrm" || token == L"norm" || token == L"nor") {
            return CatalystImport::FbxTextureSlot::Normal;
        }
        if (token == L"roughness" || token == L"rough" || token == L"rgh") {
            return CatalystImport::FbxTextureSlot::Roughness;
        }
    }

    return CatalystImport::FbxTextureSlot::Unknown;
}

struct SidecarTexture {
    fs::path file;
    CatalystImport::FbxTextureSlot slot = CatalystImport::FbxTextureSlot::Unknown;
};

// Marketplace and scan-library exports routinely carry no texture nodes at all:
// the maps sit beside the model, named after it with a suffix. When the file
// references nothing, the folder is the only thing left to go on.
std::vector<SidecarTexture> FindSidecarTextures(const fs::path& modelPath) {
    std::vector<SidecarTexture> found;
    const fs::path modelFolder = modelPath.parent_path();
    if (modelFolder.empty()) {
        return found;
    }

    const std::wstring modelStem = ToLowerWide(modelPath.stem().wstring());
    const fs::directory_iterator endIter;

    // With one model in the folder, every map in it belongs to that model. With
    // several, only the ones named after this one do.
    uint32_t modelCount = 0;
    std::error_code countError;
    fs::directory_iterator countIter(modelFolder, fs::directory_options::skip_permission_denied, countError);
    while (!countError && countIter != endIter) {
        std::error_code entryError;
        const bool isFile = countIter->is_regular_file(entryError) && !entryError;
        const fs::path filePath = countIter->path();
        countIter.increment(countError);
        if (isFile && IsImportableModelExtension(ToLowerWide(filePath.extension().wstring()))) {
            ++modelCount;
        }
    }
    const bool folderBelongsToThisModel = (modelCount <= 1);

    std::vector<fs::path> searchFolders{modelFolder};
    for (const wchar_t* subfolder : {L"textures", L"tex", L"maps"}) {
        std::error_code subError;
        const fs::path candidate = modelFolder / subfolder;
        if (fs::is_directory(candidate, subError) && !subError) {
            searchFolders.push_back(candidate);
        }
    }

    for (const fs::path& folder : searchFolders) {
        std::error_code iterError;
        fs::directory_iterator iter(folder, fs::directory_options::skip_permission_denied, iterError);
        while (!iterError && iter != endIter) {
            std::error_code entryError;
            const bool isFile = iter->is_regular_file(entryError) && !entryError;
            const fs::path filePath = iter->path();
            iter.increment(iterError);
            if (!isFile || !IsTextureExtension(ToLowerWide(filePath.extension().wstring()))) {
                continue;
            }

            const std::wstring stem = ToLowerWide(filePath.stem().wstring());
            if (!folderBelongsToThisModel && stem.rfind(modelStem, 0) != 0) {
                continue;
            }

            const CatalystImport::FbxTextureSlot slot = ClassifyTextureFileName(stem);
            if (slot == CatalystImport::FbxTextureSlot::Albedo ||
                slot == CatalystImport::FbxTextureSlot::Normal ||
                slot == CatalystImport::FbxTextureSlot::Roughness) {
                found.push_back({filePath, slot});
            }
        }
    }

    return found;
}

}

ImportedTextureResult ImportModelTextures(const std::wstring& sourceModelPath,
                                          const std::wstring& importedAssetPath,
                                          const std::vector<CatalystImport::FbxTextureReference>& textures) {
    // No early out on an empty list: a file that references nothing is exactly
    // the case the sidecar scan below exists for.
    ImportedTextureResult result;

    const fs::path modelFolder = fs::path(sourceModelPath).parent_path();
    const fs::path assetPath(importedAssetPath);
    const fs::path destinationFolder = assetPath.parent_path();

    Material material;
    material.name = assetPath.stem().string();
    bool wroteAnySlot = false;

    for (const CatalystImport::FbxTextureReference& reference : textures) {
        const fs::path found = ResolveTextureOnDisk(modelFolder, reference);
        if (found.empty()) {
            ++result.missing;
            result.missingNames.push_back(TextureDisplayName(reference));
            continue;
        }

        const fs::path destination = destinationFolder / MakeImportedTextureName(assetPath, found);

        std::error_code copyError;
        fs::copy_file(found, destination, fs::copy_options::overwrite_existing, copyError);
        if (copyError) {
            ++result.missing;
            result.missingNames.push_back(TextureDisplayName(reference));
            continue;
        }
        ++result.copied;

        const std::string link = destination.filename().generic_string();
        switch (reference.Slot) {
        case CatalystImport::FbxTextureSlot::Albedo:
            if (material.albedoPath.empty()) { material.albedoPath = link; wroteAnySlot = true; }
            break;
        case CatalystImport::FbxTextureSlot::Normal:
            if (material.normalPath.empty()) { material.normalPath = link; wroteAnySlot = true; }
            break;
        case CatalystImport::FbxTextureSlot::Roughness:
            if (material.roughnessPath.empty()) { material.roughnessPath = link; wroteAnySlot = true; }
            break;
        default:
            // Copied so the artist has it, but not guessed into a slot.
            break;
        }
    }

    // Whatever the file itself did not name. Slots already filled from its own
    // references win, so this only ever adds.
    if (material.albedoPath.empty() || material.normalPath.empty() || material.roughnessPath.empty()) {
        for (const SidecarTexture& sidecar : FindSidecarTextures(fs::path(sourceModelPath))) {
            std::string* slot = nullptr;
            switch (sidecar.slot) {
            case CatalystImport::FbxTextureSlot::Albedo:    slot = &material.albedoPath; break;
            case CatalystImport::FbxTextureSlot::Normal:    slot = &material.normalPath; break;
            case CatalystImport::FbxTextureSlot::Roughness: slot = &material.roughnessPath; break;
            default: break;
            }
            if (slot == nullptr || !slot->empty()) {
                continue;
            }

            const fs::path destination = destinationFolder / MakeImportedTextureName(assetPath, sidecar.file);
            std::error_code copyError;
            fs::copy_file(sidecar.file, destination, fs::copy_options::overwrite_existing, copyError);
            if (copyError) {
                continue;
            }

            ++result.copied;
            *slot = destination.filename().generic_string();
            wroteAnySlot = true;
        }
    }

    if (wroteAnySlot) {
        const fs::path materialPath = destinationFolder / (assetPath.stem().wstring() + L".catalystmat");
        if (material.SaveToFile(materialPath.wstring())) {
            result.materialPath = materialPath.wstring();
        }
    }
    return result;
}

bool ImportAssetToProject(const std::wstring& sourcePath,
                          const std::wstring& projectAssetsFolder,
                          const std::wstring& newName,
                          const MeshImportOptions& meshOptions,
                          std::wstring* importedPath,
                          CatalystImport::MeshBuildStats* outStats,
                          std::string* outError) {
    if (sourcePath.empty() || projectAssetsFolder.empty() || newName.empty()) {
        if (outError != nullptr) {
            *outError = "The import is missing a source file, a destination folder, or a name.";
        }
        return false;
    }

    fs::path src(sourcePath);
    fs::path destFolder(projectAssetsFolder);
    std::wstring ext = src.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), towlower);

    const bool isModel = IsImportableModelExtension(ext);
    const bool isTexture = IsTextureExtension(ext);
    if (!isModel && !isTexture) {
        if (outError != nullptr) {
            *outError = "That file type cannot be imported.";
        }
        return false;
    }

    std::error_code folderError;
    if (!fs::exists(destFolder)) {
        fs::create_directories(destFolder, folderError);
        if (folderError) {
            if (outError != nullptr) {
                *outError = "Could not create the destination folder.";
            }
            return false;
        }
    }

    fs::path destFile = destFolder / newName;
    destFile.replace_extension(isModel ? L".catalystactor" : ext);

    if (isModel) {
        // Parse, weld, shade and reorder once here rather than on every load.
        CatalystImport::MeshBuildStats localStats;
        CatalystImport::MeshBuildStats* stats = (outStats != nullptr) ? outStats : &localStats;
        if (!ModelLoader::ConvertToAsset(src.wstring(), destFile.wstring(), meshOptions, stats, outError)) {
            return false;
        }
        // Bring the textures the model refers to along with it, so the asset is
        // usable rather than untextured on arrival.
        const ImportedTextureResult textureResult =
            ImportModelTextures(src.wstring(), destFile.wstring(), stats->textures);
        stats->texturesCopied = textureResult.copied;
        stats->texturesMissing = textureResult.missing;
        stats->wroteMaterial = !textureResult.materialPath.empty();
        if (importedPath != nullptr) {
            *importedPath = destFile.wstring();
        }
        return true;
    }

    try {
        fs::copy_file(src, destFile, fs::copy_options::overwrite_existing);
        if (importedPath != nullptr) {
            *importedPath = destFile.wstring();
        }
        return true;
    } catch (const std::exception& exception) {
        if (outError != nullptr) {
            *outError = exception.what();
        }
        return false;
    } catch (...) {
        if (outError != nullptr) {
            *outError = "Copying the file failed.";
        }
        return false;
    }
}

bool ImportAssetToProject(const std::wstring& sourcePath, const std::wstring& projectAssetsFolder, const std::wstring& newName, std::wstring* importedPath) {
    return ImportAssetToProject(sourcePath, projectAssetsFolder, newName, MeshImportOptions(), importedPath, nullptr, nullptr);
}

std::vector<std::wstring> GetRecentProjects() {
    std::vector<std::wstring> projects;
    std::wifstream file(L"recent_projects.txt");
    std::wstring line;
    while(std::getline(file, line)) {
        if(!line.empty() && std::find(projects.begin(), projects.end(), line) == projects.end()) {
            projects.push_back(line);
        }
    }
    return projects;
}

namespace {
// The project file is written as narrow text, so the JSON parser reads it
// directly. A missing or malformed Settings block leaves the defaults.
bool ReadProjectSettings(const std::wstring& projectFilePath, ProjectSettings& outSettings) {
    std::ifstream file(fs::path(projectFilePath), std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Json::Parser parser(content);
    Json::Value root;
    if (!parser.Parse(root) || root.type != Json::ValueType::Object) {
        return false;
    }

    const Json::Value* settings = Json::FindField(root, "Settings");
    if (settings == nullptr || settings->type != Json::ValueType::Object) {
        return false;
    }

    const ProjectSettings defaults;
    outSettings.exposure = Json::GetNumber(*settings, "Exposure", defaults.exposure);
    outSettings.bloomThreshold = Json::GetNumber(*settings, "BloomThreshold", defaults.bloomThreshold);
    outSettings.bloomIntensity = Json::GetNumber(*settings, "BloomIntensity", defaults.bloomIntensity);
    outSettings.fixedTimeStep = Json::GetNumber(*settings, "FixedTimeStep", defaults.fixedTimeStep);
    outSettings.raytraceShadowStrength = Json::GetNumber(*settings, "RaytraceShadowStrength", defaults.raytraceShadowStrength);
    outSettings.raytraceReflectionIntensity = Json::GetNumber(*settings, "RaytraceReflectionIntensity", defaults.raytraceReflectionIntensity);
    outSettings.raytraceReflectionDistance = Json::GetNumber(*settings, "RaytraceReflectionDistance", defaults.raytraceReflectionDistance);
    outSettings.raytraceSurfaceBias = Json::GetNumber(*settings, "RaytraceSurfaceBias", defaults.raytraceSurfaceBias);
    outSettings.raytraceAoStrength = Json::GetNumber(*settings, "RaytraceAoStrength", defaults.raytraceAoStrength);
    outSettings.raytraceAoRadius = Json::GetNumber(*settings, "RaytraceAoRadius", defaults.raytraceAoRadius);
    outSettings.raytraceAoSamples = Json::GetNumber(*settings, "RaytraceAoSamples", defaults.raytraceAoSamples);
    outSettings.raytraceShadowSoftness = Json::GetNumber(*settings, "RaytraceShadowSoftness", defaults.raytraceShadowSoftness);
    outSettings.raytraceShadowSamples = Json::GetNumber(*settings, "RaytraceShadowSamples", defaults.raytraceShadowSamples);
    outSettings.colorTint = Json::GetFloat3(*settings, "ColorTint", defaults.colorTint);
    outSettings.gravity   = Json::GetFloat3(*settings, "Gravity", defaults.gravity);
    outSettings.raytraceEnabled = Json::GetBool(*settings, "RaytraceEnabled", defaults.raytraceEnabled);
    outSettings.raytraceShadowsEnabled     = Json::GetBool(*settings, "RaytraceShadowsEnabled",     defaults.raytraceShadowsEnabled);
    outSettings.raytraceReflectionsEnabled = Json::GetBool(*settings, "RaytraceReflectionsEnabled", defaults.raytraceReflectionsEnabled);
    outSettings.raytraceAoEnabled          = Json::GetBool(*settings, "RaytraceAoEnabled",          defaults.raytraceAoEnabled);
    outSettings.ClampToValidRanges();
    return true;
}
}

ProjectInfo ParseProjectFile(const std::wstring& path) {
    ProjectInfo info;
    info.Path = path;
    info.ProjectName = fs::path(path).stem().wstring(); 
    info.EngineVersion = L"Unknown";
    info.StartupScene = L"";

    std::wstring extension = fs::path(path).extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
    if (extension == L".pak") {
        TryReadProjectInfoFromPak(path, info);
        return info;
    }

    std::wifstream file(path);
    if (!file.is_open()) return info;

    std::wstring content((std::istreambuf_iterator<wchar_t>(file)), std::istreambuf_iterator<wchar_t>());

    ParseProjectFieldsFromJson(content, info.ProjectName, info.EngineVersion, info.StartupScene);

    ReadProjectSettings(path, info.Settings);
    return info;
}

std::vector<ProjectInfo> GetRecentProjectsInfo() {
    auto paths = GetRecentProjects();
    std::vector<ProjectInfo> infos;
    infos.reserve(paths.size());
    for (const auto& path : paths) {
        if (fs::exists(path)) { 
            infos.push_back(ParseProjectFile(path));
        }
    }
    return infos;
}

void AddRecentProject(const std::wstring& path) {
    auto recents = GetRecentProjects();
    recents.erase(std::remove(recents.begin(), recents.end(), path), recents.end());
    recents.insert(recents.begin(), path);
    
    std::wofstream file(L"recent_projects.txt");
    int count = 0;
    for (const auto& p : recents) {
        if (count++ >= 10) break; 
        file << p << L"\n";
    }
}

void RemoveRecentProject(const std::wstring& path) {
    auto recents = GetRecentProjects();
    recents.erase(std::remove(recents.begin(), recents.end(), path), recents.end());

    std::wofstream file(L"recent_projects.txt");
    int count = 0;
    for (const auto& recentPath : recents) {
        if (count++ >= 10) break;
        file << recentPath << L"\n";
    }
}

void CreateNewProject(const std::wstring& targetFolder, const std::string& projectName) {
    std::wstring wProjectName(projectName.begin(), projectName.end());
    fs::path projectRoot = fs::path(targetFolder) / wProjectName;

    fs::create_directories(projectRoot / L"Assets");
    fs::create_directories(projectRoot / L"Scripts");
    fs::create_directories(projectRoot / L"Code");
    fs::create_directories(projectRoot / L"Binaries" / L"Debug");
    fs::create_directories(projectRoot / L"Binaries" / L"Release");
    fs::create_directories(projectRoot / L"Intermediate");
    fs::create_directories(projectRoot / L"Config");

    fs::path projectFile = projectRoot / (wProjectName + L".CatalystProj");
    
    std::wofstream outFile(projectFile);
    if (outFile.is_open()) {
        outFile << L"{\n";
        outFile << L"  \"ProjectName\": \"" << wProjectName << L"\",\n";
        outFile << L"  \"EngineVersion\": \"1.0.0\",\n";
        outFile << L"  \"StartupScene\": \"\"\n";
        outFile << L"}\n";
        outFile.close();
    }
    (void)EnsureProjectCodeWorkspace(projectFile.wstring());
    AddRecentProject(projectFile.wstring());
}

bool EnsureProjectCodeWorkspaceReady(const std::wstring& projectFilePath) {
    return EnsureProjectCodeWorkspace(projectFilePath);
}

std::wstring ResolveProjectCodeSolutionPath(const std::wstring& projectFilePath) {
    if (projectFilePath.empty()) {
        return L"";
    }

    return (fs::path(projectFilePath).parent_path() / L"Code" / kGameCodeSolutionName).lexically_normal().wstring();
}

std::wstring ResolveProjectGameDllPath(const std::wstring& projectFilePath, bool buildRelease) {
    if (projectFilePath.empty()) {
        return L"";
    }

    const fs::path projectRoot = fs::path(projectFilePath).parent_path();
    return (projectRoot / L"Binaries" / (buildRelease ? L"Release" : L"Debug") / kGameCodeDllName).lexically_normal().wstring();
}

bool OpenProjectCodeSolution(const std::wstring& projectFilePath) {
    if (projectFilePath.empty()) {
        return false;
    }

    if (!EnsureProjectCodeWorkspace(projectFilePath)) {
        return false;
    }

    const std::wstring solutionPath = ResolveProjectCodeSolutionPath(projectFilePath);
    if (solutionPath.empty() || !fs::exists(solutionPath)) {
        return false;
    }

    const HINSTANCE openResult = ShellExecuteW(nullptr, L"open", solutionPath.c_str(), nullptr,
                                               fs::path(solutionPath).parent_path().wstring().c_str(), SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(openResult) > 32;
}

bool OpenProjectCodeFile(const std::wstring& projectFilePath, const std::wstring& codeFilePath) {
    if (projectFilePath.empty() || codeFilePath.empty()) {
        return false;
    }

    if (!EnsureProjectCodeWorkspace(projectFilePath)) {
        return false;
    }

    const fs::path normalizedCodeFilePath = fs::path(codeFilePath).lexically_normal();
    if (!fs::exists(normalizedCodeFilePath)) {
        return false;
    }

    (void)OpenProjectCodeSolution(projectFilePath);
    const HINSTANCE openResult = ShellExecuteW(nullptr, L"open", normalizedCodeFilePath.wstring().c_str(), nullptr,
                                               normalizedCodeFilePath.parent_path().wstring().c_str(), SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(openResult) > 32;
}

std::wstring ResolveProjectStartupScenePath(const std::wstring& projectFilePath) {
    if (projectFilePath.empty()) {
        return L"";
    }

    const ProjectInfo info = ParseProjectFile(projectFilePath);
    if (!info.StartupScene.empty()) {
        fs::path startupPath(info.StartupScene);
        if (!startupPath.is_absolute()) {
            startupPath = fs::path(projectFilePath).parent_path() / startupPath;
        }
        return startupPath.lexically_normal().wstring();
    }

    std::wstring extension = fs::path(projectFilePath).extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
    if (extension == L".pak") {
        return L"";
    }

    return BuildDefaultStartupScenePath(projectFilePath);
}

std::wstring ResolveProjectStartupSceneSavePath(const std::wstring& projectFilePath) {
    if (projectFilePath.empty()) {
        return L"";
    }

    const ProjectInfo info = ParseProjectFile(projectFilePath);
    if (!info.StartupScene.empty()) {
        fs::path startupPath(info.StartupScene);
        if (!startupPath.is_absolute()) {
            startupPath = fs::path(projectFilePath).parent_path() / startupPath;
        }

        const std::wstring normalizedStartupPath = startupPath.lexically_normal().wstring();
        const std::wstring legacyStartupPath = BuildLegacyStartupScenePath(projectFilePath);
        if (_wcsicmp(normalizedStartupPath.c_str(), legacyStartupPath.c_str()) != 0) {
            return normalizedStartupPath;
        }
    }

    return BuildDefaultStartupScenePath(projectFilePath);
}

bool UpdateProjectStartupScene(const std::wstring& projectFilePath, const std::wstring& startupScenePath) {
    if (projectFilePath.empty() || startupScenePath.empty()) {
        return false;
    }

    const ProjectInfo info = ParseProjectFile(projectFilePath);
    const fs::path projectRoot = fs::path(projectFilePath).parent_path();
    fs::path scenePath(startupScenePath);
    std::error_code ec;
    if (!scenePath.is_absolute()) {
        scenePath = projectRoot / scenePath;
    }

    fs::path relativePath = fs::relative(scenePath, projectRoot, ec);
    const std::wstring storedPath = NormalizeProjectPathString((ec ? scenePath : relativePath).lexically_normal().wstring());

    ProjectInfo updated = info;
    updated.StartupScene = storedPath;
    return WriteProjectFile(projectFilePath, updated);
}

// Rewrites the whole project file, so every caller must start from a
// ParseProjectFile result or it will drop fields it does not know about.
bool WriteProjectFile(const std::wstring& projectFilePath, const ProjectInfo& info) {
    if (projectFilePath.empty()) {
        return false;
    }

    std::wofstream outFile(projectFilePath, std::ios::trunc);
    if (!outFile.is_open()) {
        return false;
    }

    const std::wstring projectName = info.ProjectName.empty() ? fs::path(projectFilePath).stem().wstring() : info.ProjectName;
    const std::wstring engineVersion = (info.EngineVersion.empty() || info.EngineVersion == L"Unknown") ? L"1.0.0" : info.EngineVersion;

    ProjectSettings settings = info.Settings;
    settings.ClampToValidRanges();

    outFile << L"{\n";
    outFile << L"  \"ProjectName\": \"" << EscapeJsonString(projectName) << L"\",\n";
    outFile << L"  \"EngineVersion\": \"" << EscapeJsonString(engineVersion) << L"\",\n";
    outFile << L"  \"StartupScene\": \"" << EscapeJsonString(info.StartupScene) << L"\",\n";
    outFile << L"  \"Settings\": {\n";
    outFile << L"    \"Exposure\": " << settings.exposure << L",\n";
    outFile << L"    \"ColorTint\": [" << settings.colorTint.x << L", " << settings.colorTint.y << L", " << settings.colorTint.z << L"],\n";
    outFile << L"    \"BloomThreshold\": " << settings.bloomThreshold << L",\n";
    outFile << L"    \"BloomIntensity\": " << settings.bloomIntensity << L",\n";
    outFile << L"    \"Gravity\": [" << settings.gravity.x << L", " << settings.gravity.y << L", " << settings.gravity.z << L"],\n";
    outFile << L"    \"FixedTimeStep\": " << settings.fixedTimeStep << L",\n";
    outFile << L"    \"RaytraceEnabled\": " << (settings.raytraceEnabled ? L"true" : L"false") << L",\n";
    outFile << L"    \"RaytraceShadowsEnabled\": " << (settings.raytraceShadowsEnabled ? L"true" : L"false") << L",\n";
    outFile << L"    \"RaytraceReflectionsEnabled\": " << (settings.raytraceReflectionsEnabled ? L"true" : L"false") << L",\n";
    outFile << L"    \"RaytraceAoEnabled\": " << (settings.raytraceAoEnabled ? L"true" : L"false") << L",\n";
    outFile << L"    \"RaytraceShadowStrength\": " << settings.raytraceShadowStrength << L",\n";
    outFile << L"    \"RaytraceReflectionIntensity\": " << settings.raytraceReflectionIntensity << L",\n";
    outFile << L"    \"RaytraceReflectionDistance\": " << settings.raytraceReflectionDistance << L",\n";
    outFile << L"    \"RaytraceSurfaceBias\": " << settings.raytraceSurfaceBias << L",\n";
    outFile << L"    \"RaytraceAoStrength\": " << settings.raytraceAoStrength << L",\n";
    outFile << L"    \"RaytraceAoRadius\": " << settings.raytraceAoRadius << L",\n";
    outFile << L"    \"RaytraceAoSamples\": " << settings.raytraceAoSamples << L",\n";
    outFile << L"    \"RaytraceShadowSoftness\": " << settings.raytraceShadowSoftness << L",\n";
    outFile << L"    \"RaytraceShadowSamples\": " << settings.raytraceShadowSamples << L"\n";
    outFile << L"  }\n";
    outFile << L"}\n";
    return outFile.good();
}

bool UpdateProjectSettings(const std::wstring& projectFilePath, const ProjectSettings& settings) {
    if (projectFilePath.empty()) {
        return false;
    }

    ProjectInfo info = ParseProjectFile(projectFilePath);
    info.Settings = settings;
    return WriteProjectFile(projectFilePath, info);
}

bool BuildAdjacentPlayerLaunchRequest(AppLaunchRequest& outRequest) {
    outRequest = {};

    wchar_t modulePathBuffer[MAX_PATH] = {};
    const DWORD modulePathLength = GetModuleFileNameW(nullptr, modulePathBuffer, MAX_PATH);
    if (modulePathLength == 0 || modulePathLength >= MAX_PATH) {
        return false;
    }

    const fs::path exeFolder = fs::path(modulePathBuffer).parent_path();
    if (exeFolder.empty() || !fs::exists(exeFolder)) {
        return false;
    }

    std::vector<fs::path> projectFiles;
    std::vector<fs::path> pakFiles;
    for (const auto& entry : fs::directory_iterator(exeFolder)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        std::wstring extension = entry.path().extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
        if (extension == L".catalystproj") {
            projectFiles.push_back(entry.path().lexically_normal());
        } else if (extension == L".pak") {
            pakFiles.push_back(entry.path().lexically_normal());
        }
    }

    outRequest.play = true;
    if (!projectFiles.empty()) {
        outRequest.projectFilePath = projectFiles.front().wstring();
        outRequest.mapPath = ResolveProjectStartupScenePath(outRequest.projectFilePath);
        return !outRequest.projectFilePath.empty();
    }

    for (const fs::path& pakPath : pakFiles) {
        ProjectInfo info = ParseProjectFile(pakPath.wstring());
        if (!info.StartupScene.empty()) {
            outRequest.projectFilePath = pakPath.wstring();
            outRequest.mapPath = ResolveProjectStartupScenePath(outRequest.projectFilePath);
            return true;
        }
    }

    return false;
}

void SetAppLaunchRequest(const AppLaunchRequest& request) {
    g_launchRequest = request;
    g_hasLaunchRequest = true;
}

bool ConsumeAppLaunchRequest(AppLaunchRequest& outRequest) {
    if (!g_hasLaunchRequest) {
        return false;
    }

    outRequest = g_launchRequest;
    g_hasLaunchRequest = false;
    g_launchRequest = {};
    return true;
}

bool DeleteProject(const std::wstring& projectFilePath) {
    if (projectFilePath.empty()) {
        return false;
    }

    fs::path rawProjectFile(projectFilePath);
    std::wstring extension = rawProjectFile.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
    if (extension != L".catalystproj") {
        return false;
    }

    std::error_code ec;
    fs::path normalizedProjectFile = fs::weakly_canonical(rawProjectFile, ec);
    if (ec) {
        normalizedProjectFile = rawProjectFile.lexically_normal();
    }

    fs::path projectRoot = normalizedProjectFile.parent_path();
    if (projectRoot.empty() || projectRoot.filename().empty()) {
        return false;
    }

    fs::path expectedProjectFile = projectRoot / normalizedProjectFile.filename();
    if (expectedProjectFile.lexically_normal() != normalizedProjectFile.lexically_normal()) {
        return false;
    }

    std::error_code removeError;
    const uintmax_t removedCount = fs::remove_all(projectRoot, removeError);
    if (removeError || removedCount == 0) {
        return false;
    }

    RemoveRecentProject(projectFilePath);
    return true;
}
