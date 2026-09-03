#pragma once
#include <cstdint>
#include <vector>

#include <DirectXMath.h>

#include "PrimitiveGenerator.h"

namespace CatalystImport {

// Triangles per cluster. The renderer draws one indirect call per surviving
// cluster, so this trades draw-call count against culling granularity.
inline constexpr uint32_t kClusterMaxTriangles = 128;

// How many clusters are merged before being simplified together. Larger groups
// simplify better - fewer locked edges - but coarsen the LOD cut.
inline constexpr uint32_t kClusterGroupSize = 8;

struct ClusterBuildStats {
    uint32_t Levels = 0;
    uint32_t TotalClusters = 0;
    uint32_t BaseTriangles = 0;
    uint32_t TotalTriangles = 0;   // across every level
    double Seconds = 0.0;
};

// Splits the mesh into clusters and simplifies them into a coarser level over
// and over, producing the DAG the renderer cuts through at run time. Returns a
// new MeshData whose index buffer is laid out cluster by cluster, every level
// concatenated, with Clusters describing the runs.
MeshData BuildClusterHierarchy(const MeshData& mesh, ClusterBuildStats* outStats);

}
