#include "PrimitiveGenerator.h"
#include <cmath>
#include <vector>

using namespace DirectX;
const float PI = 3.14159265359f;

void PushVert(std::vector<Vertex>& v, float x, float y, float z, float u, float v_tex, float nx, float ny, float nz, float tx, float ty, float tz) {
    Vertex vert;
    vert.position = { x, y, z };
    vert.color = { 1,1,1,1 };
    vert.uv = { u, v_tex };
    vert.normal = { nx, ny, nz };
    vert.tangent = { tx, ty, tz }; 
    v.push_back(vert);
}

Mesh* PrimitiveGenerator::CreateCube(ID3D12Device* device, ID3D12CommandQueue* cmdQueue) {
    std::vector<Vertex> verts;
    // Front
    PushVert(verts, -0.5f, -0.5f, -0.5f, 0, 1, 0, 0, -1, 1, 0, 0);
    PushVert(verts, -0.5f,  0.5f, -0.5f, 0, 0, 0, 0, -1, 1, 0, 0);
    PushVert(verts,  0.5f,  0.5f, -0.5f, 1, 0, 0, 0, -1, 1, 0, 0);
    PushVert(verts,  0.5f, -0.5f, -0.5f, 1, 1, 0, 0, -1, 1, 0, 0);
    // Back
    PushVert(verts, -0.5f, -0.5f,  0.5f, 1, 1, 0, 0, 1, -1, 0, 0);
    PushVert(verts,  0.5f, -0.5f,  0.5f, 0, 1, 0, 0, 1, -1, 0, 0);
    PushVert(verts,  0.5f,  0.5f,  0.5f, 0, 0, 0, 0, 1, -1, 0, 0);
    PushVert(verts, -0.5f,  0.5f,  0.5f, 1, 0, 0, 0, 1, -1, 0, 0);
    // Top
    PushVert(verts, -0.5f,  0.5f, -0.5f, 0, 1, 0, 1, 0, 1, 0, 0);
    PushVert(verts, -0.5f,  0.5f,  0.5f, 0, 0, 0, 1, 0, 1, 0, 0);
    PushVert(verts,  0.5f,  0.5f,  0.5f, 1, 0, 0, 1, 0, 1, 0, 0);
    PushVert(verts,  0.5f,  0.5f, -0.5f, 1, 1, 0, 1, 0, 1, 0, 0);
    // Bottom
    PushVert(verts, -0.5f, -0.5f, -0.5f, 1, 1, 0, -1, 0, 1, 0, 0);
    PushVert(verts,  0.5f, -0.5f, -0.5f, 0, 1, 0, -1, 0, 1, 0, 0);
    PushVert(verts,  0.5f, -0.5f,  0.5f, 0, 0, 0, -1, 0, 1, 0, 0);
    PushVert(verts, -0.5f, -0.5f,  0.5f, 1, 0, 0, -1, 0, 1, 0, 0);
    // Left
    PushVert(verts, -0.5f, -0.5f,  0.5f, 0, 1, -1, 0, 0, 0, 0, -1);
    PushVert(verts, -0.5f,  0.5f,  0.5f, 0, 0, -1, 0, 0, 0, 0, -1);
    PushVert(verts, -0.5f,  0.5f, -0.5f, 1, 0, -1, 0, 0, 0, 0, -1);
    PushVert(verts, -0.5f, -0.5f, -0.5f, 1, 1, -1, 0, 0, 0, 0, -1);
    // Right
    PushVert(verts,  0.5f, -0.5f, -0.5f, 0, 1, 1, 0, 0, 0, 0, 1);
    PushVert(verts,  0.5f,  0.5f, -0.5f, 0, 0, 1, 0, 0, 0, 0, 1);
    PushVert(verts,  0.5f,  0.5f,  0.5f, 1, 0, 1, 0, 0, 0, 0, 1);
    PushVert(verts,  0.5f, -0.5f,  0.5f, 1, 1, 1, 0, 0, 0, 0, 1);

    std::vector<uint32_t> indices = {
        0,1,2, 0,2,3, 4,5,6, 4,6,7, 8,9,10, 8,10,11, 
        12,13,14, 12,14,15, 16,17,18, 16,18,19, 20,21,22, 20,22,23
    };

    Mesh* mesh = new Mesh();
    mesh->Initialize(device, cmdQueue, verts, indices);
    return mesh;
}

Mesh* PrimitiveGenerator::CreatePlane(ID3D12Device* device, ID3D12CommandQueue* cmdQueue) {
    std::vector<Vertex> verts;
    PushVert(verts, -5.0f, 0.0f, -5.0f, 0, 10, 0,1,0, 1,0,0);
    PushVert(verts, -5.0f, 0.0f,  5.0f, 0, 0,  0,1,0, 1,0,0);
    PushVert(verts,  5.0f, 0.0f,  5.0f, 10, 0, 0,1,0, 1,0,0);
    PushVert(verts,  5.0f, 0.0f, -5.0f, 10, 10, 0,1,0, 1,0,0);

    std::vector<uint32_t> indices = { 0, 1, 2, 0, 2, 3 };
    Mesh* mesh = new Mesh();
    mesh->Initialize(device, cmdQueue, verts, indices);
    return mesh;
}

Mesh* PrimitiveGenerator::CreateSphere(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, int slices, int stacks) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    float phiStep = PI / stacks;
    float thetaStep = 2.0f * PI / slices;

    for (int i = 0; i <= stacks; i++) {
        float phi = i * phiStep;
        for (int j = 0; j <= slices; j++) {
            float theta = j * thetaStep;

            float x = sinf(phi) * cosf(theta);
            float y = cosf(phi);
            float z = sinf(phi) * sinf(theta);
            float u = (float)j / slices;
            float v = (float)i / stacks;

            float tx = -sinf(theta); float ty = 0.0f; float tz = cosf(theta);

            PushVert(vertices, x*0.5f, y*0.5f, z*0.5f, u, v, x,y,z, tx,ty,tz);
        }
    }

    for (int i = 0; i < stacks; i++) {
        for (int j = 0; j < slices; j++) {
            uint32_t i0 = i * (slices + 1) + j;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = (i + 1) * (slices + 1) + j;
            uint32_t i3 = i2 + 1;
            indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
            indices.push_back(i2); indices.push_back(i1); indices.push_back(i3);
        }
    }
    
    Mesh* mesh = new Mesh();
    mesh->Initialize(device, cmdQueue, vertices, indices);
    return mesh;
}

Mesh* PrimitiveGenerator::CreateCylinder(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, float bottomRadius, float topRadius, float height, int sliceCount, int stackCount) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    float stackHeight = height / stackCount;
    float radiusStep = (topRadius - bottomRadius) / stackCount;
    float ringCount = stackCount + 1;

    for (int i = 0; i < ringCount; ++i) {
        float y = -0.5f * height + i * stackHeight;
        float r = bottomRadius + i * radiusStep;
        float dTheta = 2.0f * PI / sliceCount;

        for (int j = 0; j <= sliceCount; ++j) {
            float c = cosf(j * dTheta); float s = sinf(j * dTheta);
            float u = (float)j / sliceCount; float v = 1.0f - (float)i / stackCount;
            float tx = -s; float ty = 0.0f; float tz = c;
            PushVert(vertices, r * c, y, r * s, u, v, c, 0, s, tx, ty, tz);
        }
    }

    int ringVertexCount = sliceCount + 1;
    for (int i = 0; i < stackCount; ++i) {
        for (int j = 0; j < sliceCount; ++j) {
            uint32_t i0 = i * ringVertexCount + j;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = (i + 1) * ringVertexCount + j;
            uint32_t i3 = i2 + 1;
            indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
            indices.push_back(i2); indices.push_back(i1); indices.push_back(i3);
        }
    }

    uint32_t baseIndex = (uint32_t)vertices.size();
    float y = 0.5f * height; float dTheta = 2.0f * PI / sliceCount;

    PushVert(vertices, 0, y, 0, 0.5f, 0.5f, 0, 1, 0, 1, 0, 0);
    for (int i = 0; i <= sliceCount; ++i) {
        float x = topRadius * cosf(i * dTheta); float z = topRadius * sinf(i * dTheta);
        float u = x / height + 0.5f; float v = z / height + 0.5f;
        PushVert(vertices, x, y, z, u, v, 0, 1, 0, 1, 0, 0);
    }
    for (int i = 0; i < sliceCount; ++i) {
        indices.push_back(baseIndex); indices.push_back(baseIndex + i + 1); indices.push_back(baseIndex + i + 2);
    }
    
    baseIndex = (uint32_t)vertices.size();
    y = -0.5f * height;
    PushVert(vertices, 0, y, 0, 0.5f, 0.5f, 0, -1, 0, 1, 0, 0);
    for (int i = 0; i <= sliceCount; ++i) {
        float x = bottomRadius * cosf(i * dTheta); float z = bottomRadius * sinf(i * dTheta);
        float u = x / height + 0.5f; float v = z / height + 0.5f;
        PushVert(vertices, x, y, z, u, v, 0, -1, 0, 1, 0, 0);
    }
    for (int i = 0; i < sliceCount; ++i) {
        indices.push_back(baseIndex); indices.push_back(baseIndex + i + 2); indices.push_back(baseIndex + i + 1);
    }

    Mesh* mesh = new Mesh();
    mesh->Initialize(device, cmdQueue, vertices, indices);
    return mesh;
}