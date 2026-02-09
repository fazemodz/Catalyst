# Catalyst Engine 🛠️

**Catalyst** is a custom C++ Game Engine built from scratch using **DirectX 12**. It features a modern, Unreal-style editor interface, real-time lighting, and a component-based architecture designed for learning low-level graphics programming and engine development.



## 🌟 Features (Implemented)

### 🎨 **Core Rendering**
- [x] **DirectX 12 Backend:** Custom D3D12 wrapper handling SwapChains, Command Queues, and Fences.
- [x] **Shader Pipeline:** HLSL Vertex & Pixel shaders with Constant Buffers.
- [x] **Lighting System:** Blinn-Phong lighting model with Directional Lights.
- [x] **Normal Mapping:** Tangent-space normal mapping support for detailed surfaces.
- [x] **Texture Support:** WIC-based loader for `.png`, `.jpg`, and `.bmp`.

### 🏗️ **Editor & UI**
- [x] **ImGui Integration:** Fully interactive UI overlay.
- [x] **Unreal-Style Layout:**
    - **Viewport:** Resizeable 3D rendering area.
    - **Outliner:** Hierarchy view with selection and context menus (Delete/Rename).
    - **Details Panel:** Inspector for editing Transforms, Materials, and Light properties.
    - **Content Browser:** File explorer with thumbnail previews.
- [x] **Gizmos:** Integrated **ImGuizmo** for Translation, Rotation, and Scaling.
- [x] **Drag & Drop:** Drag models and textures from the Content Browser directly into the scene or inspector slots.

### 📐 **Scene Management**
- [x] **GameObject System:** Structure handling Mesh, Transform, and Material data.
- [x] **Camera Controller:** "Fly Mode" camera (Hold Right-Click + WASD).
- [x] **Object Picking:** Mouse picking using Ray-AABB (Axis Aligned Bounding Box) intersection.
- [x] **Procedural Geometry:** Built-in generators for Cubes, Spheres, Cylinders, and Planes.
- [x] **Place Actors Menu:** Dropdown menu to spawn primitives and lights.

---

## 🚀 Roadmap (To Be Done)

### 🔮 **Rendering Upgrades**
- [ ] **PBR Pipeline:** Upgrade from Blinn-Phong to Physically Based Rendering (Metallic/Roughness workflow).
- [ ] **Shadow Mapping:** Implement directional shadow maps (Cascaded Shadow Maps).
- [ ] **Multi-Texturing:** Robust descriptor heap management for binding Albedo, Normal, and Roughness maps simultaneously.
- [ ] **Skybox:** Cube map support for environmental backgrounds.

### ⚙️ **Engine Systems**
- [ ] **Save/Load System:** Serialize scene data to JSON or YAML to persist changes.
- [ ] **Physics Integration:** Replace simple AABB collisions with a real physics engine (e.g., PhysX or Jolt).
- [ ] **Scripting:** Basic C# or Lua scripting support for game logic.
- [ ] **Play Mode:** Separate "Editor State" from "Game State".

---

## 🛠️ Tech Stack
* **Language:** C++ (C++20 Standard)
* **Graphics API:** DirectX 12
* **GUI Library:** Dear ImGui + ImGuizmo
* **Math:** DirectXMath
* **Asset Loading:** tiny_obj_loader (Models), WIC (Textures)

