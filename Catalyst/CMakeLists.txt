# Engine (editor) target — mirrors Catalyst.vcxproj's x64 Debug/Release
# configuration. CatalystPlayer, Catalyst.Native and the C# Launcher are not
# built by this file; they remain MSBuild-only.

set(CATALYST_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Lib")
set(CATALYST_DIRECTXTEX_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../packages/directxtex_desktop_win10.2025.10.28.1"
    CACHE PATH "Path to the directxtex_desktop_win10 NuGet package")

if(NOT EXISTS "${CATALYST_DIRECTXTEX_DIR}/include/DirectXTex.h")
    message(FATAL_ERROR
        "DirectXTex NuGet package not found at ${CATALYST_DIRECTXTEX_DIR}. "
        "Restore NuGet packages for Catalyst.vcxproj (packages.config), or "
        "set -DCATALYST_DIRECTXTEX_DIR=<path>.")
endif()

add_executable(Catalyst WIN32
    # Entry point / app
    main.cpp
    EngineApp.cpp
    Launcher.cpp
    Catalyst.rc

    # Blueprint editor
    Blueprint/BlueprintEditor.cpp
    Blueprint/BlueprintEditor_AssetBrowser.cpp
    Blueprint/BlueprintEditor_IO.cpp
    Blueprint/BlueprintEditor_Labels.cpp
    Blueprint/BlueprintEditorDraw.cpp
    Blueprint/BlueprintEditorGraph.cpp
    Blueprint/BlueprintEditorInteraction.cpp
    Blueprint/CookedUIBlueprint.cpp
    Blueprint/Nodes/BlueprintNodeLibrary.cpp

    # Build
    "Build/BuildAndRun.cpp"

    # Core Render
    "Core Render/BindlessManager.cpp"
    "Core Render/DXRenderer.cpp"
    "Core Render/RenderTypes.cpp"
    "Core Render/Passes/AssetPreviewPass.cpp"
    "Core Render/Passes/PostProcessPass.cpp"
    "Core Render/Passes/QuantaMeshPass.cpp"
    "Core Render/Passes/RaytracePass.cpp"
    "Core Render/Passes/ShadowPass.cpp"
    "Core Render/Passes/SkyboxPass.cpp"

    # Error Handler
    "Error Handler/Common.cpp"
    "Error Handler/OutputLog.cpp"

    # Physics
    Physics/PhysicsSystem.cpp
    Physics/PhysicsTypes.cpp

    # Resources
    Resources/FastObjParser.cpp
    Resources/FbxParser.cpp
    Resources/Inflate.cpp
    Resources/Material.cpp
    Resources/Mesh.cpp
    Resources/MeshBuild.cpp
    Resources/MeshSimplify.cpp
    Resources/MeshletBuilder.cpp
    Resources/ModelLoader.cpp
    Resources/PakFile.cpp
    Resources/PolygonTriangulate.cpp
    Resources/PrimitiveGenerator.cpp
    Resources/Texture.cpp

    # Scene
    Scene/Asset.cpp
    Scene/Camera.cpp
    Scene/GameObject.cpp

    # Scripting
    Scripting/ScriptModuleHost.cpp

    # UI
    UI/EditorSettings.cpp
    UI/EditorUI.cpp
    UI/EditorUIDragDrop.cpp
    UI/EditorUIEditor.cpp
    UI/EditorUILauncher.cpp
    UI/EditorUISettings.cpp
    UI/EditorUIViewers.cpp
    UI/UIContext.cpp
    UI/UIContext_Gizmo.cpp
    UI/UIContext_Widgets.cpp
    UI/Input/InputManager.cpp
    UI/Render/FontManager.cpp
    UI/Render/UIDrawList.cpp
    UI/Render/UIDrawList_Draw.cpp
    UI/Render/UIRenderer.cpp
    UI/Render/UIRenderer_Pipeline.cpp
    UI/XML/UIXMLParser.cpp
    UI/XML/UIXMLRuntime.cpp
    UI/XML/UIXMLStyleSheet.cpp

    # Shared Lib
    "${CATALYST_LIB_DIR}/Json.cpp"
    "${CATALYST_LIB_DIR}/ProjectFields.cpp"
    "${CATALYST_LIB_DIR}/tinyxml2/tinyxml2.cpp"
)

target_include_directories(Catalyst PRIVATE
    "Core Render"
    "Core Render/Passes"
    "Error Handler"
    "UI/Render"
    "UI/Input"
    "UI/XML"
    "UI"
    "Window Creation"
    "Scene"
    "Resources"
    "${CATALYST_LIB_DIR}"
    "${CATALYST_LIB_DIR}/tinyxml2"
    "${CATALYST_LIB_DIR}/imgui/backends"
    "${CATALYST_LIB_DIR}/imgui"
    "${CATALYST_DIRECTXTEX_DIR}/include"
)

target_compile_definitions(Catalyst PRIVATE
    UNICODE
    _UNICODE
    _CONSOLE
    HAS_DIRECTXTEX
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

if(MSVC)
    target_compile_options(Catalyst PRIVATE /W3 /sdl /permissive-)
    target_compile_options(Catalyst PRIVATE $<$<CONFIG:Release>:/Gy /Oi>)
    set_target_properties(Catalyst PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

target_link_directories(Catalyst PRIVATE
    "${CATALYST_DIRECTXTEX_DIR}/native/lib/x64/$<IF:$<CONFIG:Debug>,Debug,Release>"
)

target_link_libraries(Catalyst PRIVATE
    d3d12.lib
    dxgi.lib
    d3dcompiler.lib
    dxguid.lib
    DirectXTex.lib
)

set_target_properties(Catalyst PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../bin/x64/$<CONFIG>"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Mirror the MSBuild project's CopyToOutputDirectory shader files.
add_custom_command(TARGET Catalyst POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Catalyst>/Shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/shaders.hlsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Skybox.hlsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Raytrace.hlsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Raytrace.cso"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/postprocess.hlsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/shadow.hlsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/QuantaMesh.hlsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/UI.hlsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/QuantaCull.hlsl"
        "$<TARGET_FILE_DIR:Catalyst>/Shaders"
)
