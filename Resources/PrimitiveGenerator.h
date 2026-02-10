#pragma once
#include <vector>
#include <d3d12.h>
#include "Mesh.h"

class PrimitiveGenerator {
public:
    static Mesh* CreateCube(ID3D12Device* device, ID3D12CommandQueue* cmdQueue);
    static Mesh* CreatePlane(ID3D12Device* device, ID3D12CommandQueue* cmdQueue);
    static Mesh* CreateSphere(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, int slices = 20, int stacks = 20);
    static Mesh* CreateCylinder(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, float bottomRadius = 0.5f, float topRadius = 0.5f, float height = 1.0f, int sliceCount = 20, int stackCount = 1);
};