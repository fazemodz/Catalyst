#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace CatalystPak {
struct EntryToWrite {
    std::wstring logicalPath;
    std::vector<uint8_t> data;
};

std::wstring NormalizePath(const std::wstring& path);
bool WritePakFile(const std::wstring& pakPath, const std::vector<EntryToWrite>& entries, std::wstring* outError = nullptr);
bool ReadEntry(const std::wstring& pakPath, const std::wstring& logicalPath, std::vector<uint8_t>& outData, std::wstring* outError = nullptr);
}
