# Catalyst Engine

A DirectX 12 game engine and editor written from scratch in C++20. Windows x64 only.

There is no third-party engine code in the runtime. Geometry parsing, DEFLATE,
mesh simplification, cluster building, the immediate-mode UI, the font atlas, the
physics solver, and the scene serializer are all in this repository. The only
libraries linked in are DirectXTex (texture and HDR decode), tinyxml2 (editor
layout and theme files), and a small in-house JSON reader/writer in `Lib/`.

Three binaries come out of the solution, plus one shared native library:

| Binary | Project | What it is |
| --- | --- | --- |
| `Catalyst.exe` | `Catalyst/Catalyst.vcxproj` | The editor. |
| `CatalystPlayer.exe` | `Catalyst/CatalystPlayer.vcxproj` | The same codebase built with `CATALYST_PLAYER`, booting straight into play mode against a staged `data.pak`. |
| `Catalyst Launcher.exe` | `Catalyst Launcher/` | .NET 10 / WPF project browser. Has [its own README](Catalyst%20Launcher/README.md). |
| `Catalyst.Native.dll` | `Catalyst/Catalyst.Native/Catalyst.Native.vcxproj` | Small C API built from the same `Lib/ProjectFields.cpp` the editor uses to read `.CatalystProj` files, so the launcher can P/Invoke into the engine's own parsing instead of carrying a second implementation. |

---

## Building

Requirements:

- Visual Studio 2022 (toolset v143) with the Windows 10/11 SDK
- .NET 10 SDK, for the launcher
- A GPU with D3D12 feature level 11_0. Raytracing needs DXR tier 1.1 but is optional.

Open `Catalyst.slnx` in Visual Studio or Rider and build, or from a developer prompt:

```bash
msbuild Catalyst.slnx -p:Configuration=Debug -p:Platform=x64 -restore
```

`-restore` matters on a fresh clone: `Catalyst/packages.config` pulls the
DirectXTex package, and the build fails without it.

Everything lands flat in `bin/x64/{Debug,Release}/`, engine and launcher together.

### Shaders

HLSL under `Catalyst/Shaders/` is compiled at startup with FXC, so editing a
shader only needs a restart, not a rebuild. `ShaderCompiler.h` resolves
`Shaders/` by walking up from the working directory and from the exe's own
folder, which is why a dev build run from the project folder and a staged build
run from its own directory both find their shaders.

`Raytrace.hlsl` is the exception. It needs shader model 6.5, which FXC cannot
emit, so it ships precompiled and has to be rebuilt by hand after an edit:

```bash
dxc -T cs_6_5 -E CSMain Shaders/Raytrace.hlsl -Fo Shaders/Raytrace.cso
```

---

## Running

`Catalyst.exe` opens on its own project launcher screen. Command line arguments,
used by the WPF launcher and by staged builds:

| Argument | Effect |
| --- | --- |
| `--project <path.CatalystProj>` | Open a project and load its startup scene. |
| `--map <path.catalystmap>` | Open a specific scene instead of the startup one. |
| `--play` | Skip the editor and start in play mode. |

The engine resolves `Shaders/` and `Assets/` against the working directory, so
start it from a folder that contains both.

---

## Rendering

`Core Render/DXRenderer.cpp` owns the device, swap chain, and frame loop, and
drives a set of standalone passes under `Core Render/Passes/`. Everything a
shader reads lives in one CBV/SRV/UAV heap managed by `BindlessManager`, which
hands out indices and recycles slots when an asset is reloaded. Per-frame CPU
writes are double-buffered, so a frame never has to stall to reuse a constant
buffer.

### Virtualised geometry

The main geometry path — "Quanta" — draws clusters, not meshes.

At import time a mesh is split into clusters of at most 128 triangles. Groups of
eight clusters are merged, simplified together with their shared rims locked, and
split again into the next level up. Repeating that builds a DAG of increasingly
coarse levels, all concatenated into one index buffer with a cluster table
describing the runs.

At draw time `QuantaCull.hlsl` runs one thread per cluster per instance. Each
thread decides on its own whether its slice is worth drawing this frame — frustum
test, backface cone test, and a screen-space error test that picks the level
whose simplification error stays under the pixel budget — and the survivors are
written out as indirect draw arguments. A dense model costs what is visible, not
what it contains.

All three tests and the error threshold are exposed on `QuantaMeshPass` and
editable from the editor, and the pass reports back how many clusters survived.

### Other passes

- **ShadowPass** — one 2048² directional depth map, PCF filtered, with bias and
  kernel measured in world-space texels so they stay stable as the light moves.
- **RaytracePass** — DXR 1.1 inline `RayQuery`. Reads the raster G-buffer and
  traces one shadow ray and one reflection ray per pixel into two screen-space
  targets. Entirely opt-in: if the adapter is below tier 1.1 or `Raytrace.cso` is
  missing, `IsAvailable()` stays false, the reason goes to the output log, and
  the pure raster path runs unchanged.
- **SkyboxPass** — equirectangular HDR environment, loaded through DirectXTex.
- **PostProcessPass** — renders the scene to an HDR target, then composites
  exposure, colour tint, and bloom, folding in the raytraced shadow mask and
  reflections when they exist.
- **AssetPreviewPass** — renders thumbnails for the content browser.

Shading is metallic/roughness PBR with albedo, normal, metallic, roughness, and
AO slots per material, each an index into the bindless heap.

---

## Importing meshes

`Resources/` holds a complete import pipeline:

- **OBJ** — `FastObjParser` memory-maps the file and parses it across every
  hardware thread, fanning polygons to triangles as it goes.
- **Binary FBX** — `FbxParser` reads the node tree and bakes each node's world
  transform into its vertices. FBX stores its large arrays as zlib streams, so
  `Inflate.cpp` implements RFC 1951/1950 rather than pulling in a dependency.
  ASCII FBX is detected and reported as unsupported instead of misparsed.
- **Reported, not applied** — an FBX's declared unit scale and up axis are
  surfaced in the import dialog rather than silently applied, so a centimetre or
  Z-up export gets corrected on purpose.

`MeshImportOptions` covers uniform scale, up axis, normal mode (import / flat /
smooth), UV flip, winding flip, tangent generation, vertex welding, cache
optimisation, pivot centring, vertex colours, and whether to build the cluster
hierarchy. Every option is folded into a hash that keys the mesh cache, so
changing one invalidates stale geometry automatically.

Imported meshes are written to a binary `.catalystactor` so later loads are a
straight read. Projects still holding raw OBJ text get a cache under
`%LOCALAPPDATA%` instead, keyed on source path, size, write time, and the options
hash — no scratch files in the user's `Assets/` folder.

---

## The editor

The editor UI is custom immediate mode, built on `UIDrawList`, `FontManager`, and
`UIRenderer` under `UI/`. Panel sizes and colours come from two XML files that can
be copied next to `Catalyst.exe` to override the defaults:

- `editor_layout.xml` — menubar, toolbar, outliner, viewport, details, bottom bar
- `editor_theme.xmlstyle` — launcher, editor, and widget palettes

Panels are the familiar set: Outliner, Details, Viewport with a transform gizmo,
Content Browser with thumbnails and drag-and-drop, Output Log, and a Place Actors
menu backed by the procedural primitive generator. Actor viewers, the material
editor, and the blueprint editor can each be torn off into their own window —
`EngineApp` runs a renderer and input manager per window.

---

## Blueprints

`Blueprint/` is a node graph editor with two asset kinds. Actor blueprints
(`.catalystblueprint`) carry a component list — static mesh, skeletal mesh,
camera, trigger — plus a construction script and an event graph. UI blueprints
(`.catalystuiblueprint`) build canvases, buttons, images, and text blocks, and are
cooked into a runtime form the renderer draws over the viewport in play mode.

The node library is small and gameplay-focused: player character controller,
space jump, open level, create widget, add to viewport, swap widget, set text
colour, print to log.

---

## Native scripting

Game logic is a C++ DLL. The editor generates `Code/GameLogic.sln`; you derive
from `Catalyst::NativeScript`, register classes with `REGISTER_SCRIPT`, and build
`GameLogic.dll` into the project's `Binaries/` folder.

`ScriptModuleHost` shadow-copies the DLL before loading it, so a rebuild is never
blocked by the running editor. It watches the source DLL, and on reload each live
script gets `OnHotReloadSave` before the swap and `OnHotReloadLoad` after, so
transient state survives.

The API surface — lifecycle, input actions and axes, transforms, physics queries,
collision and trigger callbacks, timers, save slots — is documented in
[Docs/NativeScriptAPI.md](Catalyst/Docs/NativeScriptAPI.md), and the header is
[`Scripting/CatalystAPI.h`](Catalyst/Scripting/CatalystAPI.h).

---

## Physics

`PhysicsSystem` is a fixed-step solver, 1/60 s by default, driven by an
accumulator. Box and sphere colliders, triggers, and rigidbodies with gravity,
mass, damping, and restitution. Collision and overlap events are queued and
dispatched to native scripts after the step.

---

## Projects and shipping

A project is a `.CatalystProj` JSON file next to this skeleton:

```
Assets/            Meshes, textures, materials, maps
Code/              GameLogic.sln and its sources
  Scripts/
Binaries/
  Debug/           GameLogic.dll
  Release/
Intermediate/
Config/            StartupScene.catalystmap
```

**Build and Run** (`Build/BuildAndRun.cpp`) locates MSBuild — common install
paths first, then `vswhere` — builds `CatalystPlayer.vcxproj`, and stages a
standalone folder: the player exe, `Shaders/`, `Assets/` with the project's own
assets copied over the engine defaults, and a `data.pak` holding the scene and
manifest. Then it launches the staged exe straight into play mode.

### File formats

| Extension | Contents |
| --- | --- |
| `.CatalystProj` | Project file, JSON |
| `.catalystmap` | Scene |
| `.catalystactor` | Imported mesh, binary, with cluster hierarchy |
| `.catalystmat` | Material |
| `.catalystblueprint` | Actor blueprint |
| `.catalystuiblueprint` | UI blueprint |
| `.catalystsave` | Runtime save slot |
| `.pak` | Staged build data |

---

## Not wired up yet

Worth knowing before you go looking for the code:

- **Dear ImGui is dead code.** `Lib/imgui/` is still vendored, and `UI/UIManager.*`,
  `UI/Theme.*`, and `Window Creation/Window.cpp` still reference it, but none of
  those files are in the build. The editor moved to its own immediate-mode UI and
  the ImGui-era files were left behind rather than deleted.
- **`Lib/stb_image.h` and `Lib/tiny_obj_loader.h` are unreferenced.** Both were
  replaced by the in-house parsers and DirectXTex.
- **Skeletal meshes are static meshes.** The component kind exists in the
  blueprint editor and loads geometry, but there is no skinning or skeleton.
- **Animation is storage only.** `animationState` and `animationTrigger` are
  serialized with the scene and readable from scripts; nothing plays anything.
- **The launcher's sign-in is a stub**, and its templates all produce the same
  blank skeleton. See its README.
- Older commits describe a Blinn-Phong / ImGui / tiny_obj_loader engine. That
  README was removed when the renderer was rewritten; this one replaces it.
