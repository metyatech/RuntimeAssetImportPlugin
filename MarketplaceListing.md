# Fab Marketplace Listing

## Product Details

```text
Product Title: Runtime Asset Import
Category: Tools & Plugins
Subcategory: Engine Tools
Distribution: Unreal Engine Code Plugin
License: Fab Standard License
Personal Price: $29.99
Professional Price: $79.99
Version: 1.0.0
Supported Target Platform: Win64
Mature Content: No
Generated with AI: Yes
Allows usage with AI: Yes
EDC forum post: No
Publication after approval: Manual activation
```

## Short Description

Import static 3D meshes such as FBX, OBJ, glTF/GLB, and DAE at runtime on Win64. Load from files or memory using Blueprint or C++, then build DynamicMesh or ProceduralMesh components.

## Long Description

Runtime Asset Import loads static 3D mesh files at runtime in packaged Unreal Engine applications on Windows (Win64). Use Blueprint or C++ to import from a file path or an in-memory byte array, then create a Dynamic Mesh Component hierarchy or a Procedural Mesh Component hierarchy.

Tested formats:

- FBX
- OBJ
- glTF / GLB
- DAE (Collada)

Key features:

- Runtime static mesh import without cooking the source model
- Blueprint and C++ APIs
- File and memory input
- Hierarchical node transforms and multiple mesh sections
- Diffuse color and one embedded diffuse/base-color texture per material
- Collision generation on constructed mesh components
- Assimp 6.0.5 Win64 binaries built from the official source tag and bundled with the plugin

Important limitations:

- Win64 only
- Import is synchronous and can block the calling thread for large files
- Static geometry only; skeletal meshes, animations, morph targets, and LOD import are not supported
- External texture references are not loaded
- The first texture, first UV channel, and first vertex-color channel are used
- Runtime-created meshes are local and are not automatically replicated
- ProceduralMeshComponent can cause movement/network issues in multiplayer; DynamicMeshComponent is recommended

## Technical Details

```text
Code Modules: RuntimeAssetImport (Runtime)
Blueprint Assets: 0
Blueprint-callable C++ Functions: 6
Network Replicated: No
Development Platform: Windows Win64
Target Platform: Windows Win64
Third Party: Assimp 6.0.5, BSD-3-Clause
Documentation: https://github.com/metyatech/RuntimeAssetImportPlugin/blob/master/README.md
Support: https://github.com/metyatech/RuntimeAssetImportPlugin/issues
Example Project: None for the initial release
Test Host: https://github.com/metyatech/RuntimeAssetImportSample
```

## Verified Engine Versions

- Unreal Engine 5.4
- Unreal Engine 5.5
- Unreal Engine 5.6
- Unreal Engine 5.7
- Unreal Engine 5.8

## Tags

`runtime`, `import`, `mesh`, `fbx`, `obj`, `gltf`, `glb`, `dae`, `assimp`, `dynamic mesh`, `procedural mesh`, `modding`, `blueprint`
