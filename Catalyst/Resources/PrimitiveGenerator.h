#pragma once
#include <vector>
#include <DirectXMath.h>
#include <cstdint>
#include "Mesh.h" 

struct MeshData {
    std::vector<Vertex> Vertices;
    std::vector<uint32_t> Indices;

    // Virtualised-geometry data. Empty for procedural primitives and for models
    // imported with the option turned off, in which case the renderer falls
    // back to drawing the whole index buffer.
    std::vector<MeshCluster> Clusters;
    uint32_t ClusterLevelCount = 0;
    uint32_t BaseTriangleCount = 0;

    bool HasClusters() const { return !Clusters.empty(); }
};

class PrimitiveGenerator {
public:
    // Creates a standard Box (24 vertices to keep face normals sharp)
    static MeshData CreateCube(float width, float height, float depth, DirectX::XMFLOAT4 color = {1.0f, 1.0f, 1.0f, 1.0f});
    
    // Creates a Flat Grid (Perfect for floors and grounds)
    static MeshData CreatePlane(float width, float depth, DirectX::XMFLOAT4 color = {1.0f, 1.0f, 1.0f, 1.0f});
    // Creates a Cylinder with sharp caps and a smooth side wall
    static MeshData CreateCylinder(float radius, float height, uint32_t sliceCount, DirectX::XMFLOAT4 color = {1.0f, 1.0f, 1.0f, 1.0f});
    // Creates a UV Sphere
    static MeshData CreateSphere(float radius, uint32_t sliceCount, uint32_t stackCount, DirectX::XMFLOAT4 color = {1.0f, 1.0f, 1.0f, 1.0f});
};