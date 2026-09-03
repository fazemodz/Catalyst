#pragma once
#include <cstdint>
#include <cstring>

// How vertex normals are resolved when a mesh is imported.
enum class MeshNormalMode : int {
    Import = 0,  // Use authored normals; generate smooth ones only where the file has none.
    Flat   = 1,  // One normal per triangle. Splits shared vertices, so it raises the vertex count.
    Smooth = 2   // Area-weighted average of every face touching a position.
};

// Source up-axis. DCC tools disagree, so the importer rotates on the way in
// rather than making the artist fix it downstream.
enum class MeshUpAxis : int {
    Y = 0,  // Already Y-up (Maya, Unity, most OBJ exports). No conversion.
    Z = 1   // Z-up (Blender, 3ds Max). Rotated -90 degrees about X.
};

// Everything the import dialog can change. The hash is baked into the mesh
// cache key so changing any option invalidates stale cached geometry.
struct MeshImportOptions {
    float uniformScale = 1.0f;
    MeshUpAxis upAxis = MeshUpAxis::Y;
    MeshNormalMode normalMode = MeshNormalMode::Import;
    bool flipUV = true;                   // OBJ stores V bottom-up; D3D samples top-down.
    bool flipWinding = false;
    bool generateTangents = true;
    bool mergeDuplicateVertices = true;   // Off means one vertex per face corner.
    bool optimizeForCache = true;         // Post-transform cache + vertex fetch reorder.
    bool centerPivot = false;
    bool importVertexColors = true;

    // Builds the cluster LOD hierarchy the virtualised-geometry path culls and
    // picks detail from. Costs import time and roughly doubles the index data;
    // without it a dense model is drawn in full, every frame.
    bool buildVirtualGeometry = true;

    // FBX files declare their own unit scale and up axis. Honouring them is
    // almost always right - the alternative is a centimetre export arriving a
    // hundred times too large, or a Z-up one lying on its side. Both fold into
    // the manual Uniform Scale and Source Up Axis rather than overriding them.
    // Ignored for formats that carry no such declaration, such as OBJ.
    bool applySourceUnits = true;
    bool applySourceUpAxis = true;

    uint64_t Hash() const {
        uint64_t hash = 1469598103934665603ull;
        auto mix = [&hash](uint64_t value) {
            for (int byte = 0; byte < 8; ++byte) {
                hash ^= (value >> (byte * 8)) & 0xFFull;
                hash *= 1099511628211ull;
            }
        };

        // Bit-copy the float so a scale change is always visible to the hash.
        uint32_t scaleBits = 0;
        static_assert(sizeof(scaleBits) == sizeof(uniformScale), "float must be 32-bit");
        std::memcpy(&scaleBits, &uniformScale, sizeof(scaleBits));
        mix(scaleBits);
        mix(static_cast<uint64_t>(upAxis));
        mix(static_cast<uint64_t>(normalMode));
        mix(static_cast<uint64_t>(flipUV));
        mix(static_cast<uint64_t>(flipWinding));
        mix(static_cast<uint64_t>(generateTangents));
        mix(static_cast<uint64_t>(mergeDuplicateVertices));
        mix(static_cast<uint64_t>(optimizeForCache));
        mix(static_cast<uint64_t>(centerPivot));
        mix(static_cast<uint64_t>(importVertexColors));
        mix(static_cast<uint64_t>(buildVirtualGeometry));
        mix(static_cast<uint64_t>(applySourceUnits));
        mix(static_cast<uint64_t>(applySourceUpAxis));
        return hash;
    }
};
