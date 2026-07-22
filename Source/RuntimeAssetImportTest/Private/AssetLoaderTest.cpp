// Copyright (c) 2026 metyatech. All rights reserved.

#include "AssetLoader.h"
#include "CoreMinimal.h"
#include "HasFeatureFix.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"

#include <assimp/version.h>

namespace
{
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
    const FString ObjPath = FPaths::Combine(GetTestAssetsDir(), TEXT("test_triangle.obj"));
    TArray<uint8> FileBytes;
    if (!FFileHelper::LoadFileToArray(FileBytes, *ObjPath))
    {
        AddError(FString::Printf(TEXT("Could not read test asset: %s"), *ObjPath));
        return false;
    }

    ELoadMeshFromAssetDataResult Result = ELoadMeshFromAssetDataResult::Failure;
    const FLoadedMeshData MeshData = UAssetLoader::LoadMeshFromAssetData(FileBytes, Result);
    bool Passed = TestEqual(TEXT("OBJ bytes should load successfully"), Result, ELoadMeshFromAssetDataResult::Success);
    Passed &= AssertValidLoadedMesh(*this, TEXT("OBJ bytes"), MeshData);
    return Passed;
}
