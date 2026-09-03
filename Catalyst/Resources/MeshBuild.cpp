#define NOMINMAX
#include "MeshBuild.h"
#include "MeshletBuilder.h"
#include "FbxParser.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <thread>

using namespace DirectX;
namespace fs = std::filesystem;

namespace CatalystImport {
namespace {

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

// Splits [0, total) across the hardware threads and hands each worker its own
// half-open range. Used for the per-vertex passes, which have no shared state.
void ForEachRange(size_t total, const std::function<void(size_t, size_t)>& body) {
    if (total == 0) {
        return;
    }

    const unsigned int hardwareThreads = (std::max)(1u, std::thread::hardware_concurrency());
    size_t workerCount = static_cast<size_t>(hardwareThreads);
    if (total < 32768) {
        workerCount = 1;
    }
    workerCount = (std::min)(workerCount, total);

    RunParallel(workerCount, [&](size_t worker) {
        const size_t begin = total * worker / workerCount;
        const size_t end = total * (worker + 1) / workerCount;
        if (begin < end) {
            body(begin, end);
        }
    });
}

inline XMFLOAT3 Normalized(const XMFLOAT3& value, const XMFLOAT3& fallback) {
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z;
    if (lengthSquared <= 1e-20f) {
        return fallback;
    }
    const float inverseLength = 1.0f / sqrtf(lengthSquared);
    return {value.x * inverseLength, value.y * inverseLength, value.z * inverseLength};
}

// ---------------------------------------------------------------------------
//  Corner welding
// ---------------------------------------------------------------------------

// Assembling vertices through a hash map costs more than the parse on a
// multi-million-triangle mesh. Bucketing by position index instead gives a
// chain that is almost always one or two links long, with no hashing at all.
struct WeldTable {
    std::vector<int32_t> chainHead;
    std::vector<int32_t> chainNext;
    std::vector<ObjCorner> emitted;

    void Reset(size_t positionCount, size_t expectedVertices) {
        chainHead.assign(positionCount, -1);
        chainNext.clear();
        emitted.clear();
        chainNext.reserve(expectedVertices);
        emitted.reserve(expectedVertices);
    }

    // Returns the existing vertex for this corner, or -1 when it is new.
    int32_t Find(const ObjCorner& corner) const {
        for (int32_t candidate = chainHead[corner.position]; candidate >= 0; candidate = chainNext[candidate]) {
            const ObjCorner& existing = emitted[candidate];
            if (existing.texcoord == corner.texcoord && existing.normal == corner.normal) {
                return candidate;
            }
        }
        return -1;
    }

    void Insert(const ObjCorner& corner, int32_t vertexIndex) {
        chainNext.push_back(chainHead[corner.position]);
        emitted.push_back(corner);
        chainHead[corner.position] = vertexIndex;
    }
};

// ---------------------------------------------------------------------------
//  Post-processing passes
// ---------------------------------------------------------------------------

void ApplyTransform(MeshData& mesh, const MeshImportOptions& options) {
    const bool convertAxis = (options.upAxis == MeshUpAxis::Z);
    const float scale = options.uniformScale;
    const bool scaleChanges = (scale != 1.0f);

    if (convertAxis || scaleChanges) {
        Vertex* vertices = mesh.Vertices.data();
        ForEachRange(mesh.Vertices.size(), [&](size_t begin, size_t end) {
            for (size_t index = begin; index < end; ++index) {
                Vertex& vertex = vertices[index];
                if (convertAxis) {
                    // Z-up right-handed into Y-up: rotate -90 degrees about X.
                    const XMFLOAT3 position = vertex.position;
                    vertex.position = {position.x, position.z, -position.y};
                    const XMFLOAT3 normal = vertex.normal;
                    vertex.normal = {normal.x, normal.z, -normal.y};
                }
                if (scaleChanges) {
                    vertex.position.x *= scale;
                    vertex.position.y *= scale;
                    vertex.position.z *= scale;
                }
            }
        });
    }

    if (!options.centerPivot || mesh.Vertices.empty()) {
        return;
    }

    XMFLOAT3 minimum = mesh.Vertices[0].position;
    XMFLOAT3 maximum = minimum;
    for (const Vertex& vertex : mesh.Vertices) {
        minimum.x = (std::min)(minimum.x, vertex.position.x);
        minimum.y = (std::min)(minimum.y, vertex.position.y);
        minimum.z = (std::min)(minimum.z, vertex.position.z);
        maximum.x = (std::max)(maximum.x, vertex.position.x);
        maximum.y = (std::max)(maximum.y, vertex.position.y);
        maximum.z = (std::max)(maximum.z, vertex.position.z);
    }

    const XMFLOAT3 center = {
        (minimum.x + maximum.x) * 0.5f,
        (minimum.y + maximum.y) * 0.5f,
        (minimum.z + maximum.z) * 0.5f
    };

    Vertex* vertices = mesh.Vertices.data();
    ForEachRange(mesh.Vertices.size(), [&](size_t begin, size_t end) {
        for (size_t index = begin; index < end; ++index) {
            vertices[index].position.x -= center.x;
            vertices[index].position.y -= center.y;
            vertices[index].position.z -= center.z;
        }
    });
}

void FlipWinding(MeshData& mesh) {
    uint32_t* indices = mesh.Indices.data();
    const size_t triangleCount = mesh.Indices.size() / 3;
    ForEachRange(triangleCount, [&](size_t begin, size_t end) {
        for (size_t triangle = begin; triangle < end; ++triangle) {
            std::swap(indices[triangle * 3 + 1], indices[triangle * 3 + 2]);
        }
    });
}

// Cross product of the two triangle edges. Its length is twice the triangle
// area, so accumulating it unnormalised area-weights the result for free.
inline XMFLOAT3 FaceNormal(const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT3& c) {
    const float e1x = b.x - a.x;
    const float e1y = b.y - a.y;
    const float e1z = b.z - a.z;
    const float e2x = c.x - a.x;
    const float e2y = c.y - a.y;
    const float e2z = c.z - a.z;
    return {
        e1y * e2z - e1z * e2y,
        e1z * e2x - e1x * e2z,
        e1x * e2y - e1y * e2x
    };
}

void GenerateFlatNormals(MeshData& mesh) {
    const size_t triangleCount = mesh.Indices.size() / 3;
    Vertex* vertices = mesh.Vertices.data();
    const uint32_t* indices = mesh.Indices.data();

    ForEachRange(triangleCount, [&](size_t begin, size_t end) {
        for (size_t triangle = begin; triangle < end; ++triangle) {
            const uint32_t i0 = indices[triangle * 3 + 0];
            const uint32_t i1 = indices[triangle * 3 + 1];
            const uint32_t i2 = indices[triangle * 3 + 2];
            const XMFLOAT3 normal = Normalized(
                FaceNormal(vertices[i0].position, vertices[i1].position, vertices[i2].position),
                {0.0f, 1.0f, 0.0f});
            vertices[i0].normal = normal;
            vertices[i1].normal = normal;
            vertices[i2].normal = normal;
        }
    });
}

// Averages face normals across every vertex that came from the same authored
// position, so UV seams do not turn into shading seams. When onlyMissing is
// set the result is written back solely to vertices the file left unshaded.
void GenerateSmoothNormals(MeshData& mesh, const std::vector<int32_t>& vertexSourcePosition, size_t positionCount, bool onlyMissing) {
    if (positionCount == 0 || mesh.Indices.size() < 3) {
        return;
    }

    std::vector<XMFLOAT3> accumulated(positionCount, XMFLOAT3{0.0f, 0.0f, 0.0f});
    const Vertex* vertices = mesh.Vertices.data();
    const uint32_t* indices = mesh.Indices.data();
    const size_t triangleCount = mesh.Indices.size() / 3;

    // Neighbouring triangles share positions, so this accumulation stays
    // single-threaded rather than paying for atomics on every add.
    for (size_t triangle = 0; triangle < triangleCount; ++triangle) {
        const uint32_t i0 = indices[triangle * 3 + 0];
        const uint32_t i1 = indices[triangle * 3 + 1];
        const uint32_t i2 = indices[triangle * 3 + 2];
        const XMFLOAT3 normal = FaceNormal(vertices[i0].position, vertices[i1].position, vertices[i2].position);

        const uint32_t triangleIndices[3] = {i0, i1, i2};
        for (const uint32_t vertexIndex : triangleIndices) {
            const int32_t position = vertexSourcePosition[vertexIndex];
            if (position < 0 || static_cast<size_t>(position) >= positionCount) {
                continue;
            }
            accumulated[position].x += normal.x;
            accumulated[position].y += normal.y;
            accumulated[position].z += normal.z;
        }
    }

    Vertex* writableVertices = mesh.Vertices.data();
    ForEachRange(mesh.Vertices.size(), [&](size_t begin, size_t end) {
        for (size_t index = begin; index < end; ++index) {
            Vertex& vertex = writableVertices[index];
            if (onlyMissing) {
                const float lengthSquared = vertex.normal.x * vertex.normal.x +
                                            vertex.normal.y * vertex.normal.y +
                                            vertex.normal.z * vertex.normal.z;
                if (lengthSquared > 1e-12f) {
                    continue;
                }
            }
            const int32_t position = vertexSourcePosition[index];
            if (position < 0 || static_cast<size_t>(position) >= positionCount) {
                vertex.normal = {0.0f, 1.0f, 0.0f};
                continue;
            }
            vertex.normal = Normalized(accumulated[position], {0.0f, 1.0f, 0.0f});
        }
    });
}

void NormalizeNormals(MeshData& mesh) {
    Vertex* vertices = mesh.Vertices.data();
    ForEachRange(mesh.Vertices.size(), [&](size_t begin, size_t end) {
        for (size_t index = begin; index < end; ++index) {
            vertices[index].normal = Normalized(vertices[index].normal, {0.0f, 1.0f, 0.0f});
        }
    });
}

void GenerateTangents(MeshData& mesh) {
    if (mesh.Vertices.empty() || mesh.Indices.size() < 3) {
        return;
    }

    std::vector<XMFLOAT3> accumulated(mesh.Vertices.size(), XMFLOAT3{0.0f, 0.0f, 0.0f});
    const Vertex* vertices = mesh.Vertices.data();
    const uint32_t* indices = mesh.Indices.data();
    const size_t triangleCount = mesh.Indices.size() / 3;

    for (size_t triangle = 0; triangle < triangleCount; ++triangle) {
        const uint32_t i0 = indices[triangle * 3 + 0];
        const uint32_t i1 = indices[triangle * 3 + 1];
        const uint32_t i2 = indices[triangle * 3 + 2];

        const XMFLOAT3& p0 = vertices[i0].position;
        const XMFLOAT3& p1 = vertices[i1].position;
        const XMFLOAT3& p2 = vertices[i2].position;
        const XMFLOAT2& uv0 = vertices[i0].uv;
        const XMFLOAT2& uv1 = vertices[i1].uv;
        const XMFLOAT2& uv2 = vertices[i2].uv;

        const float e1x = p1.x - p0.x;
        const float e1y = p1.y - p0.y;
        const float e1z = p1.z - p0.z;
        const float e2x = p2.x - p0.x;
        const float e2y = p2.y - p0.y;
        const float e2z = p2.z - p0.z;

        const float du1 = uv1.x - uv0.x;
        const float dv1 = uv1.y - uv0.y;
        const float du2 = uv2.x - uv0.x;
        const float dv2 = uv2.y - uv0.y;

        // A degenerate UV triangle has no tangent frame; leave it to the
        // per-vertex fallback below rather than dividing by zero.
        const float determinant = du1 * dv2 - du2 * dv1;
        if (fabsf(determinant) < 1e-12f) {
            continue;
        }

        const float inverse = 1.0f / determinant;
        const XMFLOAT3 tangent = {
            (e1x * dv2 - e2x * dv1) * inverse,
            (e1y * dv2 - e2y * dv1) * inverse,
            (e1z * dv2 - e2z * dv1) * inverse
        };

        const uint32_t triangleIndices[3] = {i0, i1, i2};
        for (const uint32_t vertexIndex : triangleIndices) {
            accumulated[vertexIndex].x += tangent.x;
            accumulated[vertexIndex].y += tangent.y;
            accumulated[vertexIndex].z += tangent.z;
        }
    }

    Vertex* writableVertices = mesh.Vertices.data();
    ForEachRange(mesh.Vertices.size(), [&](size_t begin, size_t end) {
        for (size_t index = begin; index < end; ++index) {
            Vertex& vertex = writableVertices[index];
            const XMFLOAT3& normal = vertex.normal;
            XMFLOAT3 tangent = accumulated[index];

            // Gram-Schmidt: drop the part of the tangent along the normal.
            const float projection = tangent.x * normal.x + tangent.y * normal.y + tangent.z * normal.z;
            tangent.x -= normal.x * projection;
            tangent.y -= normal.y * projection;
            tangent.z -= normal.z * projection;

            const float lengthSquared = tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z;
            if (lengthSquared <= 1e-16f) {
                // No usable UV gradient: any perpendicular will do, so pick the
                // axis the normal leans on least.
                const XMFLOAT3 axis = (fabsf(normal.x) < 0.9f) ? XMFLOAT3{1.0f, 0.0f, 0.0f} : XMFLOAT3{0.0f, 1.0f, 0.0f};
                tangent = {
                    normal.y * axis.z - normal.z * axis.y,
                    normal.z * axis.x - normal.x * axis.z,
                    normal.x * axis.y - normal.y * axis.x
                };
            }

            vertex.tangent = Normalized(tangent, {1.0f, 0.0f, 0.0f});
        }
    });
}

// ---------------------------------------------------------------------------
//  Spatial reorder
// ---------------------------------------------------------------------------

inline uint32_t SpreadBits(uint32_t value) {
    value &= 0x000003FFu;
    value = (value | (value << 16)) & 0x030000FFu;
    value = (value | (value << 8)) & 0x0300F00Fu;
    value = (value | (value << 4)) & 0x030C30C3u;
    value = (value | (value << 2)) & 0x09249249u;
    return value;
}

inline uint32_t MortonCode(uint32_t x, uint32_t y, uint32_t z) {
    return (SpreadBits(x) << 2) | (SpreadBits(y) << 1) | SpreadBits(z);
}

// The renderer packs runs of consecutive triangles into meshlets and culls by
// their bounding spheres, so triangle order decides how tight those spheres
// are. Sorting by centroid Morton code makes each meshlet a compact blob
// instead of a shell scattered across the model, which is what makes the
// GPU-driven path worth anything on dense meshes. Remapping the vertices into
// first-use order afterwards keeps the vertex fetches sequential too.
void SpatialOptimize(MeshData& mesh) {
    const size_t triangleCount = mesh.Indices.size() / 3;
    if (triangleCount < 2 || mesh.Vertices.empty()) {
        return;
    }

    XMFLOAT3 minimum = mesh.Vertices[0].position;
    XMFLOAT3 maximum = minimum;
    for (const Vertex& vertex : mesh.Vertices) {
        minimum.x = (std::min)(minimum.x, vertex.position.x);
        minimum.y = (std::min)(minimum.y, vertex.position.y);
        minimum.z = (std::min)(minimum.z, vertex.position.z);
        maximum.x = (std::max)(maximum.x, vertex.position.x);
        maximum.y = (std::max)(maximum.y, vertex.position.y);
        maximum.z = (std::max)(maximum.z, vertex.position.z);
    }

    const float extentX = (std::max)(maximum.x - minimum.x, 1e-6f);
    const float extentY = (std::max)(maximum.y - minimum.y, 1e-6f);
    const float extentZ = (std::max)(maximum.z - minimum.z, 1e-6f);
    constexpr float kGridResolution = 1023.0f;

    // Morton code in the high half, original triangle index in the low half,
    // so a plain 64-bit sort orders the triangles and stays stable.
    std::vector<uint64_t> sortKeys(triangleCount);
    const Vertex* vertices = mesh.Vertices.data();
    const uint32_t* indices = mesh.Indices.data();

    ForEachRange(triangleCount, [&](size_t begin, size_t end) {
        for (size_t triangle = begin; triangle < end; ++triangle) {
            const XMFLOAT3& p0 = vertices[indices[triangle * 3 + 0]].position;
            const XMFLOAT3& p1 = vertices[indices[triangle * 3 + 1]].position;
            const XMFLOAT3& p2 = vertices[indices[triangle * 3 + 2]].position;

            const float centroidX = (p0.x + p1.x + p2.x) * (1.0f / 3.0f);
            const float centroidY = (p0.y + p1.y + p2.y) * (1.0f / 3.0f);
            const float centroidZ = (p0.z + p1.z + p2.z) * (1.0f / 3.0f);

            const uint32_t gridX = static_cast<uint32_t>(std::clamp((centroidX - minimum.x) / extentX, 0.0f, 1.0f) * kGridResolution);
            const uint32_t gridY = static_cast<uint32_t>(std::clamp((centroidY - minimum.y) / extentY, 0.0f, 1.0f) * kGridResolution);
            const uint32_t gridZ = static_cast<uint32_t>(std::clamp((centroidZ - minimum.z) / extentZ, 0.0f, 1.0f) * kGridResolution);

            sortKeys[triangle] = (static_cast<uint64_t>(MortonCode(gridX, gridY, gridZ)) << 32) |
                                 static_cast<uint64_t>(triangle);
        }
    });

    std::sort(sortKeys.begin(), sortKeys.end());

    std::vector<uint32_t> reorderedIndices(mesh.Indices.size());
    for (size_t slot = 0; slot < triangleCount; ++slot) {
        const size_t source = static_cast<size_t>(sortKeys[slot] & 0xFFFFFFFFull);
        reorderedIndices[slot * 3 + 0] = indices[source * 3 + 0];
        reorderedIndices[slot * 3 + 1] = indices[source * 3 + 1];
        reorderedIndices[slot * 3 + 2] = indices[source * 3 + 2];
    }
    mesh.Indices.swap(reorderedIndices);

    // First-use remap. This also drops any vertex no triangle references.
    constexpr uint32_t kUnassigned = 0xFFFFFFFFu;
    std::vector<uint32_t> remap(mesh.Vertices.size(), kUnassigned);
    std::vector<Vertex> reorderedVertices;
    reorderedVertices.reserve(mesh.Vertices.size());

    for (uint32_t& index : mesh.Indices) {
        uint32_t& mapped = remap[index];
        if (mapped == kUnassigned) {
            mapped = static_cast<uint32_t>(reorderedVertices.size());
            reorderedVertices.push_back(mesh.Vertices[index]);
        }
        index = mapped;
    }

    mesh.Vertices.swap(reorderedVertices);
}

// ---------------------------------------------------------------------------
//  Binary container
// ---------------------------------------------------------------------------

constexpr char kMeshMagic[8] = {'C', 'A', 'T', 'M', 'E', 'S', 'H', '2'};
constexpr uint32_t kMeshBinaryVersion = 2;

struct MeshBinaryHeader {
    char magic[8];
    uint32_t version;
    uint32_t vertexStride;
    uint64_t vertexCount;
    uint64_t indexCount;
    uint64_t sourceSize;
    uint64_t sourceTime;
    uint64_t optionsHash;
    uint64_t clusterCount;
    uint32_t clusterStride;
    uint32_t clusterLevelCount;
    uint32_t baseTriangleCount;
    uint32_t reserved;
};
static_assert(sizeof(MeshBinaryHeader) == 80, "Mesh header must stay tightly packed");

// A dense mesh is hundreds of megabytes. Streaming that through ofstream costs
// an extra buffered copy per block and grows the file incrementally; going
// straight to the file handle with a pre-sized file is several times faster.
class RawFile {
public:
    ~RawFile() {
        if (m_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_handle);
        }
    }

    bool OpenForWrite(const std::wstring& path, uint64_t reserveBytes) {
        m_handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (m_handle == INVALID_HANDLE_VALUE) {
            return false;
        }

        // Allocating the full length up front stops the filesystem from
        // extending the file block by block as the write proceeds.
        LARGE_INTEGER endOfFile = {};
        endOfFile.QuadPart = static_cast<LONGLONG>(reserveBytes);
        if (SetFilePointerEx(m_handle, endOfFile, nullptr, FILE_BEGIN) && SetEndOfFile(m_handle)) {
            LARGE_INTEGER rewind = {};
            SetFilePointerEx(m_handle, rewind, nullptr, FILE_BEGIN);
        }
        return true;
    }

    bool OpenForRead(const std::wstring& path, uint64_t& outSize) {
        m_handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (m_handle == INVALID_HANDLE_VALUE) {
            return false;
        }
        LARGE_INTEGER size = {};
        if (!GetFileSizeEx(m_handle, &size)) {
            return false;
        }
        outSize = static_cast<uint64_t>(size.QuadPart);
        return true;
    }

    // WriteFile and ReadFile take a DWORD count, so anything past 4 GB has to
    // be split regardless.
    bool Write(const void* data, uint64_t size) {
        const uint8_t* cursor = static_cast<const uint8_t*>(data);
        while (size > 0) {
            const DWORD block = static_cast<DWORD>(size < kBlockSize ? size : kBlockSize);
            DWORD written = 0;
            if (!WriteFile(m_handle, cursor, block, &written, nullptr) || written != block) {
                return false;
            }
            cursor += written;
            size -= written;
        }
        return true;
    }

    bool Read(void* data, uint64_t size) {
        uint8_t* cursor = static_cast<uint8_t*>(data);
        while (size > 0) {
            const DWORD block = static_cast<DWORD>(size < kBlockSize ? size : kBlockSize);
            DWORD read = 0;
            if (!ReadFile(m_handle, cursor, block, &read, nullptr) || read != block) {
                return false;
            }
            cursor += read;
            size -= read;
        }
        return true;
    }

private:
    static constexpr uint64_t kBlockSize = 32ull * 1024ull * 1024ull;
    HANDLE m_handle = INVALID_HANDLE_VALUE;
};

bool ReadSourceStamp(const std::wstring& path, uint64_t& outSize, uint64_t& outTime) {
    WIN32_FILE_ATTRIBUTE_DATA attributes = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        return false;
    }
    outSize = (static_cast<uint64_t>(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
    outTime = (static_cast<uint64_t>(attributes.ftLastWriteTime.dwHighDateTime) << 32) |
              attributes.ftLastWriteTime.dwLowDateTime;
    return true;
}

std::wstring CacheDirectory() {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L"";
    }
    return std::wstring(buffer) + L"\\Catalyst\\MeshCache";
}

uint64_t HashPathAndOptions(const std::wstring& sourcePath, uint64_t optionsHash) {
    uint64_t hash = 1469598103934665603ull;
    for (wchar_t character : sourcePath) {
        const wchar_t lowered = static_cast<wchar_t>(towlower(character));
        hash ^= static_cast<uint64_t>(lowered & 0xFF);
        hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>((lowered >> 8) & 0xFF);
        hash *= 1099511628211ull;
    }
    for (int byte = 0; byte < 8; ++byte) {
        hash ^= (optionsHash >> (byte * 8)) & 0xFFull;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::wstring CacheFilePath(const std::wstring& sourcePath, uint64_t optionsHash) {
    const std::wstring directory = CacheDirectory();
    if (directory.empty()) {
        return L"";
    }
    wchar_t name[32] = {};
    swprintf_s(name, L"%016llX.cmesh", static_cast<unsigned long long>(HashPathAndOptions(sourcePath, optionsHash)));
    return directory + L"\\" + name;
}

}

// ---------------------------------------------------------------------------

MeshData BuildMeshData(const RawObjData& raw, const MeshImportOptions& options) {
    MeshData mesh;

    const size_t cornerCount = raw.Corners.size();
    const size_t positionCount = raw.Positions.size() / 3;
    if (cornerCount < 3 || positionCount == 0) {
        return mesh;
    }

    const size_t texcoordCount = raw.Texcoords.size() / 2;
    const size_t normalCount = raw.Normals.size() / 3;
    const bool useColors = options.importVertexColors && raw.Colors.size() == positionCount * 3;

    // Flat shading needs a private vertex per corner anyway, so welding it
    // would only be undone a moment later.
    const bool weld = options.mergeDuplicateVertices && options.normalMode != MeshNormalMode::Flat;

    // Most OBJs land close to one vertex per position once welded; unwelded is
    // exactly one per corner.
    const size_t expectedVertices = weld ? (positionCount + positionCount / 4) : cornerCount;
    mesh.Vertices.reserve(expectedVertices);
    mesh.Indices.reserve(cornerCount);

    std::vector<int32_t> vertexSourcePosition;
    vertexSourcePosition.reserve(expectedVertices);

    WeldTable weldTable;
    if (weld) {
        weldTable.Reset(positionCount, expectedVertices);
    }

    auto makeVertex = [&](const ObjCorner& corner) {
        Vertex vertex = {};

        const float* position = raw.Positions.data() + static_cast<size_t>(corner.position) * 3;
        vertex.position = {position[0], position[1], position[2]};

        if (useColors) {
            const float* color = raw.Colors.data() + static_cast<size_t>(corner.position) * 3;
            vertex.color = {color[0], color[1], color[2], 1.0f};
        } else {
            vertex.color = {1.0f, 1.0f, 1.0f, 1.0f};
        }

        if (corner.texcoord >= 0 && static_cast<size_t>(corner.texcoord) < texcoordCount) {
            const float* texcoord = raw.Texcoords.data() + static_cast<size_t>(corner.texcoord) * 2;
            vertex.uv = {texcoord[0], options.flipUV ? (1.0f - texcoord[1]) : texcoord[1]};
        } else {
            vertex.uv = {0.0f, 0.0f};
        }

        if (corner.normal >= 0 && static_cast<size_t>(corner.normal) < normalCount) {
            const float* normal = raw.Normals.data() + static_cast<size_t>(corner.normal) * 3;
            vertex.normal = {normal[0], normal[1], normal[2]};
        } else {
            // Zero marks "unshaded" so the normal pass can fill it in later.
            vertex.normal = {0.0f, 0.0f, 0.0f};
        }

        vertex.tangent = {0.0f, 0.0f, 0.0f};
        return vertex;
    };

    for (size_t base = 0; base + 2 < cornerCount; base += 3) {
        const ObjCorner* triangle = raw.Corners.data() + base;

        // A corner that indexes past the position array means the file is
        // malformed; drop the triangle rather than reading out of bounds.
        bool valid = true;
        for (int corner = 0; corner < 3; ++corner) {
            const int32_t position = triangle[corner].position;
            if (position < 0 || static_cast<size_t>(position) >= positionCount) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            continue;
        }

        for (int corner = 0; corner < 3; ++corner) {
            const ObjCorner& reference = triangle[corner];
            int32_t vertexIndex = -1;

            if (weld) {
                vertexIndex = weldTable.Find(reference);
            }

            if (vertexIndex < 0) {
                vertexIndex = static_cast<int32_t>(mesh.Vertices.size());
                mesh.Vertices.push_back(makeVertex(reference));
                vertexSourcePosition.push_back(reference.position);
                if (weld) {
                    weldTable.Insert(reference, vertexIndex);
                }
            }

            mesh.Indices.push_back(static_cast<uint32_t>(vertexIndex));
        }
    }

    if (mesh.Vertices.empty() || mesh.Indices.size() < 3) {
        return MeshData();
    }

    ApplyTransform(mesh, options);

    if (options.flipWinding) {
        FlipWinding(mesh);
    }

    switch (options.normalMode) {
    case MeshNormalMode::Flat:
        GenerateFlatNormals(mesh);
        break;
    case MeshNormalMode::Smooth:
        GenerateSmoothNormals(mesh, vertexSourcePosition, positionCount, false);
        break;
    case MeshNormalMode::Import:
    default: {
        // Only pay for the accumulation pass when the file actually left
        // something unshaded.
        bool anyMissing = normalCount == 0;
        if (!anyMissing) {
            for (const Vertex& vertex : mesh.Vertices) {
                const float lengthSquared = vertex.normal.x * vertex.normal.x +
                                            vertex.normal.y * vertex.normal.y +
                                            vertex.normal.z * vertex.normal.z;
                if (lengthSquared <= 1e-12f) {
                    anyMissing = true;
                    break;
                }
            }
        }
        if (anyMissing) {
            GenerateSmoothNormals(mesh, vertexSourcePosition, positionCount, true);
        }
        NormalizeNormals(mesh);
        break;
    }
    }

    if (options.optimizeForCache) {
        SpatialOptimize(mesh);
    }

    // Tangents come last so they match the final vertex order and normals.
    if (options.generateTangents) {
        GenerateTangents(mesh);
    } else {
        Vertex* vertices = mesh.Vertices.data();
        ForEachRange(mesh.Vertices.size(), [&](size_t begin, size_t end) {
            for (size_t index = begin; index < end; ++index) {
                vertices[index].tangent = {1.0f, 0.0f, 0.0f};
            }
        });
    }

    return mesh;
}

bool IsMeshBinaryFile(const std::wstring& path) {
    RawFile file;
    uint64_t fileSize = 0;
    if (!file.OpenForRead(path, fileSize) || fileSize < sizeof(MeshBinaryHeader)) {
        return false;
    }
    char magic[8] = {};
    if (!file.Read(magic, sizeof(magic))) {
        return false;
    }
    return std::memcmp(magic, kMeshMagic, sizeof(kMeshMagic)) == 0;
}

bool WriteMeshBinary(const std::wstring& path,
                     const MeshData& mesh,
                     uint64_t sourceSize,
                     uint64_t sourceTime,
                     uint64_t optionsHash,
                     std::string* outError) {
    if (mesh.Vertices.empty() || mesh.Indices.empty()) {
        if (outError != nullptr) {
            *outError = "Refusing to write an empty mesh.";
        }
        return false;
    }

    MeshBinaryHeader header = {};
    std::memcpy(header.magic, kMeshMagic, sizeof(kMeshMagic));
    header.version = kMeshBinaryVersion;
    header.vertexStride = static_cast<uint32_t>(sizeof(Vertex));
    header.vertexCount = mesh.Vertices.size();
    header.indexCount = mesh.Indices.size();
    header.sourceSize = sourceSize;
    header.sourceTime = sourceTime;
    header.optionsHash = optionsHash;
    header.clusterCount = mesh.Clusters.size();
    header.clusterStride = static_cast<uint32_t>(sizeof(MeshCluster));
    header.clusterLevelCount = mesh.ClusterLevelCount;
    header.baseTriangleCount = mesh.BaseTriangleCount;

    const uint64_t vertexBytes = static_cast<uint64_t>(mesh.Vertices.size()) * sizeof(Vertex);
    const uint64_t indexBytes = static_cast<uint64_t>(mesh.Indices.size()) * sizeof(uint32_t);
    const uint64_t clusterBytes = static_cast<uint64_t>(mesh.Clusters.size()) * sizeof(MeshCluster);
    const uint64_t totalBytes = sizeof(MeshBinaryHeader) + vertexBytes + indexBytes + clusterBytes;

    RawFile file;
    if (!file.OpenForWrite(path, totalBytes)) {
        if (outError != nullptr) {
            *outError = "Could not open the destination file for writing.";
        }
        return false;
    }

    if (!file.Write(&header, sizeof(header)) ||
        !file.Write(mesh.Vertices.data(), vertexBytes) ||
        !file.Write(mesh.Indices.data(), indexBytes) ||
        (clusterBytes > 0 && !file.Write(mesh.Clusters.data(), clusterBytes))) {
        if (outError != nullptr) {
            *outError = "Writing the mesh data failed part way through.";
        }
        return false;
    }
    return true;
}

bool ReadMeshBinary(const std::wstring& path,
                    MeshData& outMesh,
                    uint64_t* outSourceSize,
                    uint64_t* outSourceTime,
                    uint64_t* outOptionsHash,
                    std::string* outError) {
    RawFile file;
    uint64_t fileSize = 0;
    if (!file.OpenForRead(path, fileSize) || fileSize < sizeof(MeshBinaryHeader)) {
        if (outError != nullptr) {
            *outError = "Could not open the mesh file.";
        }
        return false;
    }

    MeshBinaryHeader header = {};
    if (!file.Read(&header, sizeof(header)) ||
        std::memcmp(header.magic, kMeshMagic, sizeof(kMeshMagic)) != 0) {
        if (outError != nullptr) {
            *outError = "The file is not Catalyst binary geometry.";
        }
        return false;
    }

    // A stride change means the Vertex layout moved on; the cache entry is
    // stale rather than corrupt, so the caller just rebuilds it.
    if (header.version != kMeshBinaryVersion ||
        header.vertexStride != sizeof(Vertex) ||
        (header.clusterCount > 0 && header.clusterStride != sizeof(MeshCluster))) {
        if (outError != nullptr) {
            *outError = "The mesh file was written by a different engine build.";
        }
        return false;
    }

    // Cross-check the counts against the real file length before allocating, so
    // a truncated or corrupt file cannot ask for an absurd amount of memory.
    const uint64_t expectedSize = sizeof(MeshBinaryHeader) +
                                  header.vertexCount * sizeof(Vertex) +
                                  header.indexCount * sizeof(uint32_t) +
                                  header.clusterCount * sizeof(MeshCluster);
    if (header.vertexCount == 0 || header.indexCount == 0 || expectedSize != fileSize) {
        if (outError != nullptr) {
            *outError = "The mesh file is truncated or corrupt.";
        }
        return false;
    }

    outMesh.Vertices.resize(static_cast<size_t>(header.vertexCount));
    outMesh.Indices.resize(static_cast<size_t>(header.indexCount));
    outMesh.Clusters.resize(static_cast<size_t>(header.clusterCount));
    outMesh.ClusterLevelCount = header.clusterLevelCount;
    outMesh.BaseTriangleCount = header.baseTriangleCount;
    if (!file.Read(outMesh.Vertices.data(), header.vertexCount * sizeof(Vertex)) ||
        !file.Read(outMesh.Indices.data(), header.indexCount * sizeof(uint32_t)) ||
        (header.clusterCount > 0 && !file.Read(outMesh.Clusters.data(), header.clusterCount * sizeof(MeshCluster)))) {
        outMesh = MeshData();
        if (outError != nullptr) {
            *outError = "Reading the mesh data failed part way through.";
        }
        return false;
    }

    if (outSourceSize != nullptr) {
        *outSourceSize = header.sourceSize;
    }
    if (outSourceTime != nullptr) {
        *outSourceTime = header.sourceTime;
    }
    if (outOptionsHash != nullptr) {
        *outOptionsHash = header.optionsHash;
    }
    return true;
}

bool TryLoadCachedMesh(const std::wstring& sourcePath, const MeshImportOptions& options, MeshData& outMesh) {
    const uint64_t optionsHash = options.Hash();
    const std::wstring cachePath = CacheFilePath(sourcePath, optionsHash);
    if (cachePath.empty()) {
        return false;
    }

    uint64_t sourceSize = 0;
    uint64_t sourceTime = 0;
    if (!ReadSourceStamp(sourcePath, sourceSize, sourceTime)) {
        return false;
    }

    uint64_t cachedSize = 0;
    uint64_t cachedTime = 0;
    uint64_t cachedOptions = 0;
    MeshData cached;
    if (!ReadMeshBinary(cachePath, cached, &cachedSize, &cachedTime, &cachedOptions, nullptr)) {
        return false;
    }

    if (cachedSize != sourceSize || cachedTime != sourceTime || cachedOptions != optionsHash) {
        return false;
    }

    outMesh = std::move(cached);
    return true;
}

void StoreCachedMesh(const std::wstring& sourcePath, const MeshImportOptions& options, const MeshData& mesh) {
    const uint64_t optionsHash = options.Hash();
    const std::wstring cachePath = CacheFilePath(sourcePath, optionsHash);
    if (cachePath.empty()) {
        return;
    }

    uint64_t sourceSize = 0;
    uint64_t sourceTime = 0;
    if (!ReadSourceStamp(sourcePath, sourceSize, sourceTime)) {
        return;
    }

    // A cache miss is never fatal, so every failure here is silent.
    std::error_code error;
    fs::create_directories(fs::path(CacheDirectory()), error);
    if (error) {
        return;
    }
    WriteMeshBinary(cachePath, mesh, sourceSize, sourceTime, optionsHash, nullptr);
}

MeshData ImportMeshFromSource(const std::wstring& sourcePath, const MeshImportOptions& options, MeshBuildStats* outStats, bool useCache) {
    MeshBuildStats stats;
    const auto startTime = std::chrono::steady_clock::now();

    MeshData mesh;

    // Geometry the importer already converted needs no work beyond the read.
    if (IsMeshBinaryFile(sourcePath)) {
        std::string error;
        if (!ReadMeshBinary(sourcePath, mesh, nullptr, nullptr, nullptr, &error)) {
            throw std::runtime_error("Failed to load mesh: " + error);
        }
        stats.fromCache = true;
    } else if (useCache && TryLoadCachedMesh(sourcePath, options, mesh)) {
        stats.fromCache = true;
    } else {
        RawObjData raw;
        std::string error;

        // Dispatch on the extension, then let each reader reject a file that is
        // not what its name claims.
        std::wstring extension = fs::path(sourcePath).extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });

        // Options after folding in whatever the file declares about itself.
        MeshImportOptions effective = options;

        if (extension == L".fbx") {
            FbxSceneInfo fbxInfo;
            if (!ParseFbxFile(sourcePath, raw, &fbxInfo, &error)) {
                throw std::runtime_error("Failed to load model: " + error);
            }
            stats.sourceWasFbx = true;
            stats.fbxUnitScaleFactor = fbxInfo.UnitScaleFactor;
            stats.fbxUpAxis = fbxInfo.UpAxis;
            stats.fbxMeshCount = fbxInfo.MeshCount;
            stats.textures = fbxInfo.Textures;

            // UnitScaleFactor is centimetres per unit, and the engine works in
            // metres, so a file authored in centimetres needs 0.01.
            if (options.applySourceUnits && fbxInfo.UnitScaleFactor > 0.0) {
                effective.uniformScale *= static_cast<float>(fbxInfo.UnitScaleFactor / 100.0);
                stats.appliedUnitScale = static_cast<float>(fbxInfo.UnitScaleFactor / 100.0);
            }
            if (options.applySourceUpAxis && fbxInfo.UpAxis == 2) {
                effective.upAxis = MeshUpAxis::Z;
                stats.appliedUpAxisConversion = true;
            }
        } else if (!ParseObjFile(sourcePath, raw, &error)) {
            throw std::runtime_error("Failed to load model: " + error);
        }

        const auto parsedTime = std::chrono::steady_clock::now();
        stats.parseSeconds = std::chrono::duration<double>(parsedTime - startTime).count();
        stats.sourceTriangles = raw.TriangleCount();

        mesh = BuildMeshData(raw, effective);
        if (mesh.Vertices.empty() || mesh.Indices.empty()) {
            throw std::runtime_error("The model contains no renderable geometry.");
        }

        if (options.buildVirtualGeometry) {
            ClusterBuildStats clusterStats;
            MeshData clustered = BuildClusterHierarchy(mesh, &clusterStats);
            if (!clustered.Clusters.empty()) {
                mesh = std::move(clustered);
                stats.clusterCount = clusterStats.TotalClusters;
                stats.clusterLevels = clusterStats.Levels;
                stats.clusterSeconds = clusterStats.Seconds;
            }
        }

        if (useCache) {
            StoreCachedMesh(sourcePath, options, mesh);
        }
    }

    stats.vertexCount = mesh.Vertices.size();
    stats.indexCount = mesh.Indices.size();
    if (stats.clusterCount == 0) {
        stats.clusterCount = static_cast<uint32_t>(mesh.Clusters.size());
        stats.clusterLevels = mesh.ClusterLevelCount;
    }
    if (stats.sourceTriangles == 0) {
        stats.sourceTriangles = mesh.Indices.size() / 3;
    }
    stats.buildSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count() - stats.parseSeconds;

    if (outStats != nullptr) {
        *outStats = stats;
    }
    return mesh;
}

}
