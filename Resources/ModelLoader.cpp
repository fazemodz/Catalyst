#include "ModelLoader.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "../Lib/tiny_obj_loader.h" 

Mesh* ModelLoader::Load(const std::string& filepath, ID3D12Device* device, ID3D12CommandQueue* cmdQueue) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filepath.c_str())) {
        throw std::runtime_error("Failed to load model: " + filepath);
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex = {};
            
            // Position
            vertex.position = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]
            };
            vertex.color = { 1, 1, 1, 1 };

            // UV
            if (index.texcoord_index >= 0) {
                vertex.uv = {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                };
            }

            // Normal
            if (index.normal_index >= 0) {
                vertex.normal = {
                    attrib.normals[3 * index.normal_index + 0],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2]
                };
            } else { vertex.normal = { 0, 1, 0 }; }

            vertex.tangent = { 1.0f, 0.0f, 0.0f }; // Default Tangent

            vertices.push_back(vertex);
            indices.push_back(static_cast<uint32_t>(indices.size())); 
        }
    }

    Mesh* newMesh = new Mesh();
    newMesh->Initialize(device, cmdQueue, vertices, indices); // Pass Queue
    return newMesh;
}