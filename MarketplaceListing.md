# RuntimeAssetImport Plugin - Marketplace Listing Info

Use this information to fill out the product details on the Fab / Unreal Engine Marketplace publisher portal.

## Basic Info

**Product Title:**
Runtime Asset Import

**Category:**
Tools and Plugins > Code Plugins

**Short Description (max 255 chars):**
Import 3D meshes (FBX, OBJ, glTF, etc.) at runtime in Unreal Engine 5. Powered by Assimp (prebuilt binaries included). Supports both ProceduralMesh and DynamicMesh.

**Price:**
$29.99 (Suggested)

**Version:**
1.0

## Long Description

**RuntimeAssetImport** allows you to load 3D model files from disk or URL at runtime in your packaged games and applications. It leverages the powerful **Open Asset Import Library (Assimp)** to support a wide range of formats including FBX, OBJ, glTF/GLB, DAE, and more.

### Key Features:
*   **Runtime Import:** Load 3D assets dynamically without cooking.
*   **Wide Format Support:** Supports over 40+ formats via Assimp (FBX, OBJ, GLB, etc.).
*   **Prebuilt Binaries:** Includes pre-compiled Assimp DLLs for Win64. **No CMake or external setup required.** Just enable the plugin and go.
*   **Blueprint & C++:** Full support for both Blueprint visual scripting and C++ integration.
*   **Async Loading:** Imports assets asynchronously to prevent game thread hitches.
*   **Materials:** Automatically creates basic materials and imports textures.

### Important Notes:
*   **3rd Party Dependency:** This product includes the **Open Asset Import Library (Assimp)** which is used under the BSD-3-Clause license.
*   **Platform Support:** Currently supports **Windows (Win64)** only due to prebuilt library dependencies.

### Usage:
This plugin provides two main methods for importing meshes:
1.  **DynamicMesh (Recommended):** Uses the modern Geometry Scripting framework. Stable and performant.
2.  *ProceduralMesh (Experimental):* Uses the legacy ProceduralMeshComponent. (Note: Known issues with materials in packaged builds).

### Included Modules:
*   **RuntimeAssetImport (Runtime):** Core loading logic and Blueprint nodes.
*   **assimp (External):** Prebuilt ThirdParty library (Win64).

## Technical Details

**Features:**
*   Import Mesh from File (Async/Sync)
*   Import Mesh from URL
*   Procedural Mesh generation
*   Dynamic Mesh generation

**Code Modules:**
*   RuntimeAssetImport (Runtime)

**Number of Blueprints:**
0

**Number of C++ Classes:**
10+

**Network Replicated:**
No (The loaded mesh is local to the client, but the component can be replicated if the actor is set to replicate)

**Supported Development Platforms:**
Windows (Win64)

**Supported Target Build Platforms:**
Windows (Win64)

**Documentation:**
[GitHub README](https://github.com/metyatech/RuntimeAssetImportPlugin/blob/main/README.md)

**Example Project:**
[RuntimeAssetImportSample](https://github.com/metyatech/RuntimeAssetImportSample)

**Support Email:**
fab-support@metyatech.com

## Search Tags
import, fbx, obj, gltf, runtime, assimp, procedural, mesh, loading

