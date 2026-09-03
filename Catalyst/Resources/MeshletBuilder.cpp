#define NOMINMAX
#include "MeshletBuilder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <thread>
#include <unordered_map>

#include "MeshSimplify.h"

using namespace DirectX;

namespace CatalystImport {
namespace {

// Stop refining once a level is small enough to be a single group, or once the
// hierarchy is deep enough that further levels buy nothing.
constexpr uint32_t kMaxLevels = 16;
constexpr float kSimplifyRatio = 0.5f;

// ---------------------------------------------------------------------------
//  Small helpers
// ---------------------------------------------------------------------------

struct PositionKey {
    float x, y, z;
    bool operator==(const PositionKey& other) const {
        return std::memcmp(this, &other, sizeof(PositionKey)) == 0;
    }
};

struct PositionKeyHash {
    size_t operator()(const PositionKey& key) const {
        uint32_t bits[3];
        std::memcpy(bits, &key, sizeof(bits));
        size_t hash = 1469598103934665603ull;
        for (uint32_t value : bits) {
            hash ^= value;
            hash *= 1099511628211ull;
        }
        return hash;
    }
};

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

// Orders triangles so that neighbours in the list are neighbours in space.
// Clusters are then just consecutive runs, which keeps their bounding spheres
// tight without a full graph partition.
std::vector<uint32_t> SortTrianglesSpatially(const std::vector<Vertex>& vertices,
                                             const std::vector<uint32_t>& indices) {
    const size_t triangleCount = indices.size() / 3;
    std::vector<uint32_t> order(triangleCount);
    if (triangleCount == 0) {
        return order;
    }

    XMFLOAT3 minimum{FLT_MAX, FLT_MAX, FLT_MAX};
    XMFLOAT3 maximum{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (uint32_t index : indices) {
        const XMFLOAT3& p = vertices[index].position;
        minimum.x = (std::min)(minimum.x, p.x);
        minimum.y = (std::min)(minimum.y, p.y);
        minimum.z = (std::min)(minimum.z, p.z);
        maximum.x = (std::max)(maximum.x, p.x);
        maximum.y = (std::max)(maximum.y, p.y);
        maximum.z = (std::max)(maximum.z, p.z);
    }

    const float extentX = (std::max)(maximum.x - minimum.x, 1e-6f);
    const float extentY = (std::max)(maximum.y - minimum.y, 1e-6f);
    const float extentZ = (std::max)(maximum.z - minimum.z, 1e-6f);

    std::vector<uint64_t> keys(triangleCount);
    for (size_t triangle = 0; triangle < triangleCount; ++triangle) {
        const XMFLOAT3& a = vertices[indices[triangle * 3 + 0]].position;
        const XMFLOAT3& b = vertices[indices[triangle * 3 + 1]].position;
        const XMFLOAT3& c = vertices[indices[triangle * 3 + 2]].position;
        const float cx = (a.x + b.x + c.x) / 3.0f;
        const float cy = (a.y + b.y + c.y) / 3.0f;
        const float cz = (a.z + b.z + c.z) / 3.0f;
        const uint32_t gx = static_cast<uint32_t>(std::clamp((cx - minimum.x) / extentX, 0.0f, 1.0f) * 1023.0f);
        const uint32_t gy = static_cast<uint32_t>(std::clamp((cy - minimum.y) / extentY, 0.0f, 1.0f) * 1023.0f);
        const uint32_t gz = static_cast<uint32_t>(std::clamp((cz - minimum.z) / extentZ, 0.0f, 1.0f) * 1023.0f);
        keys[triangle] = (static_cast<uint64_t>(MortonCode(gx, gy, gz)) << 32) | static_cast<uint64_t>(triangle);
    }

    std::sort(keys.begin(), keys.end());
    for (size_t slot = 0; slot < triangleCount; ++slot) {
        order[slot] = static_cast<uint32_t>(keys[slot] & 0xFFFFFFFFull);
    }
    return order;
}

// Bounding sphere plus the backface cone, both in object space.
void ComputeClusterBounds(const std::vector<Vertex>& vertices,
                          const uint32_t* indices,
                          uint32_t indexCount,
                          MeshCluster& cluster) {
    XMFLOAT3 minimum{FLT_MAX, FLT_MAX, FLT_MAX};
    XMFLOAT3 maximum{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (uint32_t i = 0; i < indexCount; ++i) {
        const XMFLOAT3& p = vertices[indices[i]].position;
        minimum.x = (std::min)(minimum.x, p.x);
        minimum.y = (std::min)(minimum.y, p.y);
        minimum.z = (std::min)(minimum.z, p.z);
        maximum.x = (std::max)(maximum.x, p.x);
        maximum.y = (std::max)(maximum.y, p.y);
        maximum.z = (std::max)(maximum.z, p.z);
    }

    cluster.Center = {(minimum.x + maximum.x) * 0.5f,
                      (minimum.y + maximum.y) * 0.5f,
                      (minimum.z + maximum.z) * 0.5f};

    float radiusSquared = 0.0f;
    for (uint32_t i = 0; i < indexCount; ++i) {
        const XMFLOAT3& p = vertices[indices[i]].position;
        const float dx = p.x - cluster.Center.x;
        const float dy = p.y - cluster.Center.y;
        const float dz = p.z - cluster.Center.z;
        radiusSquared = (std::max)(radiusSquared, dx * dx + dy * dy + dz * dz);
    }
    cluster.Radius = sqrtf(radiusSquared);

    // Average the face normals, then find how far the worst face leans off that
    // axis. If the spread is more than a hemisphere the cone is useless and the
    // cutoff is left at 1 so the shader never culls on it.
    XMFLOAT3 axis{0.0f, 0.0f, 0.0f};
    for (uint32_t i = 0; i + 2 < indexCount; i += 3) {
        const XMFLOAT3& a = vertices[indices[i + 0]].position;
        const XMFLOAT3& b = vertices[indices[i + 1]].position;
        const XMFLOAT3& c = vertices[indices[i + 2]].position;
        const float e1x = b.x - a.x, e1y = b.y - a.y, e1z = b.z - a.z;
        const float e2x = c.x - a.x, e2y = c.y - a.y, e2z = c.z - a.z;
        XMFLOAT3 normal{e1y * e2z - e1z * e2y, e1z * e2x - e1x * e2z, e1x * e2y - e1y * e2x};
        const float length = sqrtf(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        if (length <= 1e-20f) {
            continue;
        }
        axis.x += normal.x / length;
        axis.y += normal.y / length;
        axis.z += normal.z / length;
    }

    const float axisLength = sqrtf(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (axisLength <= 1e-12f) {
        cluster.ConeAxis = {0.0f, 0.0f, 0.0f};
        cluster.ConeCutoff = 1.0f;
        return;
    }
    axis.x /= axisLength;
    axis.y /= axisLength;
    axis.z /= axisLength;

    float smallestDot = 1.0f;
    for (uint32_t i = 0; i + 2 < indexCount; i += 3) {
        const XMFLOAT3& a = vertices[indices[i + 0]].position;
        const XMFLOAT3& b = vertices[indices[i + 1]].position;
        const XMFLOAT3& c = vertices[indices[i + 2]].position;
        const float e1x = b.x - a.x, e1y = b.y - a.y, e1z = b.z - a.z;
        const float e2x = c.x - a.x, e2y = c.y - a.y, e2z = c.z - a.z;
        XMFLOAT3 normal{e1y * e2z - e1z * e2y, e1z * e2x - e1x * e2z, e1x * e2y - e1y * e2x};
        const float length = sqrtf(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        if (length <= 1e-20f) {
            continue;
        }
        const float dot = (normal.x * axis.x + normal.y * axis.y + normal.z * axis.z) / length;
        smallestDot = (std::min)(smallestDot, dot);
    }

    cluster.ConeAxis = axis;
    cluster.ConeCutoff = (smallestDot <= 0.0f) ? 1.0f : sqrtf((std::max)(0.0f, 1.0f - smallestDot * smallestDot));
}

// One level of the hierarchy while it is being built.
struct LevelCluster {
    std::vector<uint32_t> Indices;
    uint32_t GlobalIndex = 0;   // slot in ClusteredMesh::Clusters
    float Error = 0.0f;
    XMFLOAT3 GroupCenter = {};
    float GroupRadius = 0.0f;
};

void SphereOfIndices(const std::vector<Vertex>& vertices,
                     const std::vector<uint32_t>& indices,
                     XMFLOAT3& outCenter,
                     float& outRadius) {
    XMFLOAT3 minimum{FLT_MAX, FLT_MAX, FLT_MAX};
    XMFLOAT3 maximum{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (uint32_t index : indices) {
        const XMFLOAT3& p = vertices[index].position;
        minimum.x = (std::min)(minimum.x, p.x);
        minimum.y = (std::min)(minimum.y, p.y);
        minimum.z = (std::min)(minimum.z, p.z);
        maximum.x = (std::max)(maximum.x, p.x);
        maximum.y = (std::max)(maximum.y, p.y);
        maximum.z = (std::max)(maximum.z, p.z);
    }
    outCenter = {(minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f, (minimum.z + maximum.z) * 0.5f};
    float radiusSquared = 0.0f;
    for (uint32_t index : indices) {
        const XMFLOAT3& p = vertices[index].position;
        const float dx = p.x - outCenter.x, dy = p.y - outCenter.y, dz = p.z - outCenter.z;
        radiusSquared = (std::max)(radiusSquared, dx * dx + dy * dy + dz * dz);
    }
    outRadius = sqrtf(radiusSquared);
}

// Grows a sphere until it also contains another one.
//
// This matters because the run-time cut divides each cluster's error by its own
// distance to the camera, so raw error monotonicity up the DAG is not enough on
// its own: a parent whose sphere happens to sit further away can project to a
// SMALLER error than its child and invert the test, which tears holes in the
// surface and leaves stray coarse clusters floating. Forcing every parent sphere
// to enclose its children guarantees the parent is never the more distant of the
// two, so the projected errors stay ordered from any camera position.
void EncloseSphere(XMFLOAT3& center, float& radius, const XMFLOAT3& other, float otherRadius) {
    const float dx = other.x - center.x;
    const float dy = other.y - center.y;
    const float dz = other.z - center.z;
    const float distance = sqrtf(dx * dx + dy * dy + dz * dz);

    if (distance + otherRadius <= radius) {
        return;   // already contained
    }
    if (distance + radius <= otherRadius) {
        center = other;
        radius = otherRadius;
        return;
    }

    const float grown = (distance + radius + otherRadius) * 0.5f;
    if (distance > 1e-12f) {
        const float step = (grown - radius) / distance;
        center.x += dx * step;
        center.y += dy * step;
        center.z += dz * step;
    }
    radius = grown;
}

// Splits a triangle soup into spatially coherent clusters of at most
// kClusterMaxTriangles.
std::vector<LevelCluster> SplitIntoClusters(const std::vector<Vertex>& vertices,
                                            const std::vector<uint32_t>& indices) {
    std::vector<LevelCluster> clusters;
    const size_t triangleCount = indices.size() / 3;
    if (triangleCount == 0) {
        return clusters;
    }

    const std::vector<uint32_t> order = SortTrianglesSpatially(vertices, indices);

    LevelCluster current;
    current.Indices.reserve(kClusterMaxTriangles * 3);
    for (size_t slot = 0; slot < order.size(); ++slot) {
        const uint32_t triangle = order[slot];
        current.Indices.push_back(indices[triangle * 3 + 0]);
        current.Indices.push_back(indices[triangle * 3 + 1]);
        current.Indices.push_back(indices[triangle * 3 + 2]);

        if (current.Indices.size() >= kClusterMaxTriangles * 3) {
            clusters.push_back(std::move(current));
            current = LevelCluster();
            current.Indices.reserve(kClusterMaxTriangles * 3);
        }
    }
    if (!current.Indices.empty()) {
        clusters.push_back(std::move(current));
    }
    return clusters;
}

// Two clusters are neighbours when they share an edge. Grouping neighbours is
// what lets the simplifier dissolve the boundary between them instead of
// locking it.
std::vector<std::vector<uint32_t>> BuildClusterAdjacency(const std::vector<Vertex>& vertices,
                                                         const std::vector<LevelCluster>& clusters) {
    std::unordered_map<PositionKey, uint32_t, PositionKeyHash> positionIds;
    positionIds.reserve(clusters.size() * kClusterMaxTriangles);

    auto positionId = [&](uint32_t vertexIndex) {
        const XMFLOAT3& p = vertices[vertexIndex].position;
        const PositionKey key{p.x, p.y, p.z};
        const auto found = positionIds.find(key);
        if (found != positionIds.end()) {
            return found->second;
        }
        const uint32_t id = static_cast<uint32_t>(positionIds.size());
        positionIds.emplace(key, id);
        return id;
    };

    std::unordered_map<uint64_t, std::vector<uint32_t>> edgeClusters;
    edgeClusters.reserve(clusters.size() * kClusterMaxTriangles * 2);

    for (uint32_t clusterIndex = 0; clusterIndex < clusters.size(); ++clusterIndex) {
        const std::vector<uint32_t>& indices = clusters[clusterIndex].Indices;
        for (size_t t = 0; t + 2 < indices.size(); t += 3) {
            const uint32_t ids[3] = {positionId(indices[t]), positionId(indices[t + 1]), positionId(indices[t + 2])};
            for (int corner = 0; corner < 3; ++corner) {
                const uint32_t a = ids[corner];
                const uint32_t b = ids[(corner + 1) % 3];
                const uint64_t key = (static_cast<uint64_t>((std::min)(a, b)) << 32) | (std::max)(a, b);
                std::vector<uint32_t>& owners = edgeClusters[key];
                if (owners.empty() || owners.back() != clusterIndex) {
                    if (std::find(owners.begin(), owners.end(), clusterIndex) == owners.end()) {
                        owners.push_back(clusterIndex);
                    }
                }
            }
        }
    }

    std::vector<std::vector<uint32_t>> adjacency(clusters.size());
    for (const auto& entry : edgeClusters) {
        const std::vector<uint32_t>& owners = entry.second;
        for (size_t i = 0; i < owners.size(); ++i) {
            for (size_t j = i + 1; j < owners.size(); ++j) {
                adjacency[owners[i]].push_back(owners[j]);
                adjacency[owners[j]].push_back(owners[i]);
            }
        }
    }
    for (std::vector<uint32_t>& list : adjacency) {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }
    return adjacency;
}

// Greedy flood fill over the adjacency graph. Not a balanced graph partition,
// but it keeps groups connected, which is the property that matters.
std::vector<std::vector<uint32_t>> GroupClusters(const std::vector<std::vector<uint32_t>>& adjacency) {
    const size_t clusterCount = adjacency.size();
    std::vector<uint8_t> assigned(clusterCount, 0);
    std::vector<std::vector<uint32_t>> groups;

    for (uint32_t seed = 0; seed < clusterCount; ++seed) {
        if (assigned[seed]) {
            continue;
        }

        std::vector<uint32_t> group;
        group.push_back(seed);
        assigned[seed] = 1;

        // Breadth-first so the group stays compact rather than stringy.
        size_t frontier = 0;
        while (group.size() < kClusterGroupSize && frontier < group.size()) {
            const uint32_t current = group[frontier++];
            for (uint32_t neighbour : adjacency[current]) {
                if (assigned[neighbour]) {
                    continue;
                }
                assigned[neighbour] = 1;
                group.push_back(neighbour);
                if (group.size() >= kClusterGroupSize) {
                    break;
                }
            }
        }
        groups.push_back(std::move(group));
    }
    return groups;
}

// The body receives its worker slot as well as the item index, so each thread
// can be handed private scratch without having to guess which slot it is on.
void RunParallel(size_t count, size_t workerCount, const std::function<void(size_t, size_t)>& body) {
    if (count == 0) {
        return;
    }
    workerCount = (std::min)(workerCount, count);
    if (workerCount < 1) {
        workerCount = 1;
    }
    if (workerCount == 1) {
        for (size_t index = 0; index < count; ++index) {
            body(index, 0);
        }
        return;
    }

    std::vector<std::thread> workers;
    workers.reserve(workerCount - 1);
    auto run = [&](size_t worker) {
        for (size_t index = worker; index < count; index += workerCount) {
            body(index, worker);
        }
    };
    for (size_t worker = 1; worker < workerCount; ++worker) {
        workers.emplace_back(run, worker);
    }
    run(0);
    for (std::thread& worker : workers) {
        worker.join();
    }
}

}

MeshData BuildClusterHierarchy(const MeshData& mesh, ClusterBuildStats* outStats) {
    const auto startTime = std::chrono::steady_clock::now();

    MeshData result;
    result.Vertices = mesh.Vertices;
    if (mesh.Indices.size() < 3 || mesh.Vertices.empty()) {
        if (outStats != nullptr) {
            *outStats = ClusterBuildStats();
        }
        return result;
    }

    result.BaseTriangleCount = static_cast<uint32_t>(mesh.Indices.size() / 3);

    // Every worker needs its own scratch. The lock array is indexed by global
    // vertex, so it is allocated once and only ever touched for the vertices of
    // the group being simplified.
    const size_t workerCount = (std::max)(1u, std::thread::hardware_concurrency());
    std::vector<SimplifyScratch> scratches(workerCount);
    for (SimplifyScratch& scratch : scratches) {
        scratch.VertexLocked.assign(result.Vertices.size(), 0);
        scratch.PositionOfVertex.assign(result.Vertices.size(), -1);
        scratch.VertexRemap.resize(result.Vertices.size());
    }

    // Writes a cluster into the shared index buffer and returns its slot.
    auto emitCluster = [&](LevelCluster& cluster, uint32_t levelIndex) {
        MeshCluster emitted = {};
        emitted.IndexOffset = static_cast<uint32_t>(result.Indices.size());
        emitted.IndexCount = static_cast<uint32_t>(cluster.Indices.size());
        emitted.Level = levelIndex;
        emitted.GroupCenter = cluster.GroupCenter;
        emitted.GroupRadius = cluster.GroupRadius;
        emitted.GroupError = cluster.Error;
        // Replaced when this cluster is later folded into a parent. Until then
        // it is a root and stays selected once the threshold passes its own
        // error.
        emitted.ParentCenter = cluster.GroupCenter;
        emitted.ParentRadius = cluster.GroupRadius;
        emitted.ParentError = FLT_MAX;

        ComputeClusterBounds(result.Vertices, cluster.Indices.data(),
                             static_cast<uint32_t>(cluster.Indices.size()), emitted);

        cluster.GlobalIndex = static_cast<uint32_t>(result.Clusters.size());
        result.Clusters.push_back(emitted);
        result.Indices.insert(result.Indices.end(), cluster.Indices.begin(), cluster.Indices.end());
    };

    // ------------------------------------------------------------- level zero
    std::vector<LevelCluster> working = SplitIntoClusters(result.Vertices, mesh.Indices);
    for (LevelCluster& cluster : working) {
        cluster.Error = 0.0f;
        SphereOfIndices(result.Vertices, cluster.Indices, cluster.GroupCenter, cluster.GroupRadius);
        emitCluster(cluster, 0);
    }

    uint32_t levelIndex = 1;
    while (working.size() > 1 && levelIndex < kMaxLevels) {
        const std::vector<std::vector<uint32_t>> adjacency = BuildClusterAdjacency(result.Vertices, working);
        const std::vector<std::vector<uint32_t>> groups = GroupClusters(adjacency);

        std::vector<uint32_t> groupOfCluster(working.size(), 0);
        for (uint32_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
            for (uint32_t clusterIndex : groups[groupIndex]) {
                groupOfCluster[clusterIndex] = groupIndex;
            }
        }

        // A position touched by more than one group sits on a group rim and has
        // to survive the simplification, or the two sides stop lining up.
        std::unordered_map<PositionKey, int64_t, PositionKeyHash> positionGroupBuild;
        positionGroupBuild.reserve(result.Vertices.size());
        for (uint32_t clusterIndex = 0; clusterIndex < working.size(); ++clusterIndex) {
            const int64_t groupIndex = static_cast<int64_t>(groupOfCluster[clusterIndex]);
            for (uint32_t vertexIndex : working[clusterIndex].Indices) {
                const XMFLOAT3& p = result.Vertices[vertexIndex].position;
                const PositionKey key{p.x, p.y, p.z};
                auto found = positionGroupBuild.find(key);
                if (found == positionGroupBuild.end()) {
                    positionGroupBuild.emplace(key, groupIndex);
                } else if (found->second != groupIndex) {
                    found->second = -1;
                }
            }
        }
        // Frozen before the workers start, so every lookup below is a read.
        const std::unordered_map<PositionKey, int64_t, PositionKeyHash>& positionGroup = positionGroupBuild;

        struct GroupResult {
            std::vector<LevelCluster> Clusters;
            float Error = 0.0f;
            XMFLOAT3 Center = {};
            float Radius = 0.0f;
            bool Simplified = false;
        };
        std::vector<GroupResult> groupResults(groups.size());

        RunParallel(groups.size(), scratches.size(), [&](size_t groupIndex, size_t worker) {
            SimplifyScratch& scratch = scratches[worker];

            std::vector<uint32_t> merged;
            for (uint32_t clusterIndex : groups[groupIndex]) {
                const std::vector<uint32_t>& indices = working[clusterIndex].Indices;
                merged.insert(merged.end(), indices.begin(), indices.end());
            }
            if (merged.size() < 3) {
                return;
            }

            GroupResult& output = groupResults[groupIndex];
            SphereOfIndices(result.Vertices, merged, output.Center, output.Radius);

            // Enclose every child's sphere so the parent can never be further
            // from the camera than the child it replaces.
            for (uint32_t clusterIndex : groups[groupIndex]) {
                EncloseSphere(output.Center, output.Radius,
                              working[clusterIndex].GroupCenter, working[clusterIndex].GroupRadius);
            }

            std::vector<uint32_t> lockedTouched;
            lockedTouched.reserve(merged.size());
            for (uint32_t vertexIndex : merged) {
                const XMFLOAT3& p = result.Vertices[vertexIndex].position;
                const PositionKey key{p.x, p.y, p.z};
                const auto found = positionGroup.find(key);
                if (found != positionGroup.end() && found->second < 0) {
                    if (scratch.VertexLocked[vertexIndex] == 0) {
                        scratch.VertexLocked[vertexIndex] = 1;
                        lockedTouched.push_back(vertexIndex);
                    }
                }
            }

            const size_t sourceTriangles = merged.size() / 3;
            size_t targetTriangles = static_cast<size_t>(sourceTriangles * kSimplifyRatio);
            if (targetTriangles < 1) {
                targetTriangles = 1;
            }

            float error = 0.0f;
            std::vector<uint32_t> simplified = SimplifyTriangles(result.Vertices, merged, targetTriangles, scratch, &error);

            for (uint32_t vertexIndex : lockedTouched) {
                scratch.VertexLocked[vertexIndex] = 0;
            }

            if (simplified.size() / 3 >= sourceTriangles) {
                return;   // nothing was gained; the caller carries these forward
            }

            // Error has to grow as we climb, or a cut through the DAG is
            // ambiguous and neighbouring regions can disagree about which level
            // they are on - which is exactly what opens a crack.
            float inheritedError = 0.0f;
            for (uint32_t clusterIndex : groups[groupIndex]) {
                inheritedError = (std::max)(inheritedError, working[clusterIndex].Error);
            }
            output.Error = (std::max)(error, inheritedError);
            output.Simplified = true;
            output.Clusters = SplitIntoClusters(result.Vertices, simplified);
            for (LevelCluster& cluster : output.Clusters) {
                cluster.Error = output.Error;
                cluster.GroupCenter = output.Center;
                cluster.GroupRadius = output.Radius;
            }
        });

        std::vector<LevelCluster> nextWorking;
        bool anyProgress = false;
        for (size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
            GroupResult& output = groupResults[groupIndex];

            if (!output.Simplified) {
                // Carry the clusters forward untouched rather than retiring them
                // as roots. A root keeps being drawn at every threshold, and its
                // neighbours go on coarsening past it until the shared boundary
                // is dissolved on one side only - a crack. Next round they get
                // grouped with different neighbours and usually reduce.
                for (uint32_t clusterIndex : groups[groupIndex]) {
                    nextWorking.push_back(working[clusterIndex]);
                }
                continue;
            }

            anyProgress = true;
            for (uint32_t clusterIndex : groups[groupIndex]) {
                MeshCluster& child = result.Clusters[working[clusterIndex].GlobalIndex];
                child.ParentCenter = output.Center;
                child.ParentRadius = output.Radius;
                child.ParentError = output.Error;
            }
            for (LevelCluster& cluster : output.Clusters) {
                emitCluster(cluster, levelIndex);
                nextWorking.push_back(cluster);
            }
        }

        if (!anyProgress || nextWorking.empty()) {
            break;   // nothing left that can be reduced
        }
        working = std::move(nextWorking);
        ++levelIndex;
    }

    result.ClusterLevelCount = levelIndex;

    if (outStats != nullptr) {
        ClusterBuildStats stats;
        stats.Levels = result.ClusterLevelCount;
        stats.TotalClusters = static_cast<uint32_t>(result.Clusters.size());
        stats.BaseTriangles = result.BaseTriangleCount;
        stats.TotalTriangles = static_cast<uint32_t>(result.Indices.size() / 3);
        stats.Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
        *outStats = stats;
    }
    return result;
}

}
