#pragma once
#include <string>
#include <d3d12.h>
#include "MeshBuild.h"
#include "MeshImportOptions.h"
#include "PrimitiveGenerator.h"

class ModelLoader {
public:
    static MeshData LoadMeshData(const std::string& filepath);
    static MeshData LoadMeshData(const std::wstring& filepath);
    static MeshData LoadMeshData(const std::wstring& filepath, const MeshImportOptions& options);
    static Mesh* Load(const std::string& filepath, ID3D12Device* device, ID3D12CommandQueue* cmdQueue);

    // Converts a source model into the engine's binary geometry format with the
    // import options baked in, so every load after this is a straight read
    // instead of a re-parse.
    static bool ConvertToAsset(const std::wstring& sourcePath,
                               const std::wstring& destinationPath,
                               const MeshImportOptions& options,
                               CatalystImport::MeshBuildStats* outStats,
                               std::string* outError);
};
