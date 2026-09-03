#include "FastObjParser.h"
#include "PolygonTriangulate.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <thread>

namespace CatalystImport {
namespace {

// Reading a multi-hundred-megabyte OBJ through iostreams costs more than the
// parse does. Mapping it lets every worker thread read straight out of the
// page cache with no copy and no locking.
class MappedFile {
public:
    ~MappedFile() {
        if (m_data != nullptr) {
            UnmapViewOfFile(m_data);
        }
        if (m_mapping != nullptr) {
            CloseHandle(m_mapping);
        }
        if (m_file != INVALID_HANDLE_VALUE) {
            CloseHandle(m_file);
        }
    }

    bool Open(const std::wstring& path, std::string* outError) {
        m_file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (m_file == INVALID_HANDLE_VALUE) {
            if (outError != nullptr) {
                *outError = "Could not open the model file for reading.";
            }
            return false;
        }

        LARGE_INTEGER size = {};
        if (!GetFileSizeEx(m_file, &size) || size.QuadPart <= 0) {
            if (outError != nullptr) {
                *outError = "The model file is empty.";
            }
            return false;
        }
        m_size = static_cast<size_t>(size.QuadPart);

        m_mapping = CreateFileMappingW(m_file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (m_mapping == nullptr) {
            if (outError != nullptr) {
                *outError = "Could not map the model file into memory.";
            }
            return false;
        }

        m_data = static_cast<const char*>(MapViewOfFile(m_mapping, FILE_MAP_READ, 0, 0, 0));
        if (m_data == nullptr) {
            if (outError != nullptr) {
                *outError = "Could not create a view of the model file.";
            }
            return false;
        }
        return true;
    }

    const char* Data() const { return m_data; }
    size_t Size() const { return m_size; }

private:
    HANDLE m_file = INVALID_HANDLE_VALUE;
    HANDLE m_mapping = nullptr;
    const char* m_data = nullptr;
    size_t m_size = 0;
};

constexpr double kPow10Table[] = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
};
constexpr int kPow10TableSize = static_cast<int>(sizeof(kPow10Table) / sizeof(kPow10Table[0]));

// Past this the 64-bit mantissa accumulator would wrap, so extra digits are
// dropped. A float only carries 24 bits of mantissa anyway.
constexpr uint64_t kMantissaLimit = 1000000000000000000ull;

inline double Pow10(int exponent) {
    if (exponent >= 0 && exponent < kPow10TableSize) {
        return kPow10Table[exponent];
    }
    return std::pow(10.0, static_cast<double>(exponent));
}

inline bool IsBlank(char c) { return c == ' ' || c == '\t' || c == '\r'; }
inline bool IsDigit(char c) { return c >= '0' && c <= '9'; }

// A locale-free float scanner. strtof consults the locale on every call and
// dominates the profile on large files; this does not.
inline float ParseFloat(const char*& cursor, const char* end) {
    while (cursor < end && IsBlank(*cursor)) {
        ++cursor;
    }

    bool negative = false;
    if (cursor < end && (*cursor == '+' || *cursor == '-')) {
        negative = (*cursor == '-');
        ++cursor;
    }

    uint64_t mantissa = 0;
    int fractionDigits = 0;
    while (cursor < end && IsDigit(*cursor)) {
        if (mantissa < kMantissaLimit) {
            mantissa = mantissa * 10ull + static_cast<uint64_t>(*cursor - '0');
        }
        ++cursor;
    }

    if (cursor < end && *cursor == '.') {
        ++cursor;
        while (cursor < end && IsDigit(*cursor)) {
            if (mantissa < kMantissaLimit) {
                mantissa = mantissa * 10ull + static_cast<uint64_t>(*cursor - '0');
                ++fractionDigits;
            }
            ++cursor;
        }
    }

    double value = static_cast<double>(mantissa);
    if (fractionDigits > 0) {
        value /= Pow10(fractionDigits);
    }

    if (cursor < end && (*cursor == 'e' || *cursor == 'E')) {
        ++cursor;
        bool exponentNegative = false;
        if (cursor < end && (*cursor == '+' || *cursor == '-')) {
            exponentNegative = (*cursor == '-');
            ++cursor;
        }
        int exponent = 0;
        while (cursor < end && IsDigit(*cursor)) {
            exponent = exponent * 10 + (*cursor - '0');
            if (exponent > 400) {
                exponent = 400;
            }
            ++cursor;
        }
        value = exponentNegative ? value / Pow10(exponent) : value * Pow10(exponent);
    }

    return static_cast<float>(negative ? -value : value);
}

// Returns the raw OBJ index: positive is 1-based, negative counts back from
// the end, zero means the slot was omitted.
inline int64_t ParseRawIndex(const char*& cursor, const char* end) {
    bool negative = false;
    if (cursor < end && (*cursor == '-' || *cursor == '+')) {
        negative = (*cursor == '-');
        ++cursor;
    }

    int64_t value = 0;
    bool sawDigit = false;
    while (cursor < end && IsDigit(*cursor)) {
        value = value * 10 + (*cursor - '0');
        sawDigit = true;
        ++cursor;
    }

    if (!sawDigit) {
        return 0;
    }
    return negative ? -value : value;
}

inline int32_t ResolveIndex(int64_t rawIndex, size_t elementsSoFar) {
    if (rawIndex > 0) {
        return static_cast<int32_t>(rawIndex - 1);
    }
    if (rawIndex < 0) {
        return static_cast<int32_t>(static_cast<int64_t>(elementsSoFar) + rawIndex);
    }
    return -1;
}

inline void SkipToNextLine(const char*& cursor, const char* end) {
    while (cursor < end && *cursor != '\n') {
        ++cursor;
    }
    if (cursor < end) {
        ++cursor;
    }
}

// Counts whitespace-separated tokens on the rest of the line without
// interpreting them. Used to size the output arrays exactly.
inline size_t CountTokensOnLine(const char* cursor, const char* end) {
    size_t tokens = 0;
    while (cursor < end && *cursor != '\n') {
        while (cursor < end && IsBlank(*cursor)) {
            ++cursor;
        }
        if (cursor >= end || *cursor == '\n') {
            break;
        }
        ++tokens;
        while (cursor < end && !IsBlank(*cursor) && *cursor != '\n') {
            ++cursor;
        }
    }
    return tokens;
}

// A slice of the file that starts and ends on a line boundary, plus the
// element counts the first pass found in it.
struct Chunk {
    size_t begin = 0;
    size_t end = 0;

    size_t positions = 0;
    size_t texcoords = 0;
    size_t normals = 0;
    size_t triangleCorners = 0;
    bool hasColors = false;

    size_t positionBase = 0;
    size_t texcoordBase = 0;
    size_t normalBase = 0;
    size_t cornerBase = 0;
};

// Attributes are parsed before faces so that by the time a face has to pick a
// diagonal for a quad, every position in the file has been written - including
// the ones a different worker was responsible for.
enum class ParsePhase {
    Attributes,
    Faces
};

void CountChunk(const char* data, Chunk& chunk) {
    const char* cursor = data + chunk.begin;
    const char* const end = data + chunk.end;

    while (cursor < end) {
        while (cursor < end && IsBlank(*cursor)) {
            ++cursor;
        }
        if (cursor >= end) {
            break;
        }

        const char first = *cursor;
        const char second = (cursor + 1 < end) ? *(cursor + 1) : '\0';

        if (first == 'v') {
            if (second == ' ' || second == '\t') {
                ++chunk.positions;
                // x y z r g b - six or more floats means the exporter wrote
                // per-vertex colours, which scans and point clouds rely on.
                if (!chunk.hasColors && CountTokensOnLine(cursor + 1, end) >= 6) {
                    chunk.hasColors = true;
                }
            } else if (second == 't') {
                ++chunk.texcoords;
            } else if (second == 'n') {
                ++chunk.normals;
            }
        } else if (first == 'f' && (second == ' ' || second == '\t')) {
            const size_t corners = CountTokensOnLine(cursor + 1, end);
            if (corners >= 3) {
                chunk.triangleCorners += (corners - 2) * 3;
            }
        }

        SkipToNextLine(cursor, end);
    }
}

void ParseChunk(const char* data, const Chunk& chunk, RawObjData& out, bool wantColors, ParsePhase phase) {
    const char* cursor = data + chunk.begin;
    const char* const end = data + chunk.end;

    size_t positionCursor = chunk.positionBase;
    size_t texcoordCursor = chunk.texcoordBase;
    size_t normalCursor = chunk.normalBase;
    size_t cornerCursor = chunk.cornerBase;

    const size_t positionCount = out.Positions.size() / 3;

    // Reused across faces so an n-gon-heavy file does not allocate per line.
    std::vector<ObjCorner> polygon;
    polygon.reserve(16);

    while (cursor < end) {
        while (cursor < end && IsBlank(*cursor)) {
            ++cursor;
        }
        if (cursor >= end) {
            break;
        }

        const char first = *cursor;
        const char second = (cursor + 1 < end) ? *(cursor + 1) : '\0';
        const bool isPosition = (first == 'v') && (second == ' ' || second == '\t');
        const bool isTexcoord = (first == 'v') && (second == 't');
        const bool isNormal = (first == 'v') && (second == 'n');
        const bool isFace = (first == 'f') && (second == ' ' || second == '\t');

        if (phase == ParsePhase::Faces) {
            // Attributes were written by the first phase. They still have to be
            // counted here, because a negative face index is relative to how
            // many elements have been seen so far.
            if (isPosition) {
                ++positionCursor;
            } else if (isTexcoord) {
                ++texcoordCursor;
            } else if (isNormal) {
                ++normalCursor;
            } else if (isFace) {
                polygon.clear();
                const char* line = cursor + 1;
                while (line < end && *line != '\n') {
                    while (line < end && IsBlank(*line)) {
                        ++line;
                    }
                    if (line >= end || *line == '\n') {
                        break;
                    }

                    const int64_t rawPosition = ParseRawIndex(line, end);
                    int64_t rawTexcoord = 0;
                    int64_t rawNormal = 0;
                    if (line < end && *line == '/') {
                        ++line;
                        if (line < end && *line != '/') {
                            rawTexcoord = ParseRawIndex(line, end);
                        }
                        if (line < end && *line == '/') {
                            ++line;
                            rawNormal = ParseRawIndex(line, end);
                        }
                    }

                    ObjCorner corner;
                    corner.position = ResolveIndex(rawPosition, positionCursor);
                    corner.texcoord = ResolveIndex(rawTexcoord, texcoordCursor);
                    corner.normal = ResolveIndex(rawNormal, normalCursor);
                    polygon.push_back(corner);

                    // Tolerate trailing junk inside a token rather than losing sync.
                    while (line < end && !IsBlank(*line) && *line != '\n') {
                        ++line;
                    }
                }

                if (polygon.size() >= 3) {
                    ObjCorner* target = out.Corners.data() + cornerCursor;
                    bool fanned = true;

                    if (polygon.size() == 4) {
                        // Split a quad along its shorter diagonal. Fanning from
                        // corner 0 regardless would carve slivers out of any
                        // quad that is not roughly square, and would fold a
                        // non-planar one the wrong way.
                        bool indicesValid = true;
                        for (int corner = 0; corner < 4; ++corner) {
                            const int32_t position = polygon[corner].position;
                            if (position < 0 || static_cast<size_t>(position) >= positionCount) {
                                indicesValid = false;
                                break;
                            }
                        }

                        if (indicesValid) {
                            const float* p0 = out.Positions.data() + static_cast<size_t>(polygon[0].position) * 3;
                            const float* p1 = out.Positions.data() + static_cast<size_t>(polygon[1].position) * 3;
                            const float* p2 = out.Positions.data() + static_cast<size_t>(polygon[2].position) * 3;
                            const float* p3 = out.Positions.data() + static_cast<size_t>(polygon[3].position) * 3;

                            if (!QuadIsConvex(p0, p1, p2, p3)) {
                                // Only one diagonal of a concave quad stays
                                // inside it, and it is not always the shorter
                                // one. Ear clipping finds the right one.
                                fanned = !TriangulatePolygon(polygon, out.Positions, positionCount, target);
                            } else {
                                const float d02x = p2[0] - p0[0];
                                const float d02y = p2[1] - p0[1];
                                const float d02z = p2[2] - p0[2];
                                const float d13x = p3[0] - p1[0];
                                const float d13y = p3[1] - p1[1];
                                const float d13z = p3[2] - p1[2];

                                const float squared02 = d02x * d02x + d02y * d02y + d02z * d02z;
                                const float squared13 = d13x * d13x + d13y * d13y + d13z * d13z;

                                if (squared02 < squared13) {
                                    *target++ = polygon[0];
                                    *target++ = polygon[1];
                                    *target++ = polygon[2];
                                    *target++ = polygon[0];
                                    *target++ = polygon[2];
                                    *target++ = polygon[3];
                                } else {
                                    *target++ = polygon[0];
                                    *target++ = polygon[1];
                                    *target++ = polygon[3];
                                    *target++ = polygon[1];
                                    *target++ = polygon[2];
                                    *target++ = polygon[3];
                                }
                                fanned = false;
                            }
                        }
                    } else if (polygon.size() >= 5) {
                        fanned = !TriangulatePolygon(polygon, out.Positions, positionCount, target);
                    }

                    if (fanned) {
                        for (size_t apex = 1; apex + 1 < polygon.size(); ++apex) {
                            *target++ = polygon[0];
                            *target++ = polygon[apex];
                            *target++ = polygon[apex + 1];
                        }
                    }

                    cornerCursor += (polygon.size() - 2) * 3;
                }
            }

            SkipToNextLine(cursor, end);
            continue;
        }

        if (isPosition) {
            const char* line = cursor + 1;
            const float x = ParseFloat(line, end);
            const float y = ParseFloat(line, end);
            const float z = ParseFloat(line, end);
            float* target = out.Positions.data() + positionCursor * 3;
            target[0] = x;
            target[1] = y;
            target[2] = z;

            if (wantColors) {
                const char* colorScan = line;
                while (colorScan < end && IsBlank(*colorScan)) {
                    ++colorScan;
                }
                // Lines without colour channels keep the white default.
                if (colorScan < end && *colorScan != '\n') {
                    const float red = ParseFloat(colorScan, end);
                    const float green = ParseFloat(colorScan, end);
                    const float blue = ParseFloat(colorScan, end);
                    float* colorTarget = out.Colors.data() + positionCursor * 3;
                    colorTarget[0] = red;
                    colorTarget[1] = green;
                    colorTarget[2] = blue;
                }
            }
            ++positionCursor;
        } else if (isTexcoord) {
            const char* line = cursor + 2;
            const float u = ParseFloat(line, end);
            const float v = ParseFloat(line, end);
            float* target = out.Texcoords.data() + texcoordCursor * 2;
            target[0] = u;
            target[1] = v;
            ++texcoordCursor;
        } else if (isNormal) {
            const char* line = cursor + 2;
            const float x = ParseFloat(line, end);
            const float y = ParseFloat(line, end);
            const float z = ParseFloat(line, end);
            float* target = out.Normals.data() + normalCursor * 3;
            target[0] = x;
            target[1] = y;
            target[2] = z;
            ++normalCursor;
        }

        SkipToNextLine(cursor, end);
    }
}

// Splits the mapped file so every chunk starts just after a newline. Lines
// therefore never straddle two workers.
std::vector<Chunk> BuildChunks(const char* data, size_t size, size_t chunkCount) {
    std::vector<Chunk> chunks;
    chunks.reserve(chunkCount);

    size_t previousBegin = 0;
    for (size_t index = 0; index < chunkCount; ++index) {
        size_t chunkEnd = size;
        if (index + 1 < chunkCount) {
            chunkEnd = size * (index + 1) / chunkCount;
            while (chunkEnd < size && data[chunkEnd] != '\n') {
                ++chunkEnd;
            }
            if (chunkEnd < size) {
                ++chunkEnd;
            }
        }
        if (chunkEnd < previousBegin) {
            chunkEnd = previousBegin;
        }

        Chunk chunk;
        chunk.begin = previousBegin;
        chunk.end = chunkEnd;
        chunks.push_back(chunk);
        previousBegin = chunkEnd;
    }

    return chunks;
}

void RunParallel(size_t count, const std::function<void(size_t)>& body) {
    if (count <= 1) {
        for (size_t index = 0; index < count; ++index) {
            body(index);
        }
        return;
    }

    std::vector<std::thread> workers;
    workers.reserve(count - 1);
    for (size_t index = 1; index < count; ++index) {
        workers.emplace_back(body, index);
    }
    body(0);
    for (std::thread& worker : workers) {
        worker.join();
    }
}

}

bool ParseObjFile(const std::wstring& path, RawObjData& out, std::string* outError) {
    out = RawObjData();

    MappedFile file;
    if (!file.Open(path, outError)) {
        return false;
    }

    const char* const data = file.Data();
    const size_t size = file.Size();

    // Threading only pays for itself once there is real work to split.
    const unsigned int hardwareThreads = (std::max)(1u, std::thread::hardware_concurrency());
    const size_t chunkCount = (size < (1u << 20)) ? 1u : static_cast<size_t>(hardwareThreads);

    std::vector<Chunk> chunks = BuildChunks(data, size, chunkCount);

    RunParallel(chunks.size(), [&](size_t index) { CountChunk(data, chunks[index]); });

    size_t totalPositions = 0;
    size_t totalTexcoords = 0;
    size_t totalNormals = 0;
    size_t totalCorners = 0;
    bool anyColors = false;
    for (Chunk& chunk : chunks) {
        chunk.positionBase = totalPositions;
        chunk.texcoordBase = totalTexcoords;
        chunk.normalBase = totalNormals;
        chunk.cornerBase = totalCorners;

        totalPositions += chunk.positions;
        totalTexcoords += chunk.texcoords;
        totalNormals += chunk.normals;
        totalCorners += chunk.triangleCorners;
        anyColors = anyColors || chunk.hasColors;
    }

    if (totalCorners == 0 || totalPositions == 0) {
        if (outError != nullptr) {
            *outError = "The model file contains no triangles.";
        }
        return false;
    }

    out.Positions.resize(totalPositions * 3);
    out.Texcoords.resize(totalTexcoords * 2);
    out.Normals.resize(totalNormals * 3);
    out.Corners.resize(totalCorners);
    if (anyColors) {
        out.Colors.assign(totalPositions * 3, 1.0f);
    }

    RunParallel(chunks.size(), [&](size_t index) {
        ParseChunk(data, chunks[index], out, anyColors, ParsePhase::Attributes);
    });
    RunParallel(chunks.size(), [&](size_t index) {
        ParseChunk(data, chunks[index], out, anyColors, ParsePhase::Faces);
    });
    return true;
}

}
