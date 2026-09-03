#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "FastObjParser.h"

namespace CatalystImport {

// What an FBX file declares about itself. The importer reports these rather
// than silently applying them, so a centimetre-scale or Z-up export can be
// corrected deliberately from the import dialog instead of the geometry quietly
// arriving a hundred times too large.
// Which material slot a texture was bound to. Exporters name the underlying FBX
// property inconsistently, so this is matched loosely rather than exactly.
enum class FbxTextureSlot { Albedo, Normal, Roughness, Metallic, Unknown };

struct FbxTextureReference {
    FbxTextureSlot Slot = FbxTextureSlot::Unknown;
    std::string PropertyName;    // the FBX property the texture was bound to
    std::string RelativePath;    // as recorded, relative to the FBX file
    std::string AbsolutePath;    // as recorded - usually the authoring machine's
};

struct FbxSceneInfo {
    uint32_t Version = 0;
    double UnitScaleFactor = 1.0;   // centimetres per unit, as the file declares
    int UpAxis = 1;                 // 0 = X, 1 = Y, 2 = Z
    int UpAxisSign = 1;
    uint32_t MeshCount = 0;
    bool HadVertexColors = false;
    bool DroppedVertexColors = false;   // present, but in a layout we do not read

    // Every texture the file's materials refer to. Paths are as recorded; it is
    // the importer's job to work out where the file actually lives.
    std::vector<FbxTextureReference> Textures;
};

// Reads a binary FBX file and flattens every mesh in it into one triangle soup,
// with each node's world transform baked into its vertices.
//
// Only the binary form is supported. ASCII FBX is a completely different
// encoding and is reported as such rather than misread.
bool ParseFbxFile(const std::wstring& path,
                  RawObjData& out,
                  FbxSceneInfo* outInfo,
                  std::string* outError);

// Produces a human-readable report of what the reader sees in a file: the node
// tree, declared units and axis, every transform property each model carries
// (including the pivots and rotation order), the mapping and reference modes of
// each layer element, and the resulting bounds.
//
// This exists because "it imported wrong" has a dozen possible causes in FBX and
// guessing between them is slow. Never called during a normal import.
std::string DescribeFbxFile(const std::wstring& path);

// True when the file starts with the binary FBX signature. Used to give a
// precise error for the ASCII variant instead of a parse failure.
bool IsBinaryFbxFile(const std::wstring& path);

}
