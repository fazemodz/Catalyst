#pragma once
#include <cstdint>
#include <string>

#include "FastObjParser.h"
#include "FbxParser.h"
#include "MeshImportOptions.h"
#include "PrimitiveGenerator.h"

namespace CatalystImport {

struct MeshBuildStats {
    size_t sourceTriangles = 0;
    size_t vertexCount = 0;
    size_t indexCount = 0;
    double parseSeconds = 0.0;
    double buildSeconds = 0.0;
    bool fromCache = false;

    // Virtualised geometry, zero when the option was off.
    uint32_t clusterCount = 0;
    uint32_t clusterLevels = 0;
    double clusterSeconds = 0.0;

    // What an FBX file declared about itself. Reported rather than applied, so
    // a centimetre or Z-up export can be corrected on purpose from the import
    // dialog instead of arriving silently wrong.
    bool sourceWasFbx = false;
    double fbxUnitScaleFactor = 1.0;
    int fbxUpAxis = 1;
    uint32_t fbxMeshCount = 0;

    // What was actually applied as a result, so the importer can say so rather
    // than leaving the artist to work out why the model changed size.
    float appliedUnitScale = 1.0f;
    bool appliedUpAxisConversion = false;

    // Textures the source file refers to, for the importer to go and find.
    std::vector<FbxTextureReference> textures;
    uint32_t texturesCopied = 0;
    uint32_t texturesMissing = 0;
    bool wroteMaterial = false;
};

// Welds face corners into vertices, applies the import transform, resolves
// normals and tangents, and optionally reorders the mesh so meshlets stay
// spatially tight and vertex fetches stay sequential.
MeshData BuildMeshData(const RawObjData& raw, const MeshImportOptions& options);

// ---------------------------------------------------------------------------
//  Binary geometry container
// ---------------------------------------------------------------------------
// A .catalystactor written by the importer is this format. Loading sniffs the
// magic, so a .catalystactor that is still a renamed OBJ keeps working.

bool IsMeshBinaryFile(const std::wstring& path);

bool WriteMeshBinary(const std::wstring& path,
                     const MeshData& mesh,
                     uint64_t sourceSize,
                     uint64_t sourceTime,
                     uint64_t optionsHash,
                     std::string* outError);

bool ReadMeshBinary(const std::wstring& path,
                    MeshData& outMesh,
                    uint64_t* outSourceSize,
                    uint64_t* outSourceTime,
                    uint64_t* outOptionsHash,
                    std::string* outError);

// ---------------------------------------------------------------------------
//  Text-source cache
// ---------------------------------------------------------------------------
// Keeps projects that still hold raw OBJ text fast without writing scratch
// files into the user's Assets folder. Entries live under LOCALAPPDATA and are
// keyed on the source path, its size and write time, and the import options.

bool TryLoadCachedMesh(const std::wstring& sourcePath, const MeshImportOptions& options, MeshData& outMesh);
void StoreCachedMesh(const std::wstring& sourcePath, const MeshImportOptions& options, const MeshData& mesh);

// Parses a source model and returns finished geometry, going through the cache
// when it can. Throws std::runtime_error on failure. Pass useCache = false when
// the caller is about to write the geometry somewhere permanent itself, so the
// same bytes are not also written to the cache.
MeshData ImportMeshFromSource(const std::wstring& sourcePath,
                              const MeshImportOptions& options,
                              MeshBuildStats* outStats,
                              bool useCache = true);

}
