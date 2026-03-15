# Catalyst Engine 🚀

**Catalyst Engine** is a modern, custom-built 3D rendering engine powered by **DirectX 12** and **C++20**. It is designed around high-performance, GPU-driven rendering techniques, featuring a fully bindless resource architecture and compute-based frustum culling.

## ✨ Key Features

* **GPU-Driven Rendering Pipeline:** Utilizes Compute Shaders (`QuantaCull.hlsl`) for per-instance frustum culling, feeding directly into `ExecuteIndirect` for zero-CPU-overhead draw calls.
* **Bindless Architecture:** A dynamic, unified Descriptor Heap manager that handles all textures, materials, and resources bindlessly, bypassing traditional DX12 slot binding bottlenecks.
* **Modern Lighting & Environments:** Supports directional lighting, shadow mapping, and HDR skybox rendering. 
* **Integrated UI & Editor:** Fully integrated with **Dear ImGui**, featuring real-time scene hierarchy management, object manipulation, and precise mouse-picking/raycasting for selecting 3D geometry in the viewport.
* **Modular Render Passes:** * `QuantaMeshPass`: The core GPU-driven geometry pipeline.
  * `ShadowPass`: Handles depth rendering for directional light shadows.
  * `PostProcessPass`: Manages full-screen effects and render target transitions.
  * `PreviewPass`: Renders UI overlays and editor-specific meshes.

## 🛠️ Tech Stack & Dependencies

* **Language:** C++20
* **Graphics API:** DirectX 12 (Shader Model 5.1+)
* **Math Library:** DirectXMath
* **Texture Processing:** [DirectXTex](https://github.com/microsoft/DirectXTex)
* **UI:** [Dear ImGui](https://github.com/ocornut/imgui)

## 🚀 Getting Started

### Prerequisites
* Windows 10/11
* Visual Studio 2022 or JetBrains Rider
* Windows 10 SDK (for DirectX 12 headers and libraries)

### Building the Project
1. Clone the repository.
2. Open `Catalyst.sln` in Visual Studio or JetBrains Rider.
3. Ensure the active build configuration is set to `Debug` or `Release` for `x64`.
4. **Important Note on Shaders:** The engine uses `D3DCompileFromFile` to compile HLSL shaders at runtime. Ensure that all `.hlsl` files in the `Shaders/` folder have their Build Action / Item Type set to **`None`** (or "Does not participate in build") in your IDE to prevent the build system from attempting to compile them as static Vertex Shaders.
5. Build and Run (`F5`).

## 🎮 Controls
* **Left Mouse Click:** Select objects in the 3D viewport (Raycasting).
* **ImGui Panels:** Tweak object transforms, lighting intensity, and materials in real-time.

## 🚧 Roadmap / Current Focus
* Stabilizing Compute Shader culling and offset memory alignments.
* Expanding the Material system (PBR properties).
* Enhancing the Post-Processing volume stack.