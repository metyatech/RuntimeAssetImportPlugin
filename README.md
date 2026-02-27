# RuntimeAssetImportPlugin
## Overview
This is a plugin for the Unreal Engine to load 3D assets (e.g. FBX) at runtime. Made with UE5.4.2.

## Prerequisites
- Windows is the primary supported platform. macOS (arm64 only; x86_64 not supported) and Linux support is experimental.
- Download and install [CMake](https://cmake.org/)
  The check box "Add CMake to the PATH environment variable" that appears during installation must be checked.

> **Note:** The first build of the assimp library (a C++ dependency) takes approximately 5 or more minutes. Subsequent builds are fast. This is expected behavior.

## How to install
Clone this repository with its submodules into the Plugins folder (or create your own if you don't have one) in the folder of the Unreal Engine project where you want to install this plugin by doing one of the following:
<details>
<summary>Installation method 1 (using Github Desktop) (easy)</summary>

1. Launch "Github Desktop" application (if not available, install it first).
2. From the menu, select File > Clone repository...
3. Go to the URL tab.
4. Enter the URL of this Github repository in "URL or username/repository", select the Plugins folder of the project where you want to install this plugin in "Local path", and press Clone.
</details>

<details>
<summary>Installation method 2 (using git command) (advanced)</summary>

1. If git is not installed, please install it.
2. In the Plugins folder of the project where you want to install this plug-in, start a command prompt.
3. Execute  
   ```
   git clone --recursive URL
   ```
   Put the URL of this repository in the URL field.
</details>

After installing the plugin using one of the above procedures, open the project (Press Yes when you see "The following modules are missing or built with a different engine version: RuntimeAssetImport") and the plugin is enabled.

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
- **Packaged game materials (StaticMesh):** Materials display as a checkerboard pattern in packaged builds. Use `DynamicMeshComponent` instead as a workaround.

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
We are using assimp as a git submodule, CMake is only needed to build assimp. The actual loading of the asset files is done by assimp, and this plugin only converts them from the format loaded by assimp to a format usable by the Unreal Engine. The build of assimp is done automatically during the project build process.

