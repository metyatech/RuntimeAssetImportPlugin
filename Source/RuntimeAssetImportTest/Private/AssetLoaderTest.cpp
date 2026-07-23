// Copyright (c) 2026 metyatech. All rights reserved.

#include "AssetLoader.h"
#include "CoreMinimal.h"
#include "HasFeatureFix.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Windows/WindowsHWrapper.h"

#include <assimp/version.h>
#include <bcrypt.h>

namespace
{
    constexpr const TCHAR *ExpectedRedPngSha256 =
        TEXT("49e1dad481e94dfab7c9573a9a81d56aa2ca629fe15a3f7a910aa4f47601c00d");

    FString GetTestAssetsDir()
    {
        return FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("RuntimeAssetImport"),
                               TEXT("Source/RuntimeAssetImportTest/TestAssets"));
    }

    bool AssertValidLoadedMesh(FAutomationTestBase &Test, const FString &Label, const FLoadedMeshData &MeshData)
    {
        bool HasSection = false;
        bool HasVertices = false;
        bool HasTriangles = false;
        bool HasValidTriangleCount = true;
        for (const FLoadedMeshNode &Node : MeshData.NodeList)
        {
            for (const FLoadedMeshSectionData &Section : Node.Sections)
            {
                HasSection = true;
                HasVertices |= !Section.Vertices.IsEmpty();
                HasTriangles |= !Section.Triangles.IsEmpty();
                HasValidTriangleCount &= Section.Triangles.Num() % 3 == 0;
            }
        }

        bool Passed = true;
        Passed &=
            Test.TestTrue(*FString::Printf(TEXT("%s should contain nodes"), *Label), !MeshData.NodeList.IsEmpty());
        Passed &= Test.TestTrue(*FString::Printf(TEXT("%s should contain a section"), *Label), HasSection);
        Passed &= Test.TestTrue(*FString::Printf(TEXT("%s should contain vertices"), *Label), HasVertices);
        Passed &= Test.TestTrue(*FString::Printf(TEXT("%s should contain triangles"), *Label), HasTriangles);
        Passed &= Test.TestTrue(*FString::Printf(TEXT("%s triangle counts should be divisible by three"), *Label),
                                HasValidTriangleCount);
        Passed &= Test.TestTrue(*FString::Printf(TEXT("%s should contain materials"), *Label),
                                !MeshData.MaterialList.IsEmpty());
        return Passed;
    }

    bool LoadFileAndAssertSuccess(FAutomationTestBase &Test, const TCHAR *FileName, const TCHAR *Label)
    {
        const FString TestAssetsDir = GetTestAssetsDir();
        if (TestAssetsDir.IsEmpty())
        {
            Test.AddError(TEXT("Could not find RuntimeAssetImport plugin"));
            return false;
        }

        ELoadMeshFromAssetFileResult Result = ELoadMeshFromAssetFileResult::Failure;
        const FLoadedMeshData MeshData =
            UAssetLoader::LoadMeshFromAssetFile(FPaths::Combine(TestAssetsDir, FileName), Result);
        bool Passed = Test.TestEqual(*FString::Printf(TEXT("%s should load successfully"), Label), Result,
                                     ELoadMeshFromAssetFileResult::Success);
        Passed &= AssertValidLoadedMesh(Test, Label, MeshData);
        return Passed;
    }

    bool LoadMemoryAndAssertSuccess(FAutomationTestBase &Test, const TCHAR *FileName, const TCHAR *Label)
    {
        const FString AssetPath = FPaths::Combine(GetTestAssetsDir(), FileName);
        TArray<uint8> FileBytes;
        if (!FFileHelper::LoadFileToArray(FileBytes, *AssetPath))
        {
            Test.AddError(FString::Printf(TEXT("Could not read test asset: %s"), *AssetPath));
            return false;
        }

        ELoadMeshFromAssetDataResult Result = ELoadMeshFromAssetDataResult::Failure;
        const FLoadedMeshData MeshData = UAssetLoader::LoadMeshFromAssetData(FileBytes, Result);
        bool Passed = Test.TestEqual(*FString::Printf(TEXT("%s should load successfully"), Label), Result,
                                     ELoadMeshFromAssetDataResult::Success);
        Passed &= AssertValidLoadedMesh(Test, Label, MeshData);
        return Passed;
    }

    FString GetSha256(const TArray<uint8> &Data)
    {
        if (Data.IsEmpty())
        {
            return {};
        }

        BCRYPT_ALG_HANDLE Algorithm = nullptr;
        BCRYPT_HASH_HANDLE HashHandle = nullptr;
        DWORD ObjectSize = 0;
        DWORD HashSize = 0;
        DWORD ResultSize = 0;
        if (BCryptOpenAlgorithmProvider(&Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
            BCryptGetProperty(Algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&ObjectSize),
                              sizeof(ObjectSize), &ResultSize, 0) < 0 ||
            BCryptGetProperty(Algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&HashSize), sizeof(HashSize),
                              &ResultSize, 0) < 0 ||
            HashSize != 32)
        {
            if (Algorithm != nullptr)
            {
                BCryptCloseAlgorithmProvider(Algorithm, 0);
            }
            return {};
        }

        TArray<uint8> HashObject;
        HashObject.SetNumUninitialized(static_cast<int32>(ObjectSize));
        TArray<uint8> HashBytes;
        HashBytes.SetNumUninitialized(static_cast<int32>(HashSize));
        const bool bSucceeded =
            BCryptCreateHash(Algorithm, &HashHandle, HashObject.GetData(), ObjectSize, nullptr, 0, 0) >= 0 &&
            BCryptHashData(HashHandle, const_cast<PUCHAR>(Data.GetData()), static_cast<ULONG>(Data.Num()), 0) >= 0 &&
            BCryptFinishHash(HashHandle, HashBytes.GetData(), HashSize, 0) >= 0;
        if (HashHandle != nullptr)
        {
            BCryptDestroyHash(HashHandle);
        }
        BCryptCloseAlgorithmProvider(Algorithm, 0);
        return bSucceeded ? BytesToHex(HashBytes.GetData(), HashBytes.Num()).ToLower() : FString();
    }

    const FLoadedMaterialData *FindFirstUsedMaterial(const FLoadedMeshData &MeshData)
    {
        for (const FLoadedMeshNode &Node : MeshData.NodeList)
        {
            for (const FLoadedMeshSectionData &Section : Node.Sections)
            {
                if (MeshData.MaterialList.IsValidIndex(Section.MaterialIndex))
                {
                    return &MeshData.MaterialList[Section.MaterialIndex];
                }
            }
        }
        return nullptr;
    }

    bool AssertRedTextureMaterial(FAutomationTestBase &Test, const FString &Label, const FLoadedMeshData &MeshData)
    {
        const FLoadedMaterialData *Material = FindFirstUsedMaterial(MeshData);
        bool Passed = Test.TestNotNull(*FString::Printf(TEXT("%s should use a material"), *Label), Material);
        if (Material == nullptr)
        {
            return false;
        }

        Passed &= Test.TestEqual(*FString::Printf(TEXT("%s should use a texture"), *Label), Material->ColorStatus,
                                 EColorStatus::TextureIsSet);
        Passed &= Test.TestFalse(*FString::Printf(TEXT("%s texture bytes should be non-empty"), *Label),
                                 Material->CompressedTextureData.IsEmpty());
        Passed &= Test.TestEqual(*FString::Printf(TEXT("%s texture hash should match the red PNG"), *Label),
                                 GetSha256(Material->CompressedTextureData), FString(ExpectedRedPngSha256));
        return Passed;
    }

    FLoadedMeshData LoadTestAssetFile(FAutomationTestBase &Test, const TCHAR *FileName, const FString &Label,
                                      ELoadMeshFromAssetFileResult &OutResult)
    {
        const FString Path = FPaths::Combine(GetTestAssetsDir(), FileName);
        Test.TestTrue(*FString::Printf(TEXT("%s should exist: %s"), *Label, *Path), FPaths::FileExists(Path));
        return UAssetLoader::LoadMeshFromAssetFile(Path, OutResult);
    }

    bool LoadTestAssetBytes(FAutomationTestBase &Test, const TCHAR *FileName, TArray<uint8> &OutBytes)
    {
        const FString Path = FPaths::Combine(GetTestAssetsDir(), FileName);
        if (!FFileHelper::LoadFileToArray(OutBytes, *Path))
        {
            Test.AddError(FString::Printf(TEXT("Could not read test asset: %s"), *Path));
            return false;
        }
        return true;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssimpVersionIs605, "RuntimeAssetImport.Assimp.Version.Is605",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssimpVersionIs605::RunTest(const FString &Parameters)
{
    TestEqual(TEXT("Assimp major version"), aiGetVersionMajor(), 6U);
    TestEqual(TEXT("Assimp minor version"), aiGetVersionMinor(), 0U);
    TestEqual(TEXT("Assimp patch version"), aiGetVersionPatch(), 5U);
    TestTrue(TEXT("Assimp should be built as a shared library"),
             (aiGetCompileFlags() & ASSIMP_CFLAGS_SHARED) == ASSIMP_CFLAGS_SHARED);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoadMeshFromAssetFile_NonExistentPath_ReturnsFailure,
                                 "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetFile.NonExistentPath",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetFile_NonExistentPath_ReturnsFailure::RunTest(const FString &Parameters)
{
    AddExpectedError(TEXT("LogAssetLoader:"), EAutomationExpectedErrorFlags::Contains, 1);
    ELoadMeshFromAssetFileResult Result = ELoadMeshFromAssetFileResult::Success;
    const FLoadedMeshData MeshData =
        UAssetLoader::LoadMeshFromAssetFile(TEXT("C:/nonexistent/path/does_not_exist.fbx"), Result);
    TestEqual(TEXT("Loading a non-existent file should return Failure without crashing"), Result,
              ELoadMeshFromAssetFileResult::Failure);
    TestTrue(TEXT("A failed file load should return empty mesh data"), MeshData.NodeList.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoadMeshFromAssetFile_EmptyPath_ReturnsFailure,
                                 "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetFile.EmptyPath",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetFile_EmptyPath_ReturnsFailure::RunTest(const FString &Parameters)
{
    AddExpectedError(TEXT("LogAssetLoader:"), EAutomationExpectedErrorFlags::Contains, 1);
    ELoadMeshFromAssetFileResult Result = ELoadMeshFromAssetFileResult::Success;
    const FLoadedMeshData MeshData = UAssetLoader::LoadMeshFromAssetFile(TEXT(""), Result);
    TestEqual(TEXT("Loading with an empty path should return Failure without crashing"), Result,
              ELoadMeshFromAssetFileResult::Failure);
    TestTrue(TEXT("An empty path should return empty mesh data"), MeshData.NodeList.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoadMeshFromAssetData_EmptyArray_ReturnsFailure,
                                 "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetData.EmptyArray",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetData_EmptyArray_ReturnsFailure::RunTest(const FString &Parameters)
{
    AddExpectedError(TEXT("LogAssetLoader:"), EAutomationExpectedErrorFlags::Contains, 1);
    ELoadMeshFromAssetDataResult Result = ELoadMeshFromAssetDataResult::Success;
    const FLoadedMeshData MeshData = UAssetLoader::LoadMeshFromAssetData({}, Result);
    TestEqual(TEXT("Loading an empty byte array should return Failure without crashing"), Result,
              ELoadMeshFromAssetDataResult::Failure);
    TestTrue(TEXT("Empty bytes should return empty mesh data"), MeshData.NodeList.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoadMeshFromAssetData_Malformed_ReturnsFailure,
                                 "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetData.Malformed",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetData_Malformed_ReturnsFailure::RunTest(const FString &Parameters)
{
    AddExpectedError(TEXT("LogAssetLoader:"), EAutomationExpectedErrorFlags::Contains, 1);
    const TArray<uint8> MalformedData = {0x00, 0x01, 0x02, 0x03, 0xFF, 0xFE, 0xFD};
    ELoadMeshFromAssetDataResult Result = ELoadMeshFromAssetDataResult::Success;
    const FLoadedMeshData MeshData = UAssetLoader::LoadMeshFromAssetData(MalformedData, Result);
    TestEqual(TEXT("Malformed bytes should return Failure"), Result, ELoadMeshFromAssetDataResult::Failure);
    TestTrue(TEXT("Malformed bytes should return empty mesh data"), MeshData.NodeList.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoadMeshFromAssetData_TruncatedGlb_ReturnsFailure,
                                 "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetData.TruncatedGlb",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetData_TruncatedGlb_ReturnsFailure::RunTest(const FString &Parameters)
{
    AddExpectedError(TEXT("LogAssetLoader:"), EAutomationExpectedErrorFlags::Contains, 1);
    TArray<uint8> GlbData;
    const FString GlbPath = FPaths::Combine(GetTestAssetsDir(), TEXT("test_triangle.glb"));
    if (!FFileHelper::LoadFileToArray(GlbData, *GlbPath))
    {
        AddError(FString::Printf(TEXT("Could not read test asset: %s"), *GlbPath));
        return false;
    }
    GlbData.SetNum(FMath::Min(16, GlbData.Num()));

    ELoadMeshFromAssetDataResult Result = ELoadMeshFromAssetDataResult::Success;
    const FLoadedMeshData MeshData = UAssetLoader::LoadMeshFromAssetData(GlbData, Result);
    TestEqual(TEXT("Truncated GLB bytes should return Failure"), Result, ELoadMeshFromAssetDataResult::Failure);
    TestTrue(TEXT("Truncated GLB bytes should return empty mesh data"), MeshData.NodeList.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoadMeshFromAssetFile_ObjTriangle_ReturnsSuccess,
                                 "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetFile.ObjTriangle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetFile_ObjTriangle_ReturnsSuccess::RunTest(const FString &Parameters)
{
    return LoadFileAndAssertSuccess(*this, TEXT("test_triangle.obj"), TEXT("OBJ triangle"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoadMeshFromAssetFile_ObjCube_ReturnsSuccess,
                                 "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetFile.ObjCube",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetFile_ObjCube_ReturnsSuccess::RunTest(const FString &Parameters)
{
    return LoadFileAndAssertSuccess(*this, TEXT("test_cube.obj"), TEXT("OBJ cube"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoadMeshFromAssetFile_FbxTriangle_ReturnsSuccess,
                                 "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetFile.FbxTriangle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetFile_FbxTriangle_ReturnsSuccess::RunTest(const FString &Parameters)
{
    return LoadFileAndAssertSuccess(*this, TEXT("test_triangle.fbx"), TEXT("FBX triangle"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoadMeshFromAssetFile_DaeTriangle_ReturnsSuccess,
                                 "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetFile.DaeTriangle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetFile_DaeTriangle_ReturnsSuccess::RunTest(const FString &Parameters)
{
    return LoadFileAndAssertSuccess(*this, TEXT("test_triangle.dae"), TEXT("DAE triangle"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoadMeshFromAssetFile_GltfScene_ReturnsSuccess,
                                 "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetFile.GltfScene",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetFile_GltfScene_ReturnsSuccess::RunTest(const FString &Parameters)
{
    return LoadFileAndAssertSuccess(*this, TEXT("test_scene.gltf"), TEXT("glTF scene"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoadMeshFromAssetFile_GlbTriangle_ReturnsSuccess,
                                 "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetFile.GlbTriangle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetFile_GlbTriangle_ReturnsSuccess::RunTest(const FString &Parameters)
{
    return LoadFileAndAssertSuccess(*this, TEXT("test_triangle.glb"), TEXT("GLB triangle"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoadMeshFromAssetData_ObjTriangle_ReturnsSuccess,
                                 "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetData.ObjTriangle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetData_ObjTriangle_ReturnsSuccess::RunTest(const FString &Parameters)
{
    AddExpectedError(TEXT("Memory import denied external filesystem"), EAutomationExpectedErrorFlags::Contains, 1);
    return LoadMemoryAndAssertSuccess(*this, TEXT("test_triangle.obj"), TEXT("OBJ bytes"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoadMeshFromAssetData_FbxTriangle_ReturnsSuccess,
                                 "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetData.FbxTriangle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetData_FbxTriangle_ReturnsSuccess::RunTest(const FString &Parameters)
{
    return LoadMemoryAndAssertSuccess(*this, TEXT("test_triangle.fbx"), TEXT("FBX bytes"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoadMeshFromAssetData_DaeTriangle_ReturnsSuccess,
                                 "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetData.DaeTriangle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetData_DaeTriangle_ReturnsSuccess::RunTest(const FString &Parameters)
{
    return LoadMemoryAndAssertSuccess(*this, TEXT("test_triangle.dae"), TEXT("DAE bytes"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoadMeshFromAssetData_GltfScene_ReturnsSuccess,
                                 "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetData.GltfScene",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetData_GltfScene_ReturnsSuccess::RunTest(const FString &Parameters)
{
    return LoadMemoryAndAssertSuccess(*this, TEXT("test_scene.gltf"), TEXT("glTF bytes"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoadMeshFromAssetData_GlbTriangle_ReturnsSuccess,
                                 "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetData.GlbTriangle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetData_GlbTriangle_ReturnsSuccess::RunTest(const FString &Parameters)
{
    return LoadMemoryAndAssertSuccess(*this, TEXT("test_triangle.glb"), TEXT("GLB bytes"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetLoaderColorMaterialValues,
                                 "RuntimeAssetImport.AssetLoader.Material.ColorMaterialValues",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetLoaderColorMaterialValues::RunTest(const FString &Parameters)
{
    ELoadMeshFromAssetFileResult Result = ELoadMeshFromAssetFileResult::Failure;
    const FLoadedMeshData MeshData = LoadTestAssetFile(*this, TEXT("test_triangle.obj"), TEXT("Color OBJ"), Result);
    bool Passed = TestEqual(TEXT("Color OBJ should load successfully"), Result, ELoadMeshFromAssetFileResult::Success);
    const FLoadedMaterialData *Material = FindFirstUsedMaterial(MeshData);
    Passed &= TestNotNull(TEXT("Color OBJ should use a material"), Material);
    if (Material != nullptr)
    {
        Passed &= TestEqual(TEXT("Color OBJ material status"), Material->ColorStatus, EColorStatus::ColorIsSet);
        Passed &= TestTrue(TEXT("Color OBJ red value"), FMath::IsNearlyEqual(Material->Color.R, 0.8f));
        Passed &= TestTrue(TEXT("Color OBJ green value"), FMath::IsNearlyEqual(Material->Color.G, 0.4f));
        Passed &= TestTrue(TEXT("Color OBJ blue value"), FMath::IsNearlyEqual(Material->Color.B, 0.2f));
        Passed &= TestTrue(TEXT("Color OBJ alpha value"), FMath::IsNearlyEqual(Material->Color.A, 1.0f));
    }
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetLoaderExternalTextureFileImport,
                                 "RuntimeAssetImport.AssetLoader.Material.ExternalTextureFileImport",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetLoaderExternalTextureFileImport::RunTest(const FString &Parameters)
{
    ELoadMeshFromAssetFileResult Result = ELoadMeshFromAssetFileResult::Failure;
    const FLoadedMeshData MeshData =
        LoadTestAssetFile(*this, TEXT("test_external_texture.obj"), TEXT("External texture OBJ"), Result);
    bool Passed =
        TestEqual(TEXT("External texture OBJ should load successfully"), Result, ELoadMeshFromAssetFileResult::Success);
    Passed &= AssertValidLoadedMesh(*this, TEXT("External texture OBJ"), MeshData);
    Passed &= AssertRedTextureMaterial(*this, TEXT("External texture OBJ"), MeshData);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetLoaderEmbeddedTextureFileImport,
                                 "RuntimeAssetImport.AssetLoader.Material.EmbeddedTextureFileImport",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetLoaderEmbeddedTextureFileImport::RunTest(const FString &Parameters)
{
    ELoadMeshFromAssetFileResult Result = ELoadMeshFromAssetFileResult::Failure;
    const FLoadedMeshData MeshData =
        LoadTestAssetFile(*this, TEXT("test_embedded_texture.gltf"), TEXT("Embedded texture glTF"), Result);
    bool Passed = TestEqual(TEXT("Embedded texture glTF should load successfully"), Result,
                            ELoadMeshFromAssetFileResult::Success);
    Passed &= AssertValidLoadedMesh(*this, TEXT("Embedded texture glTF"), MeshData);
    Passed &= AssertRedTextureMaterial(*this, TEXT("Embedded texture glTF"), MeshData);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetLoaderEmbeddedTextureMemoryImport,
                                 "RuntimeAssetImport.AssetLoader.Material.EmbeddedTextureMemoryImport",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetLoaderEmbeddedTextureMemoryImport::RunTest(const FString &Parameters)
{
    TArray<uint8> Bytes;
    if (!LoadTestAssetBytes(*this, TEXT("test_embedded_texture.gltf"), Bytes))
    {
        return false;
    }

    ELoadMeshFromAssetDataResult Result = ELoadMeshFromAssetDataResult::Failure;
    const FLoadedMeshData MeshData = UAssetLoader::LoadMeshFromAssetData(Bytes, Result);
    bool Passed = TestEqual(TEXT("Embedded texture glTF bytes should load successfully"), Result,
                            ELoadMeshFromAssetDataResult::Success);
    Passed &= AssertValidLoadedMesh(*this, TEXT("Embedded texture glTF bytes"), MeshData);
    Passed &= AssertRedTextureMaterial(*this, TEXT("Embedded texture glTF bytes"), MeshData);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAssetLoaderExternalTextureMemoryImportDoesNotReadFilesystem,
    "RuntimeAssetImport.AssetLoader.Material.ExternalTextureMemoryImportDoesNotReadFilesystem",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetLoaderExternalTextureMemoryImportDoesNotReadFilesystem::RunTest(const FString &Parameters)
{
    TArray<uint8> Bytes;
    if (!LoadTestAssetBytes(*this, TEXT("test_external_texture.obj"), Bytes))
    {
        return false;
    }

    ELoadMeshFromAssetDataResult Result = ELoadMeshFromAssetDataResult::Failure;
    AddExpectedError(TEXT("Memory import denied external filesystem"), EAutomationExpectedErrorFlags::Contains, 1);
    const FLoadedMeshData MeshData = UAssetLoader::LoadMeshFromAssetData(Bytes, Result);
    bool Passed =
        TestEqual(TEXT("External texture OBJ bytes may load geometry"), Result, ELoadMeshFromAssetDataResult::Success);
    Passed &= AssertValidLoadedMesh(*this, TEXT("External texture OBJ bytes"), MeshData);
    for (const FLoadedMaterialData &Material : MeshData.MaterialList)
    {
        Passed &= TestTrue(TEXT("Memory OBJ must not contain external texture bytes"),
                           Material.CompressedTextureData.IsEmpty());
        Passed &= TestNotEqual(TEXT("Memory OBJ must not report TextureIsSet"), Material.ColorStatus,
                               EColorStatus::TextureIsSet);
    }
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetLoaderExternalBufferFileImport,
                                 "RuntimeAssetImport.AssetLoader.Auxiliary.ExternalBufferFileImport",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetLoaderExternalBufferFileImport::RunTest(const FString &Parameters)
{
    return LoadFileAndAssertSuccess(*this, TEXT("test_external_buffer.gltf"), TEXT("External buffer glTF"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetLoaderExternalBufferMemoryImportFails,
                                 "RuntimeAssetImport.AssetLoader.Auxiliary.ExternalBufferMemoryImportFails",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetLoaderExternalBufferMemoryImportFails::RunTest(const FString &Parameters)
{
    TArray<uint8> Bytes;
    if (!LoadTestAssetBytes(*this, TEXT("test_external_buffer.gltf"), Bytes))
    {
        return false;
    }

    AddExpectedError(TEXT("Memory import denied external filesystem"), EAutomationExpectedErrorFlags::Contains, 2);
    ELoadMeshFromAssetDataResult Result = ELoadMeshFromAssetDataResult::Success;
    const FLoadedMeshData MeshData = UAssetLoader::LoadMeshFromAssetData(Bytes, Result);
    bool Passed =
        TestEqual(TEXT("External buffer glTF bytes should fail"), Result, ELoadMeshFromAssetDataResult::Failure);
    Passed &= TestTrue(TEXT("External buffer glTF bytes should return empty mesh data"), MeshData.NodeList.IsEmpty());
    return Passed;
}
