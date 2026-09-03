#pragma once
#include <cstdint>
#include <vector>

#include "PrimitiveGenerator.h"

namespace CatalystImport {

// Scratch buffers the simplifier reuses between calls. Building a LOD DAG means
// thousands of small simplifications, and reallocating for each one costs more
// than the collapses do.
struct SimplifyScratch {
    std::vector<uint8_t> VertexLocked;      // one byte per vertex in the shared array
    std::vector<int32_t> PositionOfVertex;  // vertex -> position node, -1 when unvisited
    std::vector<uint32_t> VertexRemap;      // vertex -> the vertex it collapsed into
};

// Collapses edges in quadric-error order until the triangle count reaches the
// target. Placement is always onto one of the two endpoints, so no new vertices
// are created and the caller's vertex array stays valid untouched.
//
// Vertices marked in scratch.VertexLocked are never removed and never moved.
// Locking the rim of a cluster group is what keeps neighbouring groups
// watertight when they are simplified independently.
//
// outError receives the largest quadric error accepted, which is the distance
// scale at which this simplification becomes visible.
std::vector<uint32_t> SimplifyTriangles(const std::vector<Vertex>& vertices,
                                        const std::vector<uint32_t>& indices,
                                        size_t targetTriangles,
                                        SimplifyScratch& scratch,
                                        float* outError);

}
