# RuntimeAssetImportPlugin
## Overview
This is a plugin for the Unreal Engine to load 3D assets (e.g. FBX) at runtime. Made with UE5.4.2.

## Recommended approach: DynamicMesh (stable)

**Use the `DynamicMesh` path for all builds.** The `ConstructDynamicMeshComponentFromAssetFile` and `ConstructDynamicMeshComponentFromMeshData` Blueprint functions are the stable, recommended methods that work correctly in both editor and packaged builds.

> **StaticMesh functions are [Experimental].** `ConstructStaticMeshComponentFromAssetFile` and related StaticMesh functions have a known limitation in packaged builds (no editor): **materials display as a checkerboard pattern**. Avoid using them unless you understand and accept this limitation.

## Prerequisites
- Windows (Win64) is the primary supported platform.

## How to install

1. Copy (or clone) this plugin folder into the `Plugins/` folder of your Unreal Engine project. Create the `Plugins/` folder if it does not exist.
2. Open your project. When prompted "The following modules are missing or built with a different engine version: RuntimeAssetImport", press **Yes** to build.
3. The plugin is now enabled and ready to use.

> **Note:** Win64 prebuilt binaries for assimp are included. No CMake or manual build step is required.

<details>
<summary>Alternative: clone with Git</summary>

In the `Plugins/` folder of your project, run:
```
git clone <URL of this repository>
```
Then open the project and press **Yes** when prompted to build.
</details>

## How to use
Run [Runtime Asset Import Sample](https://github.com/Udon-Tobira/RuntimeAssImpSample).

## Supported File Formats
The following file formats are supported via assimp:
- FBX
- GLTF / GLB
- OBJ
- DAE (Collada)
- Other formats supported by assimp

## Content Folder Assets

The plugin's `Content` folder includes the following assets:

- **`AssetImporterMeshMaterial.uasset`** — Parent material used by the plugin. Exposes the following parameters:
  - `TextureBlendIntensityForBaseColor` — blend intensity between texture and base color
  - `BaseColor4` — base color as a 4-component vector
  - `BaseColorTexture` — texture input for base color
- **`purple.uasset`** — Sample purple texture for testing and demonstration purposes.

## Known Bugs

- **Multiplayer (ProceduralMesh):** Clients may experience abnormal movement accompanied by a `LogNetPackageMap` warning. Use `DynamicMeshComponent` instead as a workaround.
- **Packaged game materials (StaticMesh — Experimental):** Materials display as a checkerboard pattern in packaged builds. Use `DynamicMeshComponent` instead as a workaround.

## Running Tests

To run from the command line:
```
UnrealEditor.exe <YourProject>.uproject -ExecCmds="Automation RunTests RuntimeAssetImport" -unattended -nopause -log -testexit="Automation Test Queue Empty"
```

Or from the UE Editor UI: **Window > Test Automation**, search `RuntimeAssetImport`, click **Start Tests**.

## Known Limitations
- Only embedded textures are loaded (external texture file references are not yet supported)
- Only 1 texture per material is supported
- Only 1 UV channel is supported
- Only 1 vertex color channel is supported

## Description of the technology inside
We are using assimp for loading 3D asset files. Prebuilt Win64 binaries (`assimp-vc143-mt.dll` / `.lib`) are bundled under `Source/ThirdParty/assimp/Bin/Win64` and `Lib/Win64`. This plugin converts the data loaded by assimp into a format usable by Unreal Engine.
