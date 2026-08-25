#include "PakFile.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <windows.h>

namespace fs = std::filesystem;

namespace CatalystPak {
namespace {
struct Header {
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t entryCount = 0;
};

struct IndexEntry {
    std::string logicalPathUtf8;
    uint64_t offset = 0;
    uint64_t size = 0;
};

constexpr uint32_t kPakMagic = 0x4B415043; // CPAK
constexpr uint32_t kPakVersion = 1;

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return "";
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return "";
    }

    std::string converted(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, converted.data(), size, nullptr, nullptr);
    converted.pop_back();
    return converted;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size <= 1) {
        return L"";
    }

    std::wstring converted(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, converted.data(), size);
    converted.pop_back();
    return converted;
}

template <typename T>
void WriteScalar(std::ostream& stream, const T& value) {
    stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
bool ReadScalar(std::istream& stream, T& outValue) {
    stream.read(reinterpret_cast<char*>(&outValue), sizeof(T));
    return stream.good();
}
}

std::wstring NormalizePath(const std::wstring& path) {
    std::wstring normalized = fs::path(path).lexically_normal().generic_wstring();
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), towlower);
    return normalized;
}

bool WritePakFile(const std::wstring& pakPath, const std::vector<EntryToWrite>& entries, std::wstring* outError) {
    try {
        std::error_code ec;
        fs::create_directories(fs::path(pakPath).parent_path(), ec);

        std::vector<IndexEntry> indexEntries;
        indexEntries.reserve(entries.size());
        uint64_t indexSize = 0;
        for (const EntryToWrite& entry : entries) {
            IndexEntry indexEntry;
            indexEntry.logicalPathUtf8 = WideToUtf8(NormalizePath(entry.logicalPath));
            indexEntry.size = static_cast<uint64_t>(entry.data.size());
            indexEntries.push_back(indexEntry);
            indexSize += sizeof(uint32_t) + indexEntry.logicalPathUtf8.size() + sizeof(uint64_t) + sizeof(uint64_t);
        }

        uint64_t dataOffset = sizeof(Header) + indexSize;
        for (size_t index = 0; index < indexEntries.size(); ++index) {
            indexEntries[index].offset = dataOffset;
            dataOffset += indexEntries[index].size;
        }

        std::ofstream outputFile(fs::path(pakPath), std::ios::binary | std::ios::trunc);
        if (!outputFile.is_open()) {
            if (outError != nullptr) {
                *outError = L"Could not open pak output.";
            }
            return false;
        }

        Header header;
        header.magic = kPakMagic;
        header.version = kPakVersion;
        header.entryCount = static_cast<uint32_t>(entries.size());
        WriteScalar(outputFile, header);

        for (const IndexEntry& indexEntry : indexEntries) {
            const uint32_t pathLength = static_cast<uint32_t>(indexEntry.logicalPathUtf8.size());
            WriteScalar(outputFile, pathLength);
            if (pathLength > 0) {
                outputFile.write(indexEntry.logicalPathUtf8.data(), pathLength);
            }
            WriteScalar(outputFile, indexEntry.offset);
            WriteScalar(outputFile, indexEntry.size);
        }

        for (const EntryToWrite& entry : entries) {
            if (!entry.data.empty()) {
                outputFile.write(reinterpret_cast<const char*>(entry.data.data()), static_cast<std::streamsize>(entry.data.size()));
            }
        }

        return true;
    } catch (...) {
        if (outError != nullptr) {
            *outError = L"Pak write failed.";
        }
        return false;
    }
}

bool ReadEntry(const std::wstring& pakPath, const std::wstring& logicalPath, std::vector<uint8_t>& outData, std::wstring* outError) {
    outData.clear();

    try {
    std::error_code sizeError;
    const std::uintmax_t fileSize = fs::file_size(fs::path(pakPath), sizeError);
    if (sizeError) {
        if (outError != nullptr) {
            *outError = L"Pak file could not be opened.";
        }
        return false;
    }

    std::ifstream inputFile(fs::path(pakPath), std::ios::binary);
    if (!inputFile.is_open()) {
        if (outError != nullptr) {
            *outError = L"Pak file could not be opened.";
        }
        return false;
    }

    Header header;
    if (!ReadScalar(inputFile, header) || header.magic != kPakMagic || header.version != kPakVersion) {
        if (outError != nullptr) {
            *outError = L"Pak header is invalid.";
        }
        return false;
    }

    const std::wstring targetPath = NormalizePath(logicalPath);
    for (uint32_t index = 0; index < header.entryCount; ++index) {
        uint32_t pathLength = 0;
        if (!ReadScalar(inputFile, pathLength)) {
            if (outError != nullptr) {
                *outError = L"Pak index is truncated.";
            }
            return false;
        }

        // Length and size fields come straight off disk, so sanity-check them
        // against the real file size before allocating anything from them.
        if (pathLength > fileSize) {
            if (outError != nullptr) {
                *outError = L"Pak index is corrupt.";
            }
            return false;
        }

        std::string pathUtf8(pathLength, '\0');
        if (pathLength > 0) {
            inputFile.read(pathUtf8.data(), pathLength);
            if (!inputFile.good()) {
                if (outError != nullptr) {
                    *outError = L"Pak path table is truncated.";
                }
                return false;
            }
        }

        uint64_t offset = 0;
        uint64_t size = 0;
        if (!ReadScalar(inputFile, offset) || !ReadScalar(inputFile, size)) {
            if (outError != nullptr) {
                *outError = L"Pak index entry is truncated.";
            }
            return false;
        }

        if (NormalizePath(Utf8ToWide(pathUtf8)) != targetPath) {
            continue;
        }

        if (offset > fileSize || size > fileSize - offset) {
            if (outError != nullptr) {
                *outError = L"Pak entry runs past the end of the file.";
            }
            return false;
        }

        outData.resize(static_cast<size_t>(size));
        inputFile.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (size > 0) {
            inputFile.read(reinterpret_cast<char*>(outData.data()), static_cast<std::streamsize>(size));
            if (!inputFile.good()) {
                outData.clear();
                if (outError != nullptr) {
                    *outError = L"Pak payload is truncated.";
                }
                return false;
            }
        }

        return true;
    }

    if (outError != nullptr) {
        *outError = L"Pak entry was not found.";
    }
    return false;
    } catch (...) {
        outData.clear();
        if (outError != nullptr) {
            *outError = L"Pak read failed.";
        }
        return false;
    }
}
}
