#include "AssetLoader.h"
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

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
