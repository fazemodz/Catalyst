#pragma once

#include <d3dcompiler.h>
#include <filesystem>
#include <string>
#include <vector>
#include <windows.h>

namespace CatalystRender {
namespace detail {
inline std::filesystem::path GetModuleDirectory() {
    wchar_t modulePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }

    return std::filesystem::path(modulePath).parent_path();
}

inline void AddAncestorRoots(std::vector<std::filesystem::path>& roots, const std::filesystem::path& start) {
    if (start.empty()) {
        return;
    }

    std::error_code ec;
    std::filesystem::path current = std::filesystem::absolute(start, ec);
    if (ec) {
        current = start;
    }

    while (!current.empty()) {
        roots.push_back(current);
        const std::filesystem::path parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
}
}

inline std::wstring ResolveShaderPath(const wchar_t* relativeShaderPath) {
    if (relativeShaderPath == nullptr || relativeShaderPath[0] == L'\0') {
        return L"";
    }

    const std::filesystem::path shaderPath(relativeShaderPath);
    std::error_code ec;
    if (shaderPath.is_absolute() && std::filesystem::exists(shaderPath, ec) && !ec) {
        return shaderPath.wstring();
    }

    std::vector<std::filesystem::path> roots;
    detail::AddAncestorRoots(roots, std::filesystem::current_path(ec));
    detail::AddAncestorRoots(roots, detail::GetModuleDirectory());

    for (const std::filesystem::path& root : roots) {
        const std::filesystem::path candidate = root / shaderPath;
        ec.clear();
        if (std::filesystem::exists(candidate, ec) && !ec) {
            return candidate.lexically_normal().wstring();
        }
    }

    return shaderPath.wstring();
}

inline HRESULT CompileShaderFromFile(const wchar_t* relativeShaderPath, const D3D_SHADER_MACRO* defines,
                                     ID3DInclude* includeHandler, LPCSTR entryPoint, LPCSTR target,
                                     UINT flags1, UINT flags2, ID3DBlob** code, ID3DBlob** errorMsgs) {
    const std::wstring resolvedPath = ResolveShaderPath(relativeShaderPath);
    return D3DCompileFromFile(resolvedPath.c_str(), defines, includeHandler, entryPoint, target,
                              flags1, flags2, code, errorMsgs);
}
}
