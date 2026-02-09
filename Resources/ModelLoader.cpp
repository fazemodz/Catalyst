#include "ModelLoader.h"
#include <iostream>
#include <vector>

#define TINYOBJLOADER_IMPLEMENTATION
#include "../Lib/tiny_obj_loader.h" 

using namespace DirectX;

Mesh* ModelLoader::Load(const std::string& filepath, ID3D12Device* device) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filepath.c_str())) {
        throw std::runtime_error("Failed to load model: " + filepath);
    }

    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex = {};

            // 1. Position
            vertex.position.x = attrib.vertices[3 * index.vertex_index + 0];
            vertex.position.y = attrib.vertices[3 * index.vertex_index + 1];
            vertex.position.z = attrib.vertices[3 * index.vertex_index + 2];

            // 2. Color (White default)
            vertex.color = { 1.0f, 1.0f, 1.0f, 1.0f };

            // 3. UVs
            if (index.texcoord_index >= 0) {
                vertex.uv.x = attrib.texcoords[2 * index.texcoord_index + 0];
                vertex.uv.y = 1.0f - attrib.texcoords[2 * index.texcoord_index + 1]; 
            }

            // 4. Normals 
            if (index.normal_index >= 0) {
                vertex.normal.x = attrib.normals[3 * index.normal_index + 0];
                vertex.normal.y = attrib.normals[3 * index.normal_index + 1];
                vertex.normal.z = attrib.normals[3 * index.normal_index + 2];
            } else {
                // Fallback if model has no normals (Point Up)
                vertex.normal = { 0.0f, 1.0f, 0.0f };
            }

            vertices.push_back(vertex);
            indices.push_back(static_cast<uint16_t>(indices.size())); 
        }
    }

    Mesh* newMesh = new Mesh();
    newMesh->Initialize(device, vertices, indices);
    return newMesh;
}