#include "ModelLoader.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h" 

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <unordered_map>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace {
struct ObjIndexKey {
    int vertexIndex = -1;
    int texcoordIndex = -1;
    int normalIndex = -1;

    bool operator==(const ObjIndexKey& other) const {
        return vertexIndex == other.vertexIndex &&
               texcoordIndex == other.texcoordIndex &&
               normalIndex == other.normalIndex;
    }
};

struct ObjIndexKeyHash {
    size_t operator()(const ObjIndexKey& key) const {
        const size_t h1 = std::hash<int>{}(key.vertexIndex);
        const size_t h2 = std::hash<int>{}(key.texcoordIndex);
        const size_t h3 = std::hash<int>{}(key.normalIndex);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

MeshData LoadMeshDataFromPath(const fs::path& filepath) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;

    const std::string pathString = filepath.string();
    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, pathString.c_str())) {
        throw std::runtime_error("Failed to load model: " + pathString);
    }

    MeshData meshData;
    size_t totalIndexCount = 0;
    for (const auto& shape : shapes) {
        totalIndexCount += shape.mesh.indices.size();
    }

    meshData.Vertices.reserve(totalIndexCount);
    meshData.Indices.reserve(totalIndexCount);

    std::unordered_map<ObjIndexKey, uint32_t, ObjIndexKeyHash> vertexCache;
    vertexCache.reserve(totalIndexCount);

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            const ObjIndexKey key{index.vertex_index, index.texcoord_index, index.normal_index};
            const auto existingVertex = vertexCache.find(key);
            if (existingVertex != vertexCache.end()) {
                meshData.Indices.push_back(existingVertex->second);
                continue;
            }

            Vertex vertex = {};
            if (index.vertex_index >= 0 && (3 * index.vertex_index + 2) < static_cast<int>(attrib.vertices.size())) {
                vertex.position = {
                    attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]
                };
            }

            vertex.color = {1.0f, 1.0f, 1.0f, 1.0f};

            if (index.texcoord_index >= 0 && (2 * index.texcoord_index + 1) < static_cast<int>(attrib.texcoords.size())) {
                vertex.uv = {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                };
            }

            if (index.normal_index >= 0 && (3 * index.normal_index + 2) < static_cast<int>(attrib.normals.size())) {
                vertex.normal = {
                    attrib.normals[3 * index.normal_index + 0],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2]
                };
            } else {
                vertex.normal = {0.0f, 1.0f, 0.0f};
            }

            vertex.tangent = {1.0f, 0.0f, 0.0f};

            const uint32_t vertexIndex = static_cast<uint32_t>(meshData.Vertices.size());
            meshData.Vertices.push_back(vertex);
            meshData.Indices.push_back(vertexIndex);
            vertexCache.emplace(key, vertexIndex);
        }
    }

    return meshData;
}

Mesh* CreateUploadedMesh(const MeshData& meshData, ID3D12Device* device, ID3D12CommandQueue* cmdQueue) {
    if (!device || !cmdQueue || meshData.Vertices.empty() || meshData.Indices.empty()) {
        return nullptr;
    }

    ComPtr<ID3D12CommandAllocator> commandAllocator;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)))) {
        throw std::runtime_error("Failed to create mesh upload command allocator");
    }

    ComPtr<ID3D12GraphicsCommandList> commandList;
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)))) {
        throw std::runtime_error("Failed to create mesh upload command list");
    }

    std::unique_ptr<Mesh> mesh = std::make_unique<Mesh>(
        device,
        commandList.Get(),
        meshData.Vertices.data(),
        meshData.Vertices.size(),
        meshData.Indices.data(),
        meshData.Indices.size());

    if (FAILED(commandList->Close())) {
        throw std::runtime_error("Failed to close mesh upload command list");
    }

    ID3D12CommandList* commandLists[] = {commandList.Get()};
    cmdQueue->ExecuteCommandLists(1, commandLists);

    ComPtr<ID3D12Fence> fence;
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
        throw std::runtime_error("Failed to create mesh upload fence");
    }

    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent) {
        throw std::runtime_error("Failed to create mesh upload fence event");
    }

    constexpr UINT64 fenceValue = 1;
    if (FAILED(cmdQueue->Signal(fence.Get(), fenceValue))) {
        CloseHandle(fenceEvent);
        throw std::runtime_error("Failed to signal mesh upload fence");
    }

    if (fence->GetCompletedValue() < fenceValue) {
        if (FAILED(fence->SetEventOnCompletion(fenceValue, fenceEvent))) {
            CloseHandle(fenceEvent);
            throw std::runtime_error("Failed to wait for mesh upload fence");
        }
        WaitForSingleObject(fenceEvent, INFINITE);
    }

    CloseHandle(fenceEvent);
    return mesh.release();
}
}

MeshData ModelLoader::LoadMeshData(const std::string& filepath) {
    return LoadMeshDataFromPath(fs::path(filepath));
}

MeshData ModelLoader::LoadMeshData(const std::wstring& filepath) {
    return LoadMeshDataFromPath(fs::path(filepath));
}

Mesh* ModelLoader::Load(const std::string& filepath, ID3D12Device* device, ID3D12CommandQueue* cmdQueue) {
    const MeshData meshData = LoadMeshData(filepath);
    if (meshData.Vertices.empty() || meshData.Indices.empty()) {
        throw std::runtime_error("Imported mesh contains no renderable geometry: " + filepath);
    }

    return CreateUploadedMesh(meshData, device, cmdQueue);
}
