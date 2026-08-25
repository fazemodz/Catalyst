#include "PrimitiveGenerator.h"
#include <cmath>

using namespace DirectX;

MeshData PrimitiveGenerator::CreateCube(float width, float height, float depth, XMFLOAT4 color) {
    MeshData mesh;
    
    float w2 = 0.5f * width;
    float h2 = 0.5f * height;
    float d2 = 0.5f * depth;

    // 24 Vertices (4 per face to maintain sharp normals and separate UVs)
    mesh.Vertices = {
        // Front Face
        { XMFLOAT3(-w2, -h2, -d2), color, XMFLOAT2(0.0f, 1.0f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
        { XMFLOAT3(-w2, +h2, -d2), color, XMFLOAT2(0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
        { XMFLOAT3(+w2, +h2, -d2), color, XMFLOAT2(1.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
        { XMFLOAT3(+w2, -h2, -d2), color, XMFLOAT2(1.0f, 1.0f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
        // Back Face
        { XMFLOAT3(-w2, -h2, +d2), color, XMFLOAT2(1.0f, 1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f) },
        { XMFLOAT3(+w2, -h2, +d2), color, XMFLOAT2(0.0f, 1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f) },
        { XMFLOAT3(+w2, +h2, +d2), color, XMFLOAT2(0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f) },
        { XMFLOAT3(-w2, +h2, +d2), color, XMFLOAT2(1.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f) },
        // Top Face
        { XMFLOAT3(-w2, +h2, -d2), color, XMFLOAT2(0.0f, 1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
        { XMFLOAT3(-w2, +h2, +d2), color, XMFLOAT2(0.0f, 0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
        { XMFLOAT3(+w2, +h2, +d2), color, XMFLOAT2(1.0f, 0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
        { XMFLOAT3(+w2, +h2, -d2), color, XMFLOAT2(1.0f, 1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
        // Bottom Face
        { XMFLOAT3(-w2, -h2, -d2), color, XMFLOAT2(1.0f, 1.0f), XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f) },
        { XMFLOAT3(+w2, -h2, -d2), color, XMFLOAT2(0.0f, 1.0f), XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f) },
        { XMFLOAT3(+w2, -h2, +d2), color, XMFLOAT2(0.0f, 0.0f), XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f) },
        { XMFLOAT3(-w2, -h2, +d2), color, XMFLOAT2(1.0f, 0.0f), XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f) },
        // Left Face
        { XMFLOAT3(-w2, -h2, +d2), color, XMFLOAT2(0.0f, 1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },
        { XMFLOAT3(-w2, +h2, +d2), color, XMFLOAT2(0.0f, 0.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },
        { XMFLOAT3(-w2, +h2, -d2), color, XMFLOAT2(1.0f, 0.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },
        { XMFLOAT3(-w2, -h2, -d2), color, XMFLOAT2(1.0f, 1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) },
        // Right Face
        { XMFLOAT3(+w2, -h2, -d2), color, XMFLOAT2(0.0f, 1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) },
        { XMFLOAT3(+w2, +h2, -d2), color, XMFLOAT2(0.0f, 0.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) },
        { XMFLOAT3(+w2, +h2, +d2), color, XMFLOAT2(1.0f, 0.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) },
        { XMFLOAT3(+w2, -h2, +d2), color, XMFLOAT2(1.0f, 1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) }
    };

    mesh.Indices = {
        0, 1, 2, 0, 2, 3,       // Front
        4, 5, 6, 4, 6, 7,       // Back
        8, 9, 10, 8, 10, 11,    // Top
        12, 13, 14, 12, 14, 15, // Bottom
        16, 17, 18, 16, 18, 19, // Left
        20, 21, 22, 20, 22, 23  // Right
    };

    return mesh;
}

MeshData PrimitiveGenerator::CreatePlane(float width, float depth, XMFLOAT4 color) {
    MeshData mesh;
    float w2 = 0.5f * width;
    float d2 = 0.5f * depth;

    mesh.Vertices = {
        { XMFLOAT3(-w2, 0.0f, -d2), color, XMFLOAT2(0.0f, 1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
        { XMFLOAT3(-w2, 0.0f, +d2), color, XMFLOAT2(0.0f, 0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
        { XMFLOAT3(+w2, 0.0f, +d2), color, XMFLOAT2(1.0f, 0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
        { XMFLOAT3(+w2, 0.0f, -d2), color, XMFLOAT2(1.0f, 1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) }
    };

    mesh.Indices = { 0, 1, 2, 0, 2, 3 };
    return mesh;
}

MeshData PrimitiveGenerator::CreateSphere(float radius, uint32_t sliceCount, uint32_t stackCount, XMFLOAT4 color) {
    MeshData mesh;
    
    // Top pole
    mesh.Vertices.push_back({ XMFLOAT3(0.0f, radius, 0.0f), color, XMFLOAT2(0.5f, 0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) });

    float phiStep = XM_PI / stackCount;
    float thetaStep = XM_2PI / sliceCount;

    for (uint32_t i = 1; i <= stackCount - 1; ++i) {
        float phi = i * phiStep;
        for (uint32_t j = 0; j <= sliceCount; ++j) {
            float theta = j * thetaStep;
            Vertex v;
            v.position.x = radius * sinf(phi) * cosf(theta);
            v.position.y = radius * cosf(phi);
            v.position.z = radius * sinf(phi) * sinf(theta);
            v.color = color;
            v.uv.x = theta / XM_2PI;
            v.uv.y = phi / XM_PI;
            
            XMVECTOR p = XMLoadFloat3(&v.position);
            XMStoreFloat3(&v.normal, XMVector3Normalize(p));

            v.tangent.x = -radius * sinf(phi) * sinf(theta);
            v.tangent.y = 0.0f;
            v.tangent.z = +radius * sinf(phi) * cosf(theta);
            XMVECTOR t = XMLoadFloat3(&v.tangent);
            XMStoreFloat3(&v.tangent, XMVector3Normalize(t));
            
            mesh.Vertices.push_back(v);
        }
    }

    // Bottom pole
    mesh.Vertices.push_back({ XMFLOAT3(0.0f, -radius, 0.0f), color, XMFLOAT2(0.5f, 1.0f), XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) });

    // Indices for top pole
    for (uint32_t i = 1; i <= sliceCount; ++i) {
        mesh.Indices.push_back(0);
        mesh.Indices.push_back(i + 1);
        mesh.Indices.push_back(i);
    }

    // Indices for rings
    uint32_t baseIndex = 1;
    uint32_t ringVertexCount = sliceCount + 1;
    for (uint32_t i = 0; i < stackCount - 2; ++i) {
        for (uint32_t j = 0; j < sliceCount; ++j) {
            mesh.Indices.push_back(baseIndex + i * ringVertexCount + j);
            mesh.Indices.push_back(baseIndex + i * ringVertexCount + j + 1);
            mesh.Indices.push_back(baseIndex + (i + 1) * ringVertexCount + j);

            mesh.Indices.push_back(baseIndex + (i + 1) * ringVertexCount + j);
            mesh.Indices.push_back(baseIndex + i * ringVertexCount + j + 1);
            mesh.Indices.push_back(baseIndex + (i + 1) * ringVertexCount + j + 1);
        }
    }

    // Indices for bottom pole
    uint32_t southPoleIndex = (uint32_t)mesh.Vertices.size() - 1;
    baseIndex = southPoleIndex - ringVertexCount;
    for (uint32_t i = 0; i < sliceCount; ++i) {
        mesh.Indices.push_back(southPoleIndex);
        mesh.Indices.push_back(baseIndex + i);
        mesh.Indices.push_back(baseIndex + i + 1);
    }

    return mesh;
}
MeshData PrimitiveGenerator::CreateCylinder(float radius, float height, uint32_t sliceCount, XMFLOAT4 color) {
    MeshData mesh;
    float halfHeight = height * 0.5f;
    float dTheta = XM_2PI / sliceCount;

    // 1. Build side wall (tube)
    for (uint32_t i = 0; i <= sliceCount; ++i) {
        float c = cosf(i * dTheta);
        float s = sinf(i * dTheta);

        // Bottom ring vertex
        Vertex v0;
        v0.position = XMFLOAT3(radius * c, -halfHeight, radius * s);
        v0.color = color;
        v0.uv = XMFLOAT2((float)i / sliceCount, 1.0f);
        v0.normal = XMFLOAT3(c, 0.0f, s);
        v0.tangent = XMFLOAT3(-s, 0.0f, c);
        mesh.Vertices.push_back(v0);

        // Top ring vertex
        Vertex v1;
        v1.position = XMFLOAT3(radius * c, halfHeight, radius * s);
        v1.color = color;
        v1.uv = XMFLOAT2((float)i / sliceCount, 0.0f);
        v1.normal = XMFLOAT3(c, 0.0f, s);
        v1.tangent = XMFLOAT3(-s, 0.0f, c);
        mesh.Vertices.push_back(v1);
    }

    for (uint32_t i = 0; i < sliceCount; ++i) {
        mesh.Indices.push_back(i * 2);
        mesh.Indices.push_back((i * 2) + 2);
        mesh.Indices.push_back((i * 2) + 1);

        mesh.Indices.push_back((i * 2) + 1);
        mesh.Indices.push_back((i * 2) + 2);
        mesh.Indices.push_back((i * 2) + 3);
    }

    // 2. Build top cap
    uint32_t baseIndex = (uint32_t)mesh.Vertices.size();
    Vertex topCenter;
    topCenter.position = XMFLOAT3(0.0f, halfHeight, 0.0f);
    topCenter.color = color;
    topCenter.uv = XMFLOAT2(0.5f, 0.5f);
    topCenter.normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
    topCenter.tangent = XMFLOAT3(1.0f, 0.0f, 0.0f);
    mesh.Vertices.push_back(topCenter);

    for (uint32_t i = 0; i <= sliceCount; ++i) {
        float c = cosf(i * dTheta);
        float s = sinf(i * dTheta);
        Vertex v;
        v.position = XMFLOAT3(radius * c, halfHeight, radius * s);
        v.color = color;
        v.uv = XMFLOAT2(c * 0.5f + 0.5f, s * 0.5f + 0.5f);
        v.normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
        v.tangent = XMFLOAT3(1.0f, 0.0f, 0.0f);
        mesh.Vertices.push_back(v);
    }
    for (uint32_t i = 0; i < sliceCount; ++i) {
        mesh.Indices.push_back(baseIndex);
        mesh.Indices.push_back(baseIndex + i + 1);
        mesh.Indices.push_back(baseIndex + i + 2);
    }

    // 3. Build bottom cap
    baseIndex = (uint32_t)mesh.Vertices.size();
    Vertex bottomCenter;
    bottomCenter.position = XMFLOAT3(0.0f, -halfHeight, 0.0f);
    bottomCenter.color = color;
    bottomCenter.uv = XMFLOAT2(0.5f, 0.5f);
    bottomCenter.normal = XMFLOAT3(0.0f, -1.0f, 0.0f);
    bottomCenter.tangent = XMFLOAT3(-1.0f, 0.0f, 0.0f);
    mesh.Vertices.push_back(bottomCenter);

    for (uint32_t i = 0; i <= sliceCount; ++i) {
        float c = cosf(i * dTheta);
        float s = sinf(i * dTheta);
        Vertex v;
        v.position = XMFLOAT3(radius * c, -halfHeight, radius * s);
        v.color = color;
        v.uv = XMFLOAT2(c * 0.5f + 0.5f, s * 0.5f + 0.5f);
        v.normal = XMFLOAT3(0.0f, -1.0f, 0.0f);
        v.tangent = XMFLOAT3(-1.0f, 0.0f, 0.0f);
        mesh.Vertices.push_back(v);
    }
    for (uint32_t i = 0; i < sliceCount; ++i) {
        mesh.Indices.push_back(baseIndex);
        mesh.Indices.push_back(baseIndex + i + 2);
        mesh.Indices.push_back(baseIndex + i + 1);
    }

    return mesh;
}