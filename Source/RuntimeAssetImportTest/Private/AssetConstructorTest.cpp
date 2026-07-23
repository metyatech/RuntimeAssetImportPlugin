// Copyright (c) 2026 metyatech. All rights reserved.

#include "AssetConstructor.h"
#include "AssetImportLimits.h"
#include "AssetLoader.h"
#include "Components/SceneComponent.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MaterialDomain.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Tests/AutomationEditorCommon.h"
#include "UDynamicMesh.h"

#include <limits>

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
        const int32 InitialComponentCount = Owner != nullptr ? Owner->GetComponents().Num() : 0;
        bool Passed = Test.TestNull(
            *FString::Printf(TEXT("%s should be rejected by ProceduralMesh construction"), *Label),
            UAssetConstructor::ConstructProceduralMeshComponentFromMeshData(MeshData, ParentMaterial, Owner));
        if (Owner != nullptr)
        {
            Passed &= Test.TestEqual(*FString::Printf(TEXT("%s should leave no ProceduralMesh components"), *Label),
                                     Owner->GetComponents().Num(), InitialComponentCount);
        }
        Passed &= Test.TestNull(
            *FString::Printf(TEXT("%s should be rejected by DynamicMesh construction"), *Label),
            UAssetConstructor::ConstructDynamicMeshComponentFromMeshData(MeshData, ParentMaterial, Owner));
        if (Owner != nullptr)
        {
            Passed &= Test.TestEqual(*FString::Printf(TEXT("%s should leave no DynamicMesh components"), *Label),
                                     Owner->GetComponents().Num(), InitialComponentCount);
        }
        return Passed;
    }

    template <typename MeshComponentT>
    MeshComponentT *ConstructTriangle(UMaterialInterface *ParentMaterial, AActor *Owner)
    {
        if constexpr (TypeTests::TAreTypesEqual_V<UDynamicMeshComponent, MeshComponentT>)
        {
            return UAssetConstructor::ConstructDynamicMeshComponentFromMeshData(MakeValidTriangleMeshData(),
                                                                                ParentMaterial, Owner);
        }
        else
        {
            return UAssetConstructor::ConstructProceduralMeshComponentFromMeshData(MakeValidTriangleMeshData(),
                                                                                   ParentMaterial, Owner);
        }
    }

    template <typename MeshComponentT>
    MeshComponentT *ConstructMaterialTestMesh(const FLoadedMeshData &MeshData, UMaterialInterface *ParentMaterial,
                                              AActor *Owner)
    {
        if constexpr (TypeTests::TAreTypesEqual_V<UDynamicMeshComponent, MeshComponentT>)
        {
            return UAssetConstructor::ConstructDynamicMeshComponentFromMeshData(MeshData, ParentMaterial, Owner);
        }
        else
        {
            return UAssetConstructor::ConstructProceduralMeshComponentFromMeshData(MeshData, ParentMaterial, Owner);
        }
    }

    template <typename MeshComponentT>
    bool VerifyConstructorMaterialParameters(FAutomationTestBase &Test, const FString &Label,
                                             const FLoadedMeshData &MeshData, UMaterialInterface *ParentMaterial,
                                             AActor *Owner, const EColorStatus ExpectedStatus,
                                             const FLinearColor &ExpectedColor)
    {
        MeshComponentT *Component = ConstructMaterialTestMesh<MeshComponentT>(MeshData, ParentMaterial, Owner);
        bool Passed = Test.TestNotNull(*FString::Printf(TEXT("%s should construct"), *Label), Component);
        if (Component == nullptr)
        {
            return false;
        }

        TArray<MeshComponentT *> CreatedComponents;
        Owner->GetComponents<MeshComponentT>(CreatedComponents);
        UMaterialInstanceDynamic *MaterialInstance = nullptr;
        for (MeshComponentT *CreatedComponent : CreatedComponents)
        {
            for (int32 MaterialSlot = 0; MaterialSlot < CreatedComponent->GetNumMaterials(); ++MaterialSlot)
            {
                MaterialInstance = Cast<UMaterialInstanceDynamic>(CreatedComponent->GetMaterial(MaterialSlot));
                if (MaterialInstance != nullptr)
                {
                    break;
                }
            }
            if (MaterialInstance != nullptr)
            {
                break;
            }
        }
        Passed &= Test.TestNotNull(*FString::Printf(TEXT("%s should use a MID"), *Label), MaterialInstance);
        if (MaterialInstance != nullptr)
        {
            const float ActualBlend =
                MaterialInstance->K2_GetScalarParameterValue(TEXT("TextureBlendIntensityForBaseColor"));
            const float ExpectedBlend = ExpectedStatus == EColorStatus::TextureIsSet ? 1.0f : 0.0f;
            Passed &= Test.TestTrue(*FString::Printf(TEXT("%s blend scalar should match"), *Label),
                                    FMath::IsNearlyEqual(ActualBlend, ExpectedBlend));
            if (ExpectedStatus == EColorStatus::ColorIsSet)
            {
                const FLinearColor ActualColor = MaterialInstance->K2_GetVectorParameterValue(TEXT("BaseColor4"));
                Passed &= Test.TestTrue(*FString::Printf(TEXT("%s color vector should match"), *Label),
                                        ActualColor.Equals(ExpectedColor, KINDA_SMALL_NUMBER));
            }
            else
            {
                UTexture *ActualTexture = MaterialInstance->K2_GetTextureParameterValue(TEXT("BaseColorTexture"));
                Passed &= Test.TestNotNull(*FString::Printf(TEXT("%s texture parameter should be set"), *Label),
                                           ActualTexture);
            }
        }

        if (Owner->GetRootComponent() != nullptr &&
            CreatedComponents.Contains(Cast<MeshComponentT>(Owner->GetRootComponent())))
        {
            Owner->SetRootComponent(nullptr);
        }
        for (int32 ComponentIndex = CreatedComponents.Num() - 1; ComponentIndex >= 0; --ComponentIndex)
        {
            CreatedComponents[ComponentIndex]->DestroyComponent();
        }
        return Passed;
    }

    bool VerifyBothConstructorMaterialParameters(FAutomationTestBase &Test, const FString &Label,
                                                 const FLoadedMeshData &MeshData, UMaterialInterface *ParentMaterial,
                                                 AActor *Owner, const EColorStatus ExpectedStatus,
                                                 const FLinearColor &ExpectedColor = FLinearColor::Transparent)
    {
        bool Passed = VerifyConstructorMaterialParameters<UProceduralMeshComponent>(
            Test, Label + TEXT(" ProceduralMesh"), MeshData, ParentMaterial, Owner, ExpectedStatus, ExpectedColor);
        Passed &= VerifyConstructorMaterialParameters<UDynamicMeshComponent>(
            Test, Label + TEXT(" DynamicMesh"), MeshData, ParentMaterial, Owner, ExpectedStatus, ExpectedColor);
        return Passed;
    }

    template <typename MeshComponentT>
    bool TestExistingRootTracksOwnerTransform(FAutomationTestBase &Test, const FString &Label)
    {
        UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
        AActor *Owner = World != nullptr ? World->SpawnActor<AActor>() : nullptr;
        UMaterialInterface *ParentMaterial = LoadPluginParentMaterial();
        USceneComponent *ExistingRoot = Owner != nullptr ? NewObject<USceneComponent>(Owner) : nullptr;
        if (World == nullptr || Owner == nullptr || ParentMaterial == nullptr || ExistingRoot == nullptr)
        {
            Test.AddError(TEXT("Could not create the test world, actor, Parent Material, or existing root."));
            DestroyTestActor(World, Owner);
            return false;
        }

        bool Passed = Test.TestTrue(*FString::Printf(TEXT("%s existing root should install"), *Label),
                                    Owner->SetRootComponent(ExistingRoot));
        ExistingRoot->RegisterComponent();
        Passed &= Test.TestTrue(*FString::Printf(TEXT("%s existing root should register"), *Label),
                                ExistingRoot->IsRegistered());

        const FTransform InitialOwnerTransform(FRotator(10.0, 20.0, 30.0), FVector(100.0, 200.0, 300.0),
                                               FVector(1.25, 0.75, 1.5));
        Owner->SetActorTransform(InitialOwnerTransform);
        ExistingRoot->UpdateComponentToWorld();

        MeshComponentT *Component = ConstructTriangle<MeshComponentT>(ParentMaterial, Owner);
        Passed &= Test.TestNotNull(*FString::Printf(TEXT("%s should return a component"), *Label), Component);
        if (Component != nullptr)
        {
            Passed &= Test.TestEqual(*FString::Printf(TEXT("%s should attach to the existing root"), *Label),
                                     Component->GetAttachParent(), ExistingRoot);

            const FTransform UpdatedOwnerTransform(FRotator(-15.0, 45.0, 5.0), FVector(-50.0, 75.0, 125.0),
                                                   FVector(0.5, 1.25, 2.0));
            Owner->SetActorTransform(UpdatedOwnerTransform);
            ExistingRoot->UpdateComponentToWorld();
            Component->UpdateComponentToWorld();

            Passed &= Test.TestTrue(
                *FString::Printf(TEXT("%s location should follow the owner root"), *Label),
                Component->GetComponentLocation().Equals(ExistingRoot->GetComponentLocation(), KINDA_SMALL_NUMBER));
            Passed &= Test.TestTrue(
                *FString::Printf(TEXT("%s rotation should follow the owner root"), *Label),
                Component->GetComponentQuat().Equals(ExistingRoot->GetComponentQuat(), KINDA_SMALL_NUMBER));
            Passed &= Test.TestTrue(
                *FString::Printf(TEXT("%s scale should follow the owner root"), *Label),
                Component->GetComponentScale().Equals(ExistingRoot->GetComponentScale(), KINDA_SMALL_NUMBER));
            Component->DestroyComponent();
        }

        Owner->SetRootComponent(nullptr);
        ExistingRoot->DestroyComponent();
        DestroyTestActor(World, Owner);
        return Passed;
    }

    template <typename MeshComponentT>
    bool TestOwnerWithoutRootUsesGeneratedRoot(FAutomationTestBase &Test, const FString &Label)
    {
        UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
        AActor *Owner = World != nullptr ? World->SpawnActor<AActor>() : nullptr;
        UMaterialInterface *ParentMaterial = LoadPluginParentMaterial();
        if (World == nullptr || Owner == nullptr || ParentMaterial == nullptr)
        {
            Test.AddError(TEXT("Could not create the test world, actor, or plugin Parent Material."));
            DestroyTestActor(World, Owner);
            return false;
        }

        bool Passed = Test.TestNull(*FString::Printf(TEXT("%s owner should start without a root"), *Label),
                                    Owner->GetRootComponent());
        MeshComponentT *Component = ConstructTriangle<MeshComponentT>(ParentMaterial, Owner);
        Passed &= Test.TestNotNull(*FString::Printf(TEXT("%s should return a component"), *Label), Component);
        if (Component != nullptr)
        {
            Passed &= Test.TestEqual(*FString::Printf(TEXT("%s should become the owner root"), *Label),
                                     Owner->GetRootComponent(), static_cast<USceneComponent *>(Component));
            Passed &= Test.TestTrue(*FString::Printf(TEXT("%s should register"), *Label), Component->IsRegistered());
            Owner->SetRootComponent(nullptr);
            Component->DestroyComponent();
        }

        DestroyTestActor(World, Owner);
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

    InvalidData = MakeValidTriangleMeshData();
    InvalidData.MaterialList[0].ColorStatus = static_cast<EColorStatus>(255);
    Passed &= TestBothConstructorsReject(*this, TEXT("Invalid ColorStatus"), InvalidData, ParentMaterial, Owner);

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetConstructorInvalidNumericValuesReturnFailure,
                                 "RuntimeAssetImport.AssetConstructor.InvalidNumericValuesReturnFailure",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetConstructorInvalidNumericValuesReturnFailure::RunTest(const FString &Parameters)
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

    struct FInvalidNumericCase
    {
        const TCHAR *FieldName;
        TFunction<void(FLoadedMeshData &, float)> Mutate;
    };
    const TArray<FInvalidNumericCase> Cases = {
        {TEXT("Node RelativeTransform translation"), [](FLoadedMeshData &MeshData, const float Value)
         { MeshData.NodeList[0].RelativeTransform.SetTranslation(FVector(Value, 0.0, 0.0)); }},
        {TEXT("Node RelativeTransform scale"), [](FLoadedMeshData &MeshData, const float Value)
         { MeshData.NodeList[0].RelativeTransform.SetScale3D(FVector(Value, 1.0, 1.0)); }},
        {TEXT("Node RelativeTransform quaternion"), [](FLoadedMeshData &MeshData, const float Value)
         { MeshData.NodeList[0].RelativeTransform.SetRotation(FQuat(Value, 0.0, 0.0, 1.0)); }},
        {TEXT("Vertex"),
         [](FLoadedMeshData &MeshData, const float Value) { MeshData.NodeList[0].Sections[0].Vertices[0].X = Value; }},
        {TEXT("Normal"),
         [](FLoadedMeshData &MeshData, const float Value) { MeshData.NodeList[0].Sections[0].Normals[0].X = Value; }},
        {TEXT("UV"), [](FLoadedMeshData &MeshData, const float Value)
         { MeshData.NodeList[0].Sections[0].UV0Channel[0].X = Value; }},
        {TEXT("VertexColor"), [](FLoadedMeshData &MeshData, const float Value)
         { MeshData.NodeList[0].Sections[0].VertexColors0[0].R = Value; }},
        {TEXT("TangentX"), [](FLoadedMeshData &MeshData, const float Value)
         { MeshData.NodeList[0].Sections[0].Tangents[0].TangentX.X = Value; }},
        {TEXT("Material Color"),
         [](FLoadedMeshData &MeshData, const float Value) { MeshData.MaterialList[0].Color.R = Value; }},
    };
    const struct
    {
        const TCHAR *Name;
        float Value;
    } InvalidValues[] = {
        {TEXT("NaN"), std::numeric_limits<float>::quiet_NaN()},
        {TEXT("+Inf"), std::numeric_limits<float>::infinity()},
        {TEXT("-Inf"), -std::numeric_limits<float>::infinity()},
    };

    bool Passed = true;
    for (const auto &InvalidValue : InvalidValues)
    {
        for (const FInvalidNumericCase &Case : Cases)
        {
            FLoadedMeshData InvalidData = MakeValidTriangleMeshData();
            Case.Mutate(InvalidData, InvalidValue.Value);
            const FString Label = FString::Printf(TEXT("%s %s"), Case.FieldName, InvalidValue.Name);
            Passed &= TestBothConstructorsReject(*this, Label, InvalidData, ParentMaterial, Owner);
        }
    }

    DestroyTestActor(World, Owner);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetConstructorInvalidQuaternionReturnsFailure,
                                 "RuntimeAssetImport.AssetConstructor.InvalidQuaternionReturnsFailure",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetConstructorInvalidQuaternionReturnsFailure::RunTest(const FString &Parameters)
{
    UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor *Owner = World != nullptr ? World->SpawnActor<AActor>() : nullptr;
    UMaterialInterface *ParentMaterial = LoadPluginParentMaterial();
    if (World == nullptr || Owner == nullptr || ParentMaterial == nullptr)
    {
        AddError(TEXT("Could not create the quaternion test world, actor, or plugin Parent Material."));
        DestroyTestActor(World, Owner);
        return false;
    }

    FLoadedMeshData ZeroQuaternion = MakeValidTriangleMeshData();
    ZeroQuaternion.NodeList[0].RelativeTransform.SetRotation(FQuat(0.0, 0.0, 0.0, 0.0));
    bool Passed = TestBothConstructorsReject(*this, TEXT("Zero quaternion"), ZeroQuaternion, ParentMaterial, Owner);

    FLoadedMeshData NonNormalizedQuaternion = MakeValidTriangleMeshData();
    NonNormalizedQuaternion.NodeList[0].RelativeTransform.SetRotation(FQuat(2.0, 0.0, 0.0, 0.0));
    Passed &= TestBothConstructorsReject(*this, TEXT("Finite non-normalized quaternion"), NonNormalizedQuaternion,
                                         ParentMaterial, Owner);

    DestroyTestActor(World, Owner);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetConstructorOversizedCompressedTextureReturnsFailure,
                                 "RuntimeAssetImport.AssetConstructor.OversizedCompressedTextureReturnsFailure",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetConstructorOversizedCompressedTextureReturnsFailure::RunTest(const FString &Parameters)
{
    UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor *Owner = World != nullptr ? World->SpawnActor<AActor>() : nullptr;
    UMaterialInterface *ParentMaterial = LoadPluginParentMaterial();
    if (World == nullptr || Owner == nullptr || ParentMaterial == nullptr)
    {
        AddError(TEXT("Could not create the texture limit test world, actor, or plugin Parent Material."));
        DestroyTestActor(World, Owner);
        return false;
    }

    bool Passed = TestFalse(TEXT("An empty compressed texture should fail the common boundary gate"),
                            RuntimeAssetImport::Limits::IsCompressedTextureByteCountValid(0));
    Passed &= TestTrue(TEXT("The exact compressed texture limit should pass the common boundary gate"),
                       RuntimeAssetImport::Limits::IsCompressedTextureByteCountValid(
                           RuntimeAssetImport::Limits::MaximumCompressedTextureBytes));
    Passed &= TestFalse(TEXT("One byte beyond the compressed texture limit should fail the common boundary gate"),
                        RuntimeAssetImport::Limits::IsCompressedTextureByteCountValid(
                            RuntimeAssetImport::Limits::MaximumCompressedTextureBytes + 1));

    FLoadedMeshData OversizedTexture = MakeValidTriangleMeshData();
    OversizedTexture.MaterialList[0].ColorStatus = EColorStatus::TextureIsSet;
    OversizedTexture.MaterialList[0].CompressedTextureData.SetNumUninitialized(
        static_cast<int32>(RuntimeAssetImport::Limits::MaximumCompressedTextureBytes + 1));
    Passed &= TestBothConstructorsReject(*this, TEXT("Oversized compressed texture"), OversizedTexture, ParentMaterial,
                                         Owner);

    DestroyTestActor(World, Owner);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetConstructorColorMaterialParametersApplied,
                                 "RuntimeAssetImport.AssetConstructor.Material.ColorParametersApplied",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetConstructorColorMaterialParametersApplied::RunTest(const FString &Parameters)
{
    UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor *Owner = World != nullptr ? World->SpawnActor<AActor>() : nullptr;
    UMaterialInterface *ParentMaterial = LoadPluginParentMaterial();
    if (World == nullptr || Owner == nullptr || ParentMaterial == nullptr)
    {
        AddError(TEXT("Could not create the color material test world, actor, or plugin Parent Material."));
        DestroyTestActor(World, Owner);
        return false;
    }

    FLoadedMeshData MeshData = MakeValidTriangleMeshData();
    const FLinearColor ExpectedColor(0.8f, 0.4f, 0.2f, 1.0f);
    MeshData.MaterialList[0].Color = ExpectedColor;
    const bool Passed = VerifyBothConstructorMaterialParameters(*this, TEXT("Color material"), MeshData, ParentMaterial,
                                                                Owner, EColorStatus::ColorIsSet, ExpectedColor);
    DestroyTestActor(World, Owner);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetConstructorExternalTextureParametersApplied,
                                 "RuntimeAssetImport.AssetConstructor.Material.ExternalTextureParametersApplied",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetConstructorExternalTextureParametersApplied::RunTest(const FString &Parameters)
{
    UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor *Owner = World != nullptr ? World->SpawnActor<AActor>() : nullptr;
    UMaterialInterface *ParentMaterial = LoadPluginParentMaterial();
    ELoadMeshFromAssetFileResult LoadResult = ELoadMeshFromAssetFileResult::Failure;
    const FLoadedMeshData MeshData =
        UAssetLoader::LoadMeshFromAssetFile(ResolveTestAssetPath(TEXT("test_external_texture.obj")), LoadResult);
    if (World == nullptr || Owner == nullptr || ParentMaterial == nullptr)
    {
        AddError(TEXT("Could not create the external texture test world, actor, or plugin Parent Material."));
        DestroyTestActor(World, Owner);
        return false;
    }

    bool Passed =
        TestEqual(TEXT("External texture asset should load"), LoadResult, ELoadMeshFromAssetFileResult::Success);
    Passed &= VerifyBothConstructorMaterialParameters(*this, TEXT("External texture material"), MeshData,
                                                      ParentMaterial, Owner, EColorStatus::TextureIsSet);
    DestroyTestActor(World, Owner);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetConstructorEmbeddedTextureParametersApplied,
                                 "RuntimeAssetImport.AssetConstructor.Material.EmbeddedTextureParametersApplied",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetConstructorEmbeddedTextureParametersApplied::RunTest(const FString &Parameters)
{
    UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor *Owner = World != nullptr ? World->SpawnActor<AActor>() : nullptr;
    UMaterialInterface *ParentMaterial = LoadPluginParentMaterial();
    TArray<uint8> AssetBytes;
    const FString AssetPath = ResolveTestAssetPath(TEXT("test_embedded_texture.gltf"));
    if (!FFileHelper::LoadFileToArray(AssetBytes, *AssetPath))
    {
        AddError(FString::Printf(TEXT("Could not read embedded texture test asset: %s"), *AssetPath));
        DestroyTestActor(World, Owner);
        return false;
    }
    ELoadMeshFromAssetDataResult LoadResult = ELoadMeshFromAssetDataResult::Failure;
    const FLoadedMeshData MeshData = UAssetLoader::LoadMeshFromAssetData(AssetBytes, LoadResult);
    if (World == nullptr || Owner == nullptr || ParentMaterial == nullptr)
    {
        AddError(TEXT("Could not create the embedded texture test world, actor, or plugin Parent Material."));
        DestroyTestActor(World, Owner);
        return false;
    }

    bool Passed =
        TestEqual(TEXT("Embedded texture bytes should load"), LoadResult, ELoadMeshFromAssetDataResult::Success);
    Passed &= VerifyBothConstructorMaterialParameters(*this, TEXT("Embedded texture material"), MeshData,
                                                      ParentMaterial, Owner, EColorStatus::TextureIsSet);
    DestroyTestActor(World, Owner);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExistingRootDynamicMeshTracksOwnerTransform,
                                 "RuntimeAssetImport.AssetConstructor.ExistingRootDynamicMeshTracksOwnerTransform",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExistingRootDynamicMeshTracksOwnerTransform::RunTest(const FString &Parameters)
{
    return TestExistingRootTracksOwnerTransform<UDynamicMeshComponent>(*this, TEXT("DynamicMesh"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExistingRootProceduralMeshTracksOwnerTransform,
                                 "RuntimeAssetImport.AssetConstructor.ExistingRootProceduralMeshTracksOwnerTransform",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExistingRootProceduralMeshTracksOwnerTransform::RunTest(const FString &Parameters)
{
    return TestExistingRootTracksOwnerTransform<UProceduralMeshComponent>(*this, TEXT("ProceduralMesh"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetConstructorOwnerWithoutRootUsesGeneratedDynamicRoot,
                                 "RuntimeAssetImport.AssetConstructor.OwnerWithoutRootUsesGeneratedDynamicRoot",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetConstructorOwnerWithoutRootUsesGeneratedDynamicRoot::RunTest(const FString &Parameters)
{
    return TestOwnerWithoutRootUsesGeneratedRoot<UDynamicMeshComponent>(*this, TEXT("DynamicMesh"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetConstructorOwnerWithoutRootUsesGeneratedProceduralRoot,
                                 "RuntimeAssetImport.AssetConstructor.OwnerWithoutRootUsesGeneratedProceduralRoot",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetConstructorOwnerWithoutRootUsesGeneratedProceduralRoot::RunTest(const FString &Parameters)
{
    return TestOwnerWithoutRootUsesGeneratedRoot<UProceduralMeshComponent>(*this, TEXT("ProceduralMesh"));
}
