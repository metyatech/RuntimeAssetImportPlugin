// Copyright (c) 2026 metyatech. All rights reserved.

#include "AssetConstructor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "MaterialDomain.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "UDynamicMesh.h"

namespace
{
    UMaterialInterface *LoadPluginParentMaterial()
    {
        return LoadObject<UMaterialInterface>(
            nullptr, TEXT("/RuntimeAssetImport/AssetImporterMeshMaterial.AssetImporterMeshMaterial"));
    }

    FLoadedMeshData MakeValidTriangleMeshData()
    {
        FLoadedMeshData MeshData;

        FLoadedMaterialData Material;
        Material.ColorStatus = EColorStatus::ColorIsSet;
        Material.Color = FLinearColor::White;
        MeshData.MaterialList.Add(Material);

        FLoadedMeshNode Node;
        Node.Name = TEXT("Root");
        Node.ParentNodeIndex = INDEX_NONE;
        Node.RelativeTransform = FTransform::Identity;

        FLoadedMeshSectionData Section;
        Section.Vertices = {FVector(0.0, 0.0, 0.0), FVector(100.0, 0.0, 0.0), FVector(0.0, 100.0, 0.0)};
        Section.Triangles = {0, 1, 2};
        Section.Normals = {FVector::UpVector, FVector::UpVector, FVector::UpVector};
        Section.UV0Channel = {FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), FVector2D(0.0, 1.0)};
        Section.VertexColors0 = {FLinearColor::White, FLinearColor::White, FLinearColor::White};
        Section.Tangents = {FProcMeshTangent(FVector::ForwardVector, false),
                            FProcMeshTangent(FVector::ForwardVector, false),
                            FProcMeshTangent(FVector::ForwardVector, false)};
        Section.MaterialIndex = 0;
        Node.Sections.Add(Section);
        MeshData.NodeList.Add(Node);
        return MeshData;
    }

    FString ResolveTestAssetPath(const TCHAR *FileName)
    {
        FString TestAssetPath = FPaths::ProjectPluginsDir();
        TestAssetPath.Append(TEXT("RuntimeAssetImport/Source/RuntimeAssetImportTest/TestAssets/"));
        TestAssetPath.Append(FileName);
        return TestAssetPath;
    }

    void DestroyTestActor(UWorld *World, AActor *Actor)
    {
        if (World != nullptr && IsValid(Actor))
        {
            World->DestroyActor(Actor);
        }
    }

    bool TestBothConstructorsReject(FAutomationTestBase &Test, const FString &Label, const FLoadedMeshData &MeshData,
                                    UMaterialInterface *ParentMaterial, AActor *Owner)
    {
        Test.AddExpectedError(TEXT("LogAssetConstructor:"), EAutomationExpectedErrorFlags::Contains, 2);
        bool Passed = Test.TestNull(
            *FString::Printf(TEXT("%s should be rejected by ProceduralMesh construction"), *Label),
            UAssetConstructor::ConstructProceduralMeshComponentFromMeshData(MeshData, ParentMaterial, Owner));
        Passed &= Test.TestNull(
            *FString::Printf(TEXT("%s should be rejected by DynamicMesh construction"), *Label),
            UAssetConstructor::ConstructDynamicMeshComponentFromMeshData(MeshData, ParentMaterial, Owner));
        return Passed;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetConstructorInvalidArgumentsReturnFailure,
                                 "RuntimeAssetImport.AssetConstructor.InvalidArgumentsReturnFailure",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetConstructorInvalidArgumentsReturnFailure::RunTest(const FString &Parameters)
{
    UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor *Owner = World != nullptr ? World->SpawnActor<AActor>() : nullptr;
    UMaterialInterface *ParentMaterial = LoadPluginParentMaterial();
    if (World == nullptr || Owner == nullptr || ParentMaterial == nullptr)
    {
        AddError(TEXT("Could not create the test world, actor, or plugin Parent Material."));
        DestroyTestActor(World, Owner);
        return false;
    }

    const FLoadedMeshData ValidMeshData = MakeValidTriangleMeshData();
    bool Passed = true;
    Passed &= TestBothConstructorsReject(*this, TEXT("Empty MeshData"), FLoadedMeshData(), ParentMaterial, Owner);
    Passed &= TestBothConstructorsReject(*this, TEXT("Null Owner"), ValidMeshData, ParentMaterial, nullptr);
    Passed &= TestBothConstructorsReject(*this, TEXT("Null Parent Material"), ValidMeshData, nullptr, Owner);

    EConstructProceduralMeshComponentFromAssetFileResult ProceduralResult =
        EConstructProceduralMeshComponentFromAssetFileResult::Success;
    AddExpectedError(TEXT("LogAssetConstructor:"), EAutomationExpectedErrorFlags::Contains, 1);
    Passed &= TestNull(TEXT("Procedural FromAssetFile should reject an empty path"),
                       UAssetConstructor::ConstructProceduralMeshComponentFromAssetFile(TEXT(""), ParentMaterial, Owner,
                                                                                        ProceduralResult));
    Passed &= TestEqual(TEXT("Procedural FromAssetFile should set Failure"), ProceduralResult,
                        EConstructProceduralMeshComponentFromAssetFileResult::Failure);

    EConstructDynamicMeshComponentFromAssetFileResult DynamicResult =
        EConstructDynamicMeshComponentFromAssetFileResult::Success;
    AddExpectedError(TEXT("LogAssetConstructor:"), EAutomationExpectedErrorFlags::Contains, 1);
    Passed &= TestNull(
        TEXT("Dynamic FromAssetFile should reject an empty path"),
        UAssetConstructor::ConstructDynamicMeshComponentFromAssetFile(TEXT(""), ParentMaterial, Owner, DynamicResult));
    Passed &= TestEqual(TEXT("Dynamic FromAssetFile should set Failure"), DynamicResult,
                        EConstructDynamicMeshComponentFromAssetFileResult::Failure);

    DestroyTestActor(World, Owner);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetConstructorInvalidMeshDataReturnsFailure,
                                 "RuntimeAssetImport.AssetConstructor.InvalidMeshDataReturnsFailure",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetConstructorInvalidMeshDataReturnsFailure::RunTest(const FString &Parameters)
{
    UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor *Owner = World != nullptr ? World->SpawnActor<AActor>() : nullptr;
    UMaterialInterface *ParentMaterial = LoadPluginParentMaterial();
    if (World == nullptr || Owner == nullptr || ParentMaterial == nullptr)
    {
        AddError(TEXT("Could not create the test world, actor, or plugin Parent Material."));
        DestroyTestActor(World, Owner);
        return false;
    }

    bool Passed = true;
    FLoadedMeshData InvalidData = MakeValidTriangleMeshData();
    InvalidData.NodeList[0].ParentNodeIndex = 0;
    Passed &= TestBothConstructorsReject(*this, TEXT("Invalid root parent"), InvalidData, ParentMaterial, Owner);

    InvalidData = MakeValidTriangleMeshData();
    FLoadedMeshNode Child = InvalidData.NodeList[0];
    Child.Sections.Reset();
    Child.ParentNodeIndex = 2;
    InvalidData.NodeList.Add(Child);
    Passed &= TestBothConstructorsReject(*this, TEXT("Future parent"), InvalidData, ParentMaterial, Owner);

    InvalidData.NodeList[1].ParentNodeIndex = 99;
    Passed &= TestBothConstructorsReject(*this, TEXT("Out-of-range parent"), InvalidData, ParentMaterial, Owner);

    InvalidData = MakeValidTriangleMeshData();
    InvalidData.NodeList[0].Sections[0].MaterialIndex = 4;
    Passed &= TestBothConstructorsReject(*this, TEXT("Out-of-range MaterialIndex"), InvalidData, ParentMaterial, Owner);

    InvalidData = MakeValidTriangleMeshData();
    InvalidData.NodeList[0].Sections[0].Triangles = {0, 1};
    Passed &= TestBothConstructorsReject(*this, TEXT("Triangle count not divisible by three"), InvalidData,
                                         ParentMaterial, Owner);

    InvalidData = MakeValidTriangleMeshData();
    InvalidData.NodeList[0].Sections[0].Triangles = {0, 1, 8};
    Passed &=
        TestBothConstructorsReject(*this, TEXT("Out-of-range triangle index"), InvalidData, ParentMaterial, Owner);

    InvalidData = MakeValidTriangleMeshData();
    InvalidData.NodeList[0].Sections[0].Normals.SetNum(2);
    Passed &= TestBothConstructorsReject(*this, TEXT("Mismatched Normals"), InvalidData, ParentMaterial, Owner);

    InvalidData = MakeValidTriangleMeshData();
    InvalidData.NodeList[0].Sections[0].UV0Channel.SetNum(2);
    Passed &= TestBothConstructorsReject(*this, TEXT("Mismatched UV0Channel"), InvalidData, ParentMaterial, Owner);

    InvalidData = MakeValidTriangleMeshData();
    InvalidData.MaterialList[0].ColorStatus = EColorStatus::TextureIsSet;
    InvalidData.MaterialList[0].CompressedTextureData.Reset();
    Passed &= TestBothConstructorsReject(*this, TEXT("TextureIsSet with empty texture data"), InvalidData,
                                         ParentMaterial, Owner);

    DestroyTestActor(World, Owner);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetConstructorMissingMaterialParametersReturnsFailure,
                                 "RuntimeAssetImport.AssetConstructor.MissingMaterialParametersReturnsFailure",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetConstructorMissingMaterialParametersReturnsFailure::RunTest(const FString &Parameters)
{
    UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor *Owner = World != nullptr ? World->SpawnActor<AActor>() : nullptr;
    UMaterialInterface *DefaultMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
    if (World == nullptr || Owner == nullptr || DefaultMaterial == nullptr)
    {
        AddError(TEXT("Could not create the test world, actor, or Engine default material."));
        DestroyTestActor(World, Owner);
        return false;
    }

    const bool Passed = TestBothConstructorsReject(*this, TEXT("Parent Material without required parameters"),
                                                   MakeValidTriangleMeshData(), DefaultMaterial, Owner);
    DestroyTestActor(World, Owner);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetConstructorDynamicMeshValidTriangleReturnsComponent,
                                 "RuntimeAssetImport.AssetConstructor.DynamicMeshValidTriangleReturnsComponent",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetConstructorDynamicMeshValidTriangleReturnsComponent::RunTest(const FString &Parameters)
{
    UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor *Owner = World != nullptr ? World->SpawnActor<AActor>() : nullptr;
    UMaterialInterface *ParentMaterial = LoadPluginParentMaterial();
    if (World == nullptr || Owner == nullptr || ParentMaterial == nullptr)
    {
        AddError(TEXT("Could not create the test world, actor, or plugin Parent Material."));
        DestroyTestActor(World, Owner);
        return false;
    }

    UDynamicMeshComponent *Component = UAssetConstructor::ConstructDynamicMeshComponentFromMeshData(
        MakeValidTriangleMeshData(), ParentMaterial, Owner);
    bool Passed = TestNotNull(TEXT("DynamicMesh construction should return a component"), Component);
    if (Component != nullptr)
    {
        Passed &= TestTrue(TEXT("DynamicMesh component should be registered"), Component->IsRegistered());
        Passed &=
            TestTrue(TEXT("DynamicMesh component should contain triangle geometry"),
                     Component->GetDynamicMesh() != nullptr && Component->GetDynamicMesh()->GetTriangleCount() > 0);
        Passed &= TestEqual(TEXT("DynamicMesh component should have one material"), Component->GetNumMaterials(), 1);
        Passed &= TestFalse(TEXT("DynamicMesh component bounds should be nonzero"),
                            Component->Bounds.BoxExtent.IsNearlyZero());
        Component->DestroyComponent();
    }
    DestroyTestActor(World, Owner);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetConstructorProceduralMeshValidTriangleReturnsComponent,
                                 "RuntimeAssetImport.AssetConstructor.ProceduralMeshValidTriangleReturnsComponent",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetConstructorProceduralMeshValidTriangleReturnsComponent::RunTest(const FString &Parameters)
{
    UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor *Owner = World != nullptr ? World->SpawnActor<AActor>() : nullptr;
    UMaterialInterface *ParentMaterial = LoadPluginParentMaterial();
    if (World == nullptr || Owner == nullptr || ParentMaterial == nullptr)
    {
        AddError(TEXT("Could not create the test world, actor, or plugin Parent Material."));
        DestroyTestActor(World, Owner);
        return false;
    }

    UProceduralMeshComponent *Component = UAssetConstructor::ConstructProceduralMeshComponentFromMeshData(
        MakeValidTriangleMeshData(), ParentMaterial, Owner);
    bool Passed = TestNotNull(TEXT("ProceduralMesh construction should return a component"), Component);
    if (Component != nullptr)
    {
        Passed &= TestTrue(TEXT("ProceduralMesh component should be registered"), Component->IsRegistered());
        Passed &= TestEqual(TEXT("ProceduralMesh component should have one section"), Component->GetNumSections(), 1);
        Passed &= TestEqual(TEXT("ProceduralMesh component should have one material"), Component->GetNumMaterials(), 1);
        Passed &= TestFalse(TEXT("ProceduralMesh component bounds should be nonzero"),
                            Component->CalcBounds(FTransform::Identity).BoxExtent.IsNearlyZero());
        Component->DestroyComponent();
    }
    DestroyTestActor(World, Owner);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetConstructorDynamicMeshFromObjFileReturnsSuccess,
                                 "RuntimeAssetImport.AssetConstructor.DynamicMeshFromObjFileReturnsSuccess",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetConstructorDynamicMeshFromObjFileReturnsSuccess::RunTest(const FString &Parameters)
{
    UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor *Owner = World != nullptr ? World->SpawnActor<AActor>() : nullptr;
    UMaterialInterface *ParentMaterial = LoadPluginParentMaterial();
    EConstructDynamicMeshComponentFromAssetFileResult Result =
        EConstructDynamicMeshComponentFromAssetFileResult::Failure;
    const FString ObjPath = ResolveTestAssetPath(TEXT("test_triangle.obj"));
    bool Passed = TestTrue(*FString::Printf(TEXT("DynamicMesh OBJ test asset should exist: %s"), *ObjPath),
                           FPaths::FileExists(ObjPath));
    UDynamicMeshComponent *Component =
        Owner != nullptr && ParentMaterial != nullptr
            ? UAssetConstructor::ConstructDynamicMeshComponentFromAssetFile(ObjPath, ParentMaterial, Owner, Result)
            : nullptr;
    Passed &= TestEqual(TEXT("DynamicMesh OBJ construction should return Success"), Result,
                        EConstructDynamicMeshComponentFromAssetFileResult::Success);
    Passed &= TestNotNull(TEXT("DynamicMesh OBJ construction should return a component"), Component);
    if (Component != nullptr)
    {
        Component->DestroyComponent();
    }
    DestroyTestActor(World, Owner);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetConstructorProceduralMeshFromObjFileReturnsSuccess,
                                 "RuntimeAssetImport.AssetConstructor.ProceduralMeshFromObjFileReturnsSuccess",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetConstructorProceduralMeshFromObjFileReturnsSuccess::RunTest(const FString &Parameters)
{
    UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor *Owner = World != nullptr ? World->SpawnActor<AActor>() : nullptr;
    UMaterialInterface *ParentMaterial = LoadPluginParentMaterial();
    EConstructProceduralMeshComponentFromAssetFileResult Result =
        EConstructProceduralMeshComponentFromAssetFileResult::Failure;
    const FString ObjPath = ResolveTestAssetPath(TEXT("test_triangle.obj"));
    bool Passed = TestTrue(*FString::Printf(TEXT("ProceduralMesh OBJ test asset should exist: %s"), *ObjPath),
                           FPaths::FileExists(ObjPath));
    UProceduralMeshComponent *Component =
        Owner != nullptr && ParentMaterial != nullptr
            ? UAssetConstructor::ConstructProceduralMeshComponentFromAssetFile(ObjPath, ParentMaterial, Owner, Result)
            : nullptr;
    Passed &= TestEqual(TEXT("ProceduralMesh OBJ construction should return Success"), Result,
                        EConstructProceduralMeshComponentFromAssetFileResult::Success);
    Passed &= TestNotNull(TEXT("ProceduralMesh OBJ construction should return a component"), Component);
    if (Component != nullptr)
    {
        Component->DestroyComponent();
    }
    DestroyTestActor(World, Owner);
    return Passed;
}
