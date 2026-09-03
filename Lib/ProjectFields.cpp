#include "ProjectFields.h"

#include <regex>

void ParseProjectFieldsFromJson(
    const std::wstring& jsonContent,
    std::wstring& projectName,
    std::wstring& engineVersion,
    std::wstring& startupScene)
{
    std::wsmatch match;

    static const std::wregex nameRegex(L"\"ProjectName\"\\s*:\\s*\"([^\"]+)\"");
    if (std::regex_search(jsonContent, match, nameRegex) && match.size() > 1)
        projectName = match[1].str();

    static const std::wregex versionRegex(L"\"EngineVersion\"\\s*:\\s*\"([^\"]+)\"");
    if (std::regex_search(jsonContent, match, versionRegex) && match.size() > 1)
        engineVersion = match[1].str();

    static const std::wregex startupRegex(L"\"StartupScene\"\\s*:\\s*\"([^\"]*)\"");
    if (std::regex_search(jsonContent, match, startupRegex) && match.size() > 1)
        startupScene = match[1].str();
}
