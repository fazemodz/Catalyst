#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace CatalystImport {

// One face corner as authored: indices into the flat position/uv/normal
// arrays, already resolved to 0-based and already triangulated.
struct ObjCorner {
    int32_t position = -1;
    int32_t texcoord = -1;
    int32_t normal = -1;
};

// The OBJ file split into its raw arrays, before welding or shading.
// Corners come in groups of three; every polygon has been fanned already.
struct RawObjData {
    std::vector<float> Positions;   // 3 per position
    std::vector<float> Colors;      // 3 per position, empty when the file had none
    std::vector<float> Texcoords;   // 2 per texcoord
    std::vector<float> Normals;     // 3 per normal
    std::vector<ObjCorner> Corners; // 3 per triangle

    size_t TriangleCount() const { return Corners.size() / 3; }
};

// Memory-maps the file and parses it across every hardware thread. Returns
// false and fills outError when the file cannot be opened or holds no faces.
bool ParseObjFile(const std::wstring& path, RawObjData& out, std::string* outError);

}
