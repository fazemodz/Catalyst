# Catalyst Engine

**Catalyst Engine** is a modern, custom-built 3D game engine powered by **DirectX 12** and **C++20**. It features a full editor, GPU-driven rendering, visual scripting, and a standalone player runtime for shipping games.

## Key Features

* **GPU-Driven Rendering Pipeline:** Compute Shaders (`QuantaCull.hlsl`) perform per-instance frustum culling, feeding directly into `ExecuteIndirect` for zero-CPU-overhead draw calls.
* **Bindless Architecture:** A dynamic, unified Descriptor Heap manager handles all textures, materials, and resources bindlessly, bypassing traditional DX12 slot binding bottlenecks.
* **Blueprint Editor:** Visual node-based UI designer for building in-game interfaces. Blueprints compile to a binary `CookedUIBlueprint` format with opcodes for animations, level transitions, and widget swaps.
* **Native C++ DLL Scripting:** Each project compiles a `GameLogic.dll` from user scripts. The engine hot-reloads it during editing using shadow-copy to avoid file locks.
* **Standalone Player Runtime:** `CatalystPlayer.exe` is a separate stripped-down runtime. The editor packages your project — assets, shaders, and DLL — into `.pak` archives and stages a playable build with one click.
* **Physics System:** Colliders, rigidbodies, triggers, raycasts, and overlap queries with full editor integration.
* **Modern Lighting & Environments:** Directional lighting, shadow mapping, HDR skybox rendering.
* **Integrated Editor UI:** Real-time scene hierarchy, object transforms, mouse-picking/raycasting, material editor, asset browser, and multi-window support.
* **Modular Render Passes:**
  * `QuantaMeshPass` — GPU-driven geometry pipeline
  * `ShadowPass` — Depth rendering for directional shadows
  * `PostProcessPass` — Full-screen effects and render target transitions
  * `PreviewPass` — UI overlays and editor-specific meshes

## Tech Stack

* **Language:** C++20
* **Graphics API:** DirectX 12 (Shader Model 5.1+)
* **Math Library:** DirectXMath
* **Texture Processing:** [DirectXTex](https://github.com/microsoft/DirectXTex)
* **UI:** [Dear ImGui](https://github.com/ocornut/imgui)

## Getting Started

### Prerequisites
* Windows 10/11
* Visual Studio 2022 (v143 toolset)
* Windows 10 SDK (for DirectX 12 headers and libraries)

### Building

1. Clone the repository.
2. Open `Catalyst.sln` in Visual Studio 2022.
3. Select the `Catalyst` project, set configuration to `Debug x64` or `Release x64`.
4. **Shaders:** All `.hlsl` files in `Shaders/` must have their Item Type set to **`None`** in project properties — the engine compiles them at runtime via `D3DCompileFromFile`.
5. Build and Run (`F5`).

The solution contains two projects:

| Project | Output | Purpose |
|---|---|---|
| `Catalyst` | `Catalyst.exe` | Full editor with all tools |
| `CatalystPlayer` | `CatalystPlayer.exe` | Stripped runtime for shipping games |

### Shipping a Game

From within the editor, use **Build > Package Game**. This:
1. Packs your project's `Assets/` into `.pak` archives (CPAK format).
2. Stages `CatalystPlayer.exe`, shaders, `GameLogic.dll`, and a build manifest into `/builds/`.
3. Launches the staged build with `--play --project <name> --map <scene>`.

## Native C++ Scripting

Each project generates a `GameLogic.sln` workspace under `Code/`.

1. Open `GameLogic.sln` from inside the Catalyst editor.
2. Inherit from `Catalyst::NativeScript` in `Code/Scripts/*.cpp`.
3. Register each class with `REGISTER_SCRIPT(ClassName)`.
4. Build the DLL — the editor hot-reloads it automatically.
5. Assign script components to objects from the Details panel and press Play.

See `Docs/NativeScriptAPI.md` for the full API reference covering input, physics, rendering, UI, audio, timers, save data, and animations.

## Controls

* **Left Mouse Click:** Select objects in the 3D viewport (raycasting).
* **ImGui Panels:** Tweak transforms, lighting, materials, and physics in real-time.

## Roadmap

* Stabilizing Compute Shader culling and offset memory alignments.
* Expanding the Material system (PBR properties).
* Enhancing the Post-Processing volume stack.
* Audio system integration.
