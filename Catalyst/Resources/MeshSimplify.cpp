#define NOMINMAX
#include "MeshSimplify.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <queue>
#include <unordered_map>

using namespace DirectX;

namespace CatalystImport {
namespace {

// A symmetric 4x4 quadric stored as its ten distinct terms. Summing quadrics
// over the faces around a vertex gives the squared distance from a candidate
// position to every one of those planes at once.
struct Quadric {
    double a00 = 0.0, a01 = 0.0, a02 = 0.0, a03 = 0.0;
    double a11 = 0.0, a12 = 0.0, a13 = 0.0;
    double a22 = 0.0, a23 = 0.0;
    double a33 = 0.0;

    void AddPlane(double x, double y, double z, double w, double weight) {
        a00 += weight * x * x;
        a01 += weight * x * y;
        a02 += weight * x * z;
        a03 += weight * x * w;
        a11 += weight * y * y;
        a12 += weight * y * z;
        a13 += weight * y * w;
        a22 += weight * z * z;
        a23 += weight * z * w;
        a33 += weight * w * w;
    }

    void Add(const Quadric& other) {
        a00 += other.a00; a01 += other.a01; a02 += other.a02; a03 += other.a03;
        a11 += other.a11; a12 += other.a12; a13 += other.a13;
        a22 += other.a22; a23 += other.a23;
        a33 += other.a33;
    }

    double Evaluate(double x, double y, double z) const {
        return a00 * x * x + 2.0 * a01 * x * y + 2.0 * a02 * x * z + 2.0 * a03 * x +
               a11 * y * y + 2.0 * a12 * y * z + 2.0 * a13 * y +
               a22 * z * z + 2.0 * a23 * z +
               a33;
    }
};

struct PositionKey {
    float x, y, z;
    bool operator==(const PositionKey& other) const {
        // Bit equality is what we want: welded vertices that share a position
        // carry byte-identical floats, and anything else is a distinct corner.
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

struct PendingCollapse {
    double cost;
    uint32_t from;     // position node that disappears
    uint32_t into;     // position node that survives
    uint32_t version;  // stale once either endpoint has changed

    bool operator>(const PendingCollapse& other) const { return cost > other.cost; }
};

}

std::vector<uint32_t> SimplifyTriangles(const std::vector<Vertex>& vertices,
                                        const std::vector<uint32_t>& indices,
                                        size_t targetTriangles,
                                        SimplifyScratch& scratch,
                                        float* outError) {
    const size_t triangleCount = indices.size() / 3;
    if (triangleCount <= targetTriangles || triangleCount == 0) {
        if (outError != nullptr) {
            *outError = 0.0f;
        }
        return indices;
    }

    // ----------------------------------------------------------------- setup
    // Simplification runs on positions, not on attribute corners. A UV seam
    // splits one position into several vertices, and collapsing those
    // independently would tear the surface apart.
    std::unordered_map<PositionKey, uint32_t, PositionKeyHash> positionLookup;
    positionLookup.reserve(indices.size());

    std::vector<uint32_t> positionNodes;          // node -> a representative vertex
    std::vector<std::vector<uint32_t>> nodeVertices;  // node -> every vertex on it
    positionNodes.reserve(indices.size() / 2);
    nodeVertices.reserve(indices.size() / 2);

    if (scratch.PositionOfVertex.size() < vertices.size()) {
        scratch.PositionOfVertex.assign(vertices.size(), -1);
    }

    std::vector<uint32_t> touchedVertices;
    touchedVertices.reserve(indices.size());

    auto nodeOf = [&](uint32_t vertexIndex) {
        int32_t& cached = scratch.PositionOfVertex[vertexIndex];
        if (cached >= 0) {
            return static_cast<uint32_t>(cached);
        }

        const XMFLOAT3& position = vertices[vertexIndex].position;
        const PositionKey key{position.x, position.y, position.z};
        const auto existing = positionLookup.find(key);
        uint32_t node;
        if (existing != positionLookup.end()) {
            node = existing->second;
        } else {
            node = static_cast<uint32_t>(positionNodes.size());
            positionNodes.push_back(vertexIndex);
            nodeVertices.emplace_back();
            positionLookup.emplace(key, node);
        }
        nodeVertices[node].push_back(vertexIndex);
        cached = static_cast<int32_t>(node);
        touchedVertices.push_back(vertexIndex);
        return node;
    };

    struct Face {
        uint32_t node[3];
        uint32_t vertex[3];
        bool alive = true;
    };

    std::vector<Face> faces;
    faces.reserve(triangleCount);
    for (size_t triangle = 0; triangle < triangleCount; ++triangle) {
        Face face = {};
        for (int corner = 0; corner < 3; ++corner) {
            face.vertex[corner] = indices[triangle * 3 + corner];
            face.node[corner] = nodeOf(face.vertex[corner]);
        }
        // Drop triangles that are already degenerate rather than carrying them
        // through the collapse loop.
        if (face.node[0] == face.node[1] || face.node[1] == face.node[2] || face.node[0] == face.node[2]) {
            continue;
        }
        faces.push_back(face);
    }

    const size_t nodeCount = positionNodes.size();
    if (faces.empty() || nodeCount == 0) {
        for (uint32_t vertexIndex : touchedVertices) {
            scratch.PositionOfVertex[vertexIndex] = -1;
        }
        if (outError != nullptr) {
            *outError = 0.0f;
        }
        return indices;
    }

    // A node is locked when any vertex sitting on it is locked.
    std::vector<uint8_t> nodeLocked(nodeCount, 0);
    if (!scratch.VertexLocked.empty()) {
        for (size_t node = 0; node < nodeCount; ++node) {
            for (uint32_t vertexIndex : nodeVertices[node]) {
                if (vertexIndex < scratch.VertexLocked.size() && scratch.VertexLocked[vertexIndex] != 0) {
                    nodeLocked[node] = 1;
                    break;
                }
            }
        }
    }

    std::vector<XMFLOAT3> nodePosition(nodeCount);
    for (size_t node = 0; node < nodeCount; ++node) {
        nodePosition[node] = vertices[positionNodes[node]].position;
    }

    // --------------------------------------------------------------- quadrics
    std::vector<Quadric> quadrics(nodeCount);
    std::vector<std::vector<uint32_t>> nodeFaces(nodeCount);

    auto faceNormal = [&](const Face& face, double& outX, double& outY, double& outZ) {
        const XMFLOAT3& a = nodePosition[face.node[0]];
        const XMFLOAT3& b = nodePosition[face.node[1]];
        const XMFLOAT3& c = nodePosition[face.node[2]];
        const double e1x = double(b.x) - a.x, e1y = double(b.y) - a.y, e1z = double(b.z) - a.z;
        const double e2x = double(c.x) - a.x, e2y = double(c.y) - a.y, e2z = double(c.z) - a.z;
        outX = e1y * e2z - e1z * e2y;
        outY = e1z * e2x - e1x * e2z;
        outZ = e1x * e2y - e1y * e2x;
        return sqrt(outX * outX + outY * outY + outZ * outZ);
    };

    for (size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex) {
        const Face& face = faces[faceIndex];
        double nx = 0.0, ny = 0.0, nz = 0.0;
        const double length = faceNormal(face, nx, ny, nz);
        if (length > 1e-24) {
            nx /= length;
            ny /= length;
            nz /= length;
            const XMFLOAT3& a = nodePosition[face.node[0]];
            const double d = -(nx * a.x + ny * a.y + nz * a.z);
            // Weighting by area makes large flat regions dominate small noise.
            const double weight = length * 0.5;
            for (int corner = 0; corner < 3; ++corner) {
                quadrics[face.node[corner]].AddPlane(nx, ny, nz, d, weight);
            }
        }
        for (int corner = 0; corner < 3; ++corner) {
            nodeFaces[face.node[corner]].push_back(static_cast<uint32_t>(faceIndex));
        }
    }

    // An edge used by exactly one face is an open boundary. Adding a plane
    // perpendicular to the surface there stops the silhouette from receding.
    {
        std::unordered_map<uint64_t, uint32_t> edgeUse;
        edgeUse.reserve(faces.size() * 3);
        for (const Face& face : faces) {
            for (int corner = 0; corner < 3; ++corner) {
                const uint32_t a = face.node[corner];
                const uint32_t b = face.node[(corner + 1) % 3];
                const uint64_t key = (static_cast<uint64_t>((std::min)(a, b)) << 32) | (std::max)(a, b);
                ++edgeUse[key];
            }
        }

        for (const Face& face : faces) {
            double nx = 0.0, ny = 0.0, nz = 0.0;
            const double length = faceNormal(face, nx, ny, nz);
            if (length <= 1e-24) {
                continue;
            }
            nx /= length; ny /= length; nz /= length;

            for (int corner = 0; corner < 3; ++corner) {
                const uint32_t a = face.node[corner];
                const uint32_t b = face.node[(corner + 1) % 3];
                const uint64_t key = (static_cast<uint64_t>((std::min)(a, b)) << 32) | (std::max)(a, b);
                if (edgeUse[key] != 1) {
                    continue;
                }

                const XMFLOAT3& pa = nodePosition[a];
                const XMFLOAT3& pb = nodePosition[b];
                double ex = double(pb.x) - pa.x, ey = double(pb.y) - pa.y, ez = double(pb.z) - pa.z;
                const double edgeLength = sqrt(ex * ex + ey * ey + ez * ez);
                if (edgeLength <= 1e-24) {
                    continue;
                }
                ex /= edgeLength; ey /= edgeLength; ez /= edgeLength;

                // Plane containing the edge and perpendicular to the triangle.
                double px = ey * nz - ez * ny;
                double py = ez * nx - ex * nz;
                double pz = ex * ny - ey * nx;
                const double planeLength = sqrt(px * px + py * py + pz * pz);
                if (planeLength <= 1e-24) {
                    continue;
                }
                px /= planeLength; py /= planeLength; pz /= planeLength;
                const double d = -(px * pa.x + py * pa.y + pz * pa.z);
                const double weight = edgeLength * edgeLength;
                quadrics[a].AddPlane(px, py, pz, d, weight);
                quadrics[b].AddPlane(px, py, pz, d, weight);
            }
        }
    }

    // ------------------------------------------------------------- collapse
    std::vector<uint32_t> nodeVersion(nodeCount, 0);
    std::vector<uint8_t> nodeAlive(nodeCount, 1);
    std::vector<uint32_t> nodeRemap(nodeCount);
    for (uint32_t node = 0; node < nodeCount; ++node) {
        nodeRemap[node] = node;
    }

    // Which vertex on the surviving node each retired vertex becomes. Seeded to
    // identity and rewritten as collapses pair corners up across the edge. This
    // lives in the scratch block because a LOD build calls in thousands of
    // times and a full-mesh array per call would dominate the run.
    if (scratch.VertexRemap.size() < vertices.size()) {
        scratch.VertexRemap.resize(vertices.size());
    }
    std::vector<uint32_t>& vertexRemap = scratch.VertexRemap;
    for (uint32_t vertexIndex : touchedVertices) {
        vertexRemap[vertexIndex] = vertexIndex;
    }

    std::priority_queue<PendingCollapse, std::vector<PendingCollapse>, std::greater<PendingCollapse>> queue;

    auto costOf = [&](uint32_t from, uint32_t into) {
        Quadric combined = quadrics[from];
        combined.Add(quadrics[into]);
        const XMFLOAT3& target = nodePosition[into];
        return (std::max)(0.0, combined.Evaluate(target.x, target.y, target.z));
    };

    auto pushEdge = [&](uint32_t a, uint32_t b) {
        if (a == b) {
            return;
        }
        // A locked node must survive, so it can only ever be the destination.
        const bool canCollapseAIntoB = !nodeLocked[a];
        const bool canCollapseBIntoA = !nodeLocked[b];
        if (!canCollapseAIntoB && !canCollapseBIntoA) {
            return;
        }

        if (canCollapseAIntoB) {
            queue.push({costOf(a, b), a, b, nodeVersion[a] + nodeVersion[b]});
        }
        if (canCollapseBIntoA) {
            queue.push({costOf(b, a), b, a, nodeVersion[a] + nodeVersion[b]});
        }
    };

    for (const Face& face : faces) {
        for (int corner = 0; corner < 3; ++corner) {
            const uint32_t a = face.node[corner];
            const uint32_t b = face.node[(corner + 1) % 3];
            if (a < b) {
                pushEdge(a, b);
            }
        }
    }

    size_t liveTriangles = faces.size();
    double worstError = 0.0;

    // Collapsing must not turn a triangle inside out; that is what produces the
    // shattered spikes a naive quadric simplifier is known for.
    auto collapseFlipsAnything = [&](uint32_t from, uint32_t into) {
        const XMFLOAT3& target = nodePosition[into];
        for (uint32_t faceIndex : nodeFaces[from]) {
            const Face& face = faces[faceIndex];
            if (!face.alive) {
                continue;
            }
            if (face.node[0] == into || face.node[1] == into || face.node[2] == into) {
                continue;  // this face disappears with the collapse
            }

            XMFLOAT3 corners[3];
            for (int corner = 0; corner < 3; ++corner) {
                corners[corner] = (face.node[corner] == from) ? target : nodePosition[face.node[corner]];
            }

            double beforeX, beforeY, beforeZ;
            const double beforeLength = faceNormal(face, beforeX, beforeY, beforeZ);

            const double e1x = double(corners[1].x) - corners[0].x;
            const double e1y = double(corners[1].y) - corners[0].y;
            const double e1z = double(corners[1].z) - corners[0].z;
            const double e2x = double(corners[2].x) - corners[0].x;
            const double e2y = double(corners[2].y) - corners[0].y;
            const double e2z = double(corners[2].z) - corners[0].z;
            const double afterX = e1y * e2z - e1z * e2y;
            const double afterY = e1z * e2x - e1x * e2z;
            const double afterZ = e1x * e2y - e1y * e2x;
            const double afterLength = sqrt(afterX * afterX + afterY * afterY + afterZ * afterZ);

            if (afterLength <= 1e-24 || beforeLength <= 1e-24) {
                return true;  // collapsed to a sliver
            }
            const double alignment = (beforeX * afterX + beforeY * afterY + beforeZ * afterZ) /
                                     (beforeLength * afterLength);
            if (alignment < 0.2) {
                return true;
            }
        }
        return false;
    };

    while (liveTriangles > targetTriangles && !queue.empty()) {
        const PendingCollapse candidate = queue.top();
        queue.pop();

        const uint32_t from = candidate.from;
        const uint32_t into = candidate.into;
        if (!nodeAlive[from] || !nodeAlive[into] || from == into) {
            continue;
        }
        if (candidate.version != nodeVersion[from] + nodeVersion[into]) {
            continue;  // an endpoint moved since this was queued
        }
        if (nodeLocked[from]) {
            continue;
        }
        if (collapseFlipsAnything(from, into)) {
            continue;
        }

        // Pair up attribute corners across the edge before the faces go away, so
        // a UV seam keeps sensible coordinates instead of snapping to a random
        // corner on the far side.
        for (uint32_t faceIndex : nodeFaces[from]) {
            const Face& face = faces[faceIndex];
            if (!face.alive) {
                continue;
            }
            int fromCorner = -1;
            int intoCorner = -1;
            for (int corner = 0; corner < 3; ++corner) {
                if (face.node[corner] == from) {
                    fromCorner = corner;
                } else if (face.node[corner] == into) {
                    intoCorner = corner;
                }
            }
            if (fromCorner >= 0 && intoCorner >= 0) {
                vertexRemap[face.vertex[fromCorner]] = vertexRemap[face.vertex[intoCorner]];
            }
        }

        // Anything on the retired node that no face paired up falls back to the
        // survivor's representative vertex.
        const uint32_t fallbackVertex = vertexRemap[positionNodes[into]];
        for (uint32_t vertexIndex : nodeVertices[from]) {
            if (vertexRemap[vertexIndex] == vertexIndex) {
                vertexRemap[vertexIndex] = fallbackVertex;
            }
        }

        worstError = (std::max)(worstError, candidate.cost);

        // Retire the faces that used the collapsed edge, and rewrite the rest.
        std::vector<uint32_t>& survivingFaces = nodeFaces[into];
        for (uint32_t faceIndex : nodeFaces[from]) {
            Face& face = faces[faceIndex];
            if (!face.alive) {
                continue;
            }
            bool touchesInto = false;
            for (int corner = 0; corner < 3; ++corner) {
                if (face.node[corner] == into) {
                    touchesInto = true;
                }
            }
            if (touchesInto) {
                face.alive = false;
                --liveTriangles;
                continue;
            }
            for (int corner = 0; corner < 3; ++corner) {
                if (face.node[corner] == from) {
                    face.node[corner] = into;
                    face.vertex[corner] = vertexRemap[face.vertex[corner]];
                }
            }
            survivingFaces.push_back(faceIndex);
        }
        nodeFaces[from].clear();

        nodeAlive[from] = 0;
        nodeRemap[from] = into;
        quadrics[into].Add(quadrics[from]);
        ++nodeVersion[into];

        // Re-price every edge still touching the survivor.
        for (uint32_t faceIndex : survivingFaces) {
            const Face& face = faces[faceIndex];
            if (!face.alive) {
                continue;
            }
            for (int corner = 0; corner < 3; ++corner) {
                if (face.node[corner] == into) {
                    pushEdge(into, face.node[(corner + 1) % 3]);
                    pushEdge(into, face.node[(corner + 2) % 3]);
                }
            }
        }
    }

    // ------------------------------------------------------------------ emit
    std::vector<uint32_t> result;
    result.reserve(liveTriangles * 3);
    for (const Face& face : faces) {
        if (!face.alive) {
            continue;
        }
        const uint32_t a = vertexRemap[face.vertex[0]];
        const uint32_t b = vertexRemap[face.vertex[1]];
        const uint32_t c = vertexRemap[face.vertex[2]];
        // A collapse can leave a triangle with two corners on the same position;
        // those carry no area and only cost bandwidth.
        if (a == b || b == c || a == c) {
            continue;
        }
        result.push_back(a);
        result.push_back(b);
        result.push_back(c);
    }

    for (uint32_t vertexIndex : touchedVertices) {
        scratch.PositionOfVertex[vertexIndex] = -1;
    }

    if (outError != nullptr) {
        // The quadric is a squared distance, so the usable figure is its root.
        *outError = static_cast<float>(sqrt((std::max)(0.0, worstError)));
    }
    return result;
}

}
