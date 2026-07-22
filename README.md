# Runtime Asset Import

## Overview

Runtime Asset Import is an Unreal Engine code plugin that imports static 3D mesh data at runtime on Windows (Win64). It accepts a file path or an in-memory byte array and constructs a hierarchy of Dynamic Mesh Components or Procedural Mesh Components. DynamicMesh is the recommended path.

## Supported Environment

- Unreal Engine 5.4, 5.5, 5.7, and 5.8, as verified in [MarketplaceListing.md](MarketplaceListing.md)
- Windows Win64 editor and packaged applications
- Static geometry only

Other desktop platforms, mobile platforms, and consoles are not supported by the bundled dependency.

## Features

- Synchronous runtime import from a local file or memory
- Blueprint and C++ APIs
- DynamicMeshComponent construction (recommended)
- ProceduralMeshComponent construction
- Hierarchical node transforms and multiple mesh sections
- Diffuse/base color and one embedded texture per material
- Collision generation on constructed components
- Assimp 6.0.5 built from the official source tag

The bundled Assimp build includes the FBX, OBJ, glTF/GLB, and Collada importers used by this product. Unrelated importers are disabled.

The plugin intentionally has no StaticMesh construction API, latent or asynchronous API, or URL downloader.

## Installation from Fab

1. Acquire Runtime Asset Import in Fab.
2. In the Epic Games Launcher, install the plugin for a supported Unreal Engine version.
3. Open the project and enable **Runtime Asset Import** under **Edit > Plugins** if it is not already enabled.
4. Restart the editor when prompted.

The packaged plugin already contains the required Win64 Assimp DLL. CMake is not required for normal use.

## Installation from Source

1. Place this repository at `<Project>/Plugins/RuntimeAssetImport`.
2. Generate project files or open the `.uproject` and accept the rebuild prompt.
3. Build the project for Win64.

For a Git checkout:

```powershell
git clone https://github.com/metyatech/RuntimeAssetImportPlugin.git Plugins/RuntimeAssetImport
```

## Blueprint Quick Start

1. Create a file path for a local FBX, OBJ, glTF/GLB, or DAE file.
2. Call **Construct Dynamic Mesh Component from Asset File**.
3. Pass an owning Actor and a Parent Material that provides the required parameters below.
4. Handle the `Success` and `Failure` execution outputs.
5. Use the returned root Dynamic Mesh Component. Child nodes are attached beneath it.

For memory input, call **Load Mesh from Asset Data**, then pass the returned mesh data to **Construct Dynamic Mesh Component from Mesh Data**.

## C++ Quick Start

```cpp
#include "AssetConstructor.h"

EConstructDynamicMeshComponentFromAssetFileResult Result =
    EConstructDynamicMeshComponentFromAssetFileResult::Failure;

UDynamicMeshComponent* RootComponent =
    UAssetConstructor::ConstructDynamicMeshComponentFromAssetFile(
        FilePath,
        ParentMaterial,
        OwnerActor,
        Result);

if (Result != EConstructDynamicMeshComponentFromAssetFileResult::Success ||
    RootComponent == nullptr)
{
    // Report or recover from the failed import.
}
```

To separate import from construction:

```cpp
#include "AssetLoader.h"

ELoadMeshFromAssetFileResult LoadResult = ELoadMeshFromAssetFileResult::Failure;
const FLoadedMeshData MeshData = UAssetLoader::LoadMeshFromAssetFile(FilePath, LoadResult);

if (LoadResult == ELoadMeshFromAssetFileResult::Success)
{
    UDynamicMeshComponent* RootComponent =
        UAssetConstructor::ConstructDynamicMeshComponentFromMeshData(
            MeshData, ParentMaterial, OwnerActor);
}
```

## Parent Material Requirements

Construction requires a Parent Material with these parameters:

| Parameter | Type | Purpose |
| --- | --- | --- |
| `TextureBlendIntensityForBaseColor` | Scalar | Selects the base-color texture contribution. |
| `BaseColor4` | Vector | Supplies the imported diffuse/base color. |
| `BaseColorTexture` | Texture2D | Supplies the imported embedded texture. |

The bundled `/RuntimeAssetImport/AssetImporterMeshMaterial` satisfies this contract. A Parent Material missing any required parameter is rejected without creating a partial component hierarchy.

## Tested File Formats

Automated tests cover both file and in-memory import for FBX, OBJ, glTF, GLB, and DAE.

Tested formats:

- FBX
- OBJ
- glTF
- GLB
- DAE (Collada)

## Blueprint API

The plugin exposes exactly six Blueprint-callable functions:

- `LoadMeshFromAssetFile`
- `LoadMeshFromAssetData`
- `ConstructDynamicMeshComponentFromMeshData`
- `ConstructDynamicMeshComponentFromAssetFile`
- `ConstructProceduralMeshComponentFromMeshData`
- `ConstructProceduralMeshComponentFromAssetFile`

## Synchronous Import Warning

All import and construction functions are synchronous. A large or complex file blocks the calling thread until parsing and construction finish. Schedule calls at an acceptable point in the application flow and validate input size before importing untrusted data. The plugin does not provide latent, asynchronous, or URL-loading APIs.

## Known Limitations

- Win64 only
- Static geometry only; no skeletal meshes, animations, morph targets, or LOD import
- External texture references are not loaded
- Only the first material texture, first UV channel, and first vertex-color channel are used
- Runtime-created meshes are local and are not automatically replicated
- ProceduralMeshComponent can cause movement or network issues when independently created by multiplayer peers; DynamicMeshComponent is recommended
- Input paths must refer to local files accessible to the application

## Running Tests

Install the plugin in an Unreal project, then run:

```powershell
UnrealEditor-Cmd.exe <Project>.uproject -ExecCmds="Automation RunTests RuntimeAssetImport; Quit" -unattended -nop4 -nosplash
```

The [RuntimeAssetImportSample](https://github.com/metyatech/RuntimeAssetImportSample) repository is the CI test host. It is not an end-user example project for the initial Fab release.

## Rebuilding the Bundled Assimp Dependency

Normal users do not need to rebuild Assimp. Maintainers can reproduce the bundled Visual Studio 2022 v143, Win64, Release build from official Assimp tag `v6.0.5` and commit `392a658f9c271be965271f45e7521a1b80ea4392`:

```powershell
pwsh -NoProfile -File .\BuildAssimpForPlugin.ps1
```

The script validates the source revision, headers, binary architecture, dependencies, exports, generated test assets, and hashes before replacing the repository artifacts. See [BUILD-INFO.json](Source/ThirdParty/assimp/BUILD-INFO.json).

## Creating a Fab Submission Package

Create and validate the UE 5.4 package with:

```powershell
pwsh -NoProfile -File .\PackageForFab.ps1 -EngineVersion 5.4
```

The output contains a single `RuntimeAssetImport` directory and excludes tests, internal files, debug symbols, and build artifacts. Use `-OutputDirectory <Path>` to place generated output outside the repository.

## Licensing

The current source is source-available under the conditions in [LICENSE](LICENSE). Copies acquired through Fab are governed by the applicable Fab Standard License and Fab End User License Agreement. Earlier commits or releases explicitly distributed under MIT remain under the MIT license recorded in [LICENSE-MIT-LEGACY](LICENSE-MIT-LEGACY).

## Third-Party Software

The bundled Assimp DLL and import library were built by the publisher from the official Assimp 6.0.5 source tag; they are not described as official distributed binaries. Assimp and its bundled components retain their respective licenses. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and `Source/ThirdParty/assimp/LICENSES`.

## Support

Report reproducible problems through [GitHub Issues](https://github.com/metyatech/RuntimeAssetImportPlugin/issues). Include the Unreal Engine version, input format, whether the failure occurs in editor or a packaged Win64 build, and a minimal reproduction when possible.
