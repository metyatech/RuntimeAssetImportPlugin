#include "AssetLoader.h"
#include "CoreMinimal.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"

// ---------------------------------------------------------------------------
// Failure-path tests
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoadMeshFromAssetFile_NonExistentPath_ReturnsFailure,
    "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetFile.NonExistentPath",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetFile_NonExistentPath_ReturnsFailure::RunTest(const FString& Parameters)
{
    ELoadMeshFromAssetFileResult Result;
    FLoadedMeshData MeshData = UAssetLoader::LoadMeshFromAssetFile(
        TEXT("C:/nonexistent/path/does_not_exist.fbx"), Result);

    TestEqual(
        TEXT("Loading a non-existent file should return Failure without crashing"),
        Result, ELoadMeshFromAssetFileResult::Failure);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoadMeshFromAssetFile_EmptyPath_ReturnsFailure,
    "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetFile.EmptyPath",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetFile_EmptyPath_ReturnsFailure::RunTest(const FString& Parameters)
{
    ELoadMeshFromAssetFileResult Result;
    FLoadedMeshData MeshData = UAssetLoader::LoadMeshFromAssetFile(TEXT(""), Result);

    TestEqual(
        TEXT("Loading with an empty path should return Failure without crashing"),
        Result, ELoadMeshFromAssetFileResult::Failure);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoadMeshFromAssetData_EmptyArray_ReturnsFailure,
    "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetData.EmptyArray",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetData_EmptyArray_ReturnsFailure::RunTest(const FString& Parameters)
{
    ELoadMeshFromAssetDataResult Result;
    TArray<uint8> EmptyData;
    FLoadedMeshData MeshData = UAssetLoader::LoadMeshFromAssetData(EmptyData, Result);

    TestEqual(
        TEXT("Loading from an empty byte array should return Failure without crashing"),
        Result, ELoadMeshFromAssetDataResult::Failure);
    return true;
}

// ---------------------------------------------------------------------------
// Positive-path tests
// ---------------------------------------------------------------------------

namespace
{
    FString GetTestAssetsDir()
    {
        TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("RuntimeAssetImport"));
        if (!Plugin.IsValid())
        {
            return FString();
        }
        return FPaths::Combine(Plugin->GetBaseDir(),
                               TEXT("Source/RuntimeAssetImportTest/TestAssets"));
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoadMeshFromAssetFile_ObjTriangle_ReturnsSuccess,
    "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetFile.ObjTriangle",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetFile_ObjTriangle_ReturnsSuccess::RunTest(const FString& Parameters)
{
    const FString TestAssetsDir = GetTestAssetsDir();
    if (TestAssetsDir.IsEmpty())
    {
        AddError(TEXT("Could not find RuntimeAssetImport plugin"));
        return false;
    }
    const FString ObjPath = FPaths::Combine(TestAssetsDir, TEXT("test_triangle.obj"));

    ELoadMeshFromAssetFileResult Result;
    FLoadedMeshData MeshData = UAssetLoader::LoadMeshFromAssetFile(ObjPath, Result);

    TestEqual(
        TEXT("Loading a valid OBJ triangle should return Success"),
        Result, ELoadMeshFromAssetFileResult::Success);
    TestTrue(
        TEXT("Loaded OBJ triangle should have at least one mesh node"),
        MeshData.NodeList.Num() > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoadMeshFromAssetFile_ObjCube_ReturnsSuccess,
    "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetFile.ObjCube",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetFile_ObjCube_ReturnsSuccess::RunTest(const FString& Parameters)
{
    const FString TestAssetsDir = GetTestAssetsDir();
    if (TestAssetsDir.IsEmpty())
    {
        AddError(TEXT("Could not find RuntimeAssetImport plugin"));
        return false;
    }
    const FString ObjPath = FPaths::Combine(TestAssetsDir, TEXT("test_cube.obj"));

    ELoadMeshFromAssetFileResult Result;
    FLoadedMeshData MeshData = UAssetLoader::LoadMeshFromAssetFile(ObjPath, Result);

    TestEqual(
        TEXT("Loading a valid OBJ cube should return Success"),
        Result, ELoadMeshFromAssetFileResult::Success);
    TestTrue(
        TEXT("Loaded OBJ cube should have at least one mesh node"),
        MeshData.NodeList.Num() > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoadMeshFromAssetFile_GltfScene_ReturnsSuccess,
    "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetFile.GltfScene",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetFile_GltfScene_ReturnsSuccess::RunTest(const FString& Parameters)
{
    const FString TestAssetsDir = GetTestAssetsDir();
    if (TestAssetsDir.IsEmpty())
    {
        AddError(TEXT("Could not find RuntimeAssetImport plugin"));
        return false;
    }
    const FString GltfPath = FPaths::Combine(TestAssetsDir, TEXT("test_scene.gltf"));

    ELoadMeshFromAssetFileResult Result;
    FLoadedMeshData MeshData = UAssetLoader::LoadMeshFromAssetFile(GltfPath, Result);

    TestEqual(
        TEXT("Loading a valid glTF scene should return Success"),
        Result, ELoadMeshFromAssetFileResult::Success);
    TestTrue(
        TEXT("Loaded glTF scene should have at least one mesh node"),
        MeshData.NodeList.Num() > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoadMeshFromAssetData_ObjTriangle_ReturnsSuccess,
    "RuntimeAssetImport.AssetLoader.LoadMeshFromAssetData.ObjTriangle",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLoadMeshFromAssetData_ObjTriangle_ReturnsSuccess::RunTest(const FString& Parameters)
{
    const FString TestAssetsDir = GetTestAssetsDir();
    if (TestAssetsDir.IsEmpty())
    {
        AddError(TEXT("Could not find RuntimeAssetImport plugin"));
        return false;
    }
    const FString ObjPath = FPaths::Combine(TestAssetsDir, TEXT("test_triangle.obj"));

    TArray<uint8> FileBytes;
    if (!FFileHelper::LoadFileToArray(FileBytes, *ObjPath))
    {
        AddError(FString::Printf(TEXT("Could not read test asset: %s"), *ObjPath));
        return false;
    }

    ELoadMeshFromAssetDataResult Result;
    FLoadedMeshData MeshData = UAssetLoader::LoadMeshFromAssetData(FileBytes, Result);

    TestEqual(
        TEXT("Loading OBJ triangle bytes should return Success"),
        Result, ELoadMeshFromAssetDataResult::Success);
    TestTrue(
        TEXT("Loaded OBJ triangle from memory should have at least one mesh node"),
        MeshData.NodeList.Num() > 0);
    return true;
}
