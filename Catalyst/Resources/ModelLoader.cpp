#include "ModelLoader.h"

#include <filesystem>
#include <memory>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace {

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

    // Rides the same command list as the geometry copy, so it is covered by the
    // fence wait below.
    mesh->UploadClusters(device, commandList.Get(), meshData.Clusters,
                         meshData.ClusterLevelCount, meshData.BaseTriangleCount);

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

    // The copy has landed, so the staging copy can go back to the heap. On a
    // dense mesh that is hundreds of megabytes that would otherwise be held
    // for the lifetime of the model.
    mesh->ReleaseUploadBuffers();
    return mesh.release();
}

}

MeshData ModelLoader::LoadMeshData(const std::string& filepath) {
    return LoadMeshData(fs::path(filepath).wstring());
}

MeshData ModelLoader::LoadMeshData(const std::wstring& filepath) {
    return LoadMeshData(filepath, MeshImportOptions());
}

MeshData ModelLoader::LoadMeshData(const std::wstring& filepath, const MeshImportOptions& options) {
    return CatalystImport::ImportMeshFromSource(filepath, options, nullptr);
}

bool ModelLoader::ConvertToAsset(const std::wstring& sourcePath,
                                 const std::wstring& destinationPath,
                                 const MeshImportOptions& options,
                                 CatalystImport::MeshBuildStats* outStats,
                                 std::string* outError) {
    try {
        // Writing the converted geometry over the model it was read from would
        // destroy the source, so refuse rather than trust the caller.
        std::error_code compareError;
        if (fs::exists(fs::path(destinationPath)) &&
            fs::equivalent(fs::path(sourcePath), fs::path(destinationPath), compareError) && !compareError) {
            if (outError != nullptr) {
                *outError = "The destination is the same file as the source model.";
            }
            return false;
        }

        CatalystImport::MeshBuildStats stats;
        // The destination file below is the permanent copy, so there is no
        // point also filling the scratch cache with the same geometry.
        const MeshData meshData = CatalystImport::ImportMeshFromSource(sourcePath, options, &stats, false);
        if (meshData.Vertices.empty() || meshData.Indices.empty()) {
            if (outError != nullptr) {
                *outError = "The model contains no renderable geometry.";
            }
            return false;
        }

        std::error_code directoryError;
        fs::create_directories(fs::path(destinationPath).parent_path(), directoryError);

        // The options are baked into the written geometry, so the stamp here is
        // only a record of what it came from.
        if (!CatalystImport::WriteMeshBinary(destinationPath, meshData, 0, 0, options.Hash(), outError)) {
            return false;
        }

        if (outStats != nullptr) {
            *outStats = stats;
        }
        return true;
    } catch (const std::exception& exception) {
        if (outError != nullptr) {
            *outError = exception.what();
        }
        return false;
    } catch (...) {
        if (outError != nullptr) {
            *outError = "The importer failed for an unknown reason.";
        }
        return false;
    }
}

Mesh* ModelLoader::Load(const std::string& filepath, ID3D12Device* device, ID3D12CommandQueue* cmdQueue) {
    const MeshData meshData = LoadMeshData(filepath);
    if (meshData.Vertices.empty() || meshData.Indices.empty()) {
        throw std::runtime_error("Imported mesh contains no renderable geometry: " + filepath);
    }

    return CreateUploadedMesh(meshData, device, cmdQueue);
}
