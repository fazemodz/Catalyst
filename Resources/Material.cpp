#include "Material.h"
#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

namespace {
std::string EscapeJsonString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());

    for (char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += ch;
            break;
        }
    }

    return escaped;
}

std::string UnescapeJsonString(const std::string& value) {
    std::string unescaped;
    unescaped.reserve(value.size());

    bool escaped = false;
    for (char ch : value) {
        if (!escaped) {
            if (ch == '\\') {
                escaped = true;
            } else {
                unescaped += ch;
            }
            continue;
        }

        switch (ch) {
        case '\\':
            unescaped += '\\';
            break;
        case '"':
            unescaped += '"';
            break;
        case 'n':
            unescaped += '\n';
            break;
        case 'r':
            unescaped += '\r';
            break;
        case 't':
            unescaped += '\t';
            break;
        default:
            unescaped += ch;
            break;
        }

        escaped = false;
    }

    if (escaped) {
        unescaped += '\\';
    }

    return unescaped;
}

std::string ExtractJsonString(const std::string& content, const char* key) {
    const std::regex pattern(
        std::string("\"") + key + "\"\\s*:\\s*\"((?:\\\\.|[^\"])*)\"",
        std::regex_constants::icase);
    std::smatch match;
    if (!std::regex_search(content, match, pattern) || match.size() < 2) {
        return "";
    }

    return UnescapeJsonString(match[1].str());
}

std::string NormalizeLinkedPath(const std::string& path) {
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    return normalized;
}
}

bool Material::LoadFromFile(const std::wstring& filepath) {
    sourcePath = filepath;
    name = fs::path(filepath).stem().string();
    albedoPath.clear();
    normalPath.clear();
    roughnessPath.clear();
    albedoTexture = nullptr;
    normalTexture = nullptr;
    roughnessTexture = nullptr;

    std::ifstream file(fs::path(filepath), std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    albedoPath = NormalizeLinkedPath(ExtractJsonString(content, "albedo"));
    normalPath = NormalizeLinkedPath(ExtractJsonString(content, "normal"));
    roughnessPath = NormalizeLinkedPath(ExtractJsonString(content, "roughness"));

    std::error_code ec;
    lastWriteTime = fs::last_write_time(filepath, ec);
    if (ec) {
        lastWriteTime = {};
    }

    return true;
}

bool Material::SaveToFile(const std::wstring& filepath) const {
    std::error_code ec;
    fs::create_directories(fs::path(filepath).parent_path(), ec);

    std::ofstream file(fs::path(filepath), std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file << "{\n";
    file << "  \"type\": \"CatalystMaterial\",\n";
    file << "  \"version\": 1,\n";
    file << "  \"textures\": {\n";
    file << "    \"albedo\": \"" << EscapeJsonString(NormalizeLinkedPath(albedoPath)) << "\",\n";
    file << "    \"normal\": \"" << EscapeJsonString(NormalizeLinkedPath(normalPath)) << "\",\n";
    file << "    \"roughness\": \"" << EscapeJsonString(NormalizeLinkedPath(roughnessPath)) << "\"\n";
    file << "  }\n";
    file << "}\n";
    return true;
}

std::wstring Material::ResolveLinkedTexturePath(const std::string& linkedPath) const {
    if (linkedPath.empty() || sourcePath.empty()) {
        return L"";
    }

    fs::path linked(linkedPath);
    fs::path resolved = linked.is_absolute() ? linked : fs::path(sourcePath).parent_path() / linked;
    return resolved.lexically_normal().wstring();
}
