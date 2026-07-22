// Copyright (c) 2026 metyatech. All rights reserved.

#include "RuntimeAssetImportSmokeGameInstance.h"

#include "AssetConstructor.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformMisc.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TimerManager.h"
#include "UDynamicMesh.h"

DEFINE_LOG_CATEGORY_STATIC(LogRuntimeAssetImportSmoke, Log, All);

namespace
{
    struct FSmokeAsset
    {
        const TCHAR *Format;
        const TCHAR *FileName;
    };

    constexpr FSmokeAsset SmokeAssets[] = {
        {TEXT("FBX"), TEXT("test_triangle.fbx")}, {TEXT("OBJ"), TEXT("test_triangle.obj")},
        {TEXT("DAE"), TEXT("test_triangle.dae")}, {TEXT("glTF"), TEXT("test_scene.gltf")},
        {TEXT("GLB"), TEXT("test_triangle.glb")},
    };

    bool TransformComponentsNearlyEqual(const FTransform &Actual, const FTransform &Expected)
    {
        return Actual.GetLocation().Equals(Expected.GetLocation(), KINDA_SMALL_NUMBER) &&
               Actual.GetRotation().Equals(Expected.GetRotation(), KINDA_SMALL_NUMBER) &&
               Actual.GetScale3D().Equals(Expected.GetScale3D(), KINDA_SMALL_NUMBER);
    }
} // namespace

void URuntimeAssetImportSmokeGameInstance::Init()
{
    Super::Init();
    WorldInitializationHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(
        this, &URuntimeAssetImportSmokeGameInstance::HandlePostWorldInitialization);
}

void URuntimeAssetImportSmokeGameInstance::Shutdown()
{
    if (WorldInitializationHandle.IsValid())
    {
        FWorldDelegates::OnPostWorldInitialization.Remove(WorldInitializationHandle);
        WorldInitializationHandle.Reset();
    }
    Super::Shutdown();
}

void URuntimeAssetImportSmokeGameInstance::HandlePostWorldInitialization(
    UWorld *World, const UWorld::InitializationValues InitializationValues)
{
    if (bSmokeStarted || World == nullptr || !World->IsGameWorld())
    {
        return;
    }

    bSmokeStarted = true;
    if (WorldInitializationHandle.IsValid())
    {
        FWorldDelegates::OnPostWorldInitialization.Remove(WorldInitializationHandle);
        WorldInitializationHandle.Reset();
    }
    RunSmoke(World);
}

void URuntimeAssetImportSmokeGameInstance::RunSmoke(UWorld *World)
{
    FormatResults.Reset();
    FormatResults.Reserve(UE_ARRAY_COUNT(SmokeAssets));

    UMaterialInterface *ParentMaterial = LoadObject<UMaterialInterface>(
        nullptr, TEXT("/RuntimeAssetImport/AssetImporterMeshMaterial.AssetImporterMeshMaterial"));
    if (ParentMaterial == nullptr)
    {
        UE_LOG(LogRuntimeAssetImportSmoke, Error, TEXT("Could not load the Runtime Asset Import Parent Material."));
    }

    for (const FSmokeAsset &SmokeAsset : SmokeAssets)
    {
        FRuntimeAssetImportSmokeFormatResult &Result = FormatResults.AddDefaulted_GetRef();
        Result.Format = SmokeAsset.Format;

        AActor *Owner = World->SpawnActor<AActor>();
        USceneComponent *OwnerRoot = Owner != nullptr ? NewObject<USceneComponent>(Owner) : nullptr;
        if (Owner == nullptr || OwnerRoot == nullptr || ParentMaterial == nullptr ||
            !Owner->SetRootComponent(OwnerRoot))
        {
            UE_LOG(LogRuntimeAssetImportSmoke, Error, TEXT("%s: failed to create the Owner hierarchy."),
                   SmokeAsset.Format);
            continue;
        }

        OwnerRoot->RegisterComponent();
        if (!OwnerRoot->IsRegistered())
        {
            UE_LOG(LogRuntimeAssetImportSmoke, Error, TEXT("%s: Owner root registration failed."), SmokeAsset.Format);
            continue;
        }
        Owner->SetActorTransform(
            FTransform(FRotator(10.0, 20.0, 30.0), FVector(100.0, 200.0, 300.0), FVector(1.25, 0.75, 1.5)));
        OwnerRoot->UpdateComponentToWorld();

        EConstructDynamicMeshComponentFromAssetFileResult ImportResult =
            EConstructDynamicMeshComponentFromAssetFileResult::Failure;
        const FString AssetPath =
            FPaths::Combine(FPaths::ProjectContentDir(), TEXT("SmokeAssets"), SmokeAsset.FileName);
        UDynamicMeshComponent *Component = UAssetConstructor::ConstructDynamicMeshComponentFromAssetFile(
            AssetPath, ParentMaterial, Owner, ImportResult);

        Result.Owner = Owner;
        Result.OwnerRoot = OwnerRoot;
        Result.Component = Component;
        Result.bImportSuccess =
            ImportResult == EConstructDynamicMeshComponentFromAssetFileResult::Success && Component != nullptr;
        if (Component == nullptr)
        {
            UE_LOG(LogRuntimeAssetImportSmoke, Error, TEXT("%s: import or construction failed for '%s'."),
                   SmokeAsset.Format, *AssetPath);
            continue;
        }

        TInlineComponentArray<UDynamicMeshComponent *> GeneratedComponents;
        Owner->GetComponents(GeneratedComponents);
        Result.bComponentRegistered = Component->IsRegistered();
        for (UDynamicMeshComponent *GeneratedComponent : GeneratedComponents)
        {
            if (GeneratedComponent == nullptr ||
                (GeneratedComponent != Component && !GeneratedComponent->IsAttachedTo(Component)))
            {
                continue;
            }

            GeneratedComponent->UpdateBounds();
            Result.bComponentRegistered &= GeneratedComponent->IsRegistered();
            const int32 ComponentTriangleCount = GeneratedComponent->GetDynamicMesh() != nullptr
                                                     ? GeneratedComponent->GetDynamicMesh()->GetTriangleCount()
                                                     : 0;
            Result.TriangleCount += ComponentTriangleCount;
            Result.MaterialCount += GeneratedComponent->GetNumMaterials();
            Result.bMaterialSlot0Valid |=
                GeneratedComponent->GetNumMaterials() > 0 && GeneratedComponent->GetMaterial(0) != nullptr;
            Result.bBoundsNonZero |= !GeneratedComponent->Bounds.BoxExtent.IsNearlyZero();
            if (ComponentTriangleCount > 0)
            {
                GeneratedComponent->UpdateCollision(false);
                Result.bCollisionEnabled |= GeneratedComponent->GetCollisionEnabled() != ECollisionEnabled::NoCollision;
                Result.bCollisionData |= GeneratedComponent->ContainsPhysicsTriMeshData(true);
                if (!Result.GeometryComponent.IsValid())
                {
                    Result.GeometryComponent = GeneratedComponent;
                }
            }
        }
        Result.bAttachedToOwnerRoot = Component->GetAttachParent() == OwnerRoot;
        Result.InitialComponentWorldTransform = Component->GetComponentTransform();

        Owner->SetActorTransform(
            FTransform(FRotator(-15.0, 45.0, 5.0), FVector(-50.0, 75.0, 125.0), FVector(0.5, 1.25, 2.0)));
    }

    World->GetTimerManager().SetTimerForNextTick(
        FTimerDelegate::CreateUObject(this, &URuntimeAssetImportSmokeGameInstance::FinalizeSmoke, World));
}

void URuntimeAssetImportSmokeGameInstance::FinalizeSmoke(UWorld *World)
{
    for (FRuntimeAssetImportSmokeFormatResult &Result : FormatResults)
    {
        UDynamicMeshComponent *Component = Result.Component.Get();
        UDynamicMeshComponent *GeometryComponent = Result.GeometryComponent.Get();
        USceneComponent *OwnerRoot = Result.OwnerRoot.Get();
        if (Component == nullptr || GeometryComponent == nullptr || OwnerRoot == nullptr)
        {
            continue;
        }

        OwnerRoot->UpdateComponentToWorld();
        Component->UpdateComponentToWorld();
        Component->UpdateBounds();

        const FTransform ExpectedWorldTransform =
            Component->GetRelativeTransform() * OwnerRoot->GetComponentTransform();
        Result.bFollowedOwnerTransform =
            !Component->GetComponentTransform().Equals(Result.InitialComponentWorldTransform, KINDA_SMALL_NUMBER) &&
            TransformComponentsNearlyEqual(Component->GetComponentTransform(), ExpectedWorldTransform);

        GeometryComponent->UpdateComponentToWorld();
        GeometryComponent->UpdateBounds();
        GeometryComponent->UpdateCollision(false);
        const FVector Center = GeometryComponent->Bounds.Origin;
        const double TraceDistance = GeometryComponent->Bounds.BoxExtent.GetMax() + 100.0;
        const FVector TraceAxes[] = {FVector::XAxisVector, FVector::YAxisVector, FVector::ZAxisVector};
        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RuntimeAssetImportPackagedSmoke), true);
        for (const FRuntimeAssetImportSmokeFormatResult &OtherResult : FormatResults)
        {
            AActor *OtherOwner = OtherResult.Owner.Get();
            if (OtherOwner != nullptr && OtherOwner != Result.Owner.Get())
            {
                QueryParams.AddIgnoredActor(OtherOwner);
            }
        }
        for (const FVector &Axis : TraceAxes)
        {
            const FVector Start = Center - Axis * TraceDistance;
            const FVector End = Center + Axis * TraceDistance;
            FHitResult ForwardHit;
            FHitResult ReverseHit;
            const bool bForwardHit =
                World->LineTraceSingleByChannel(ForwardHit, Start, End, ECC_Visibility, QueryParams) &&
                ForwardHit.GetComponent() == GeometryComponent;
            const bool bReverseHit =
                World->LineTraceSingleByChannel(ReverseHit, End, Start, ECC_Visibility, QueryParams) &&
                ReverseHit.GetComponent() == GeometryComponent;
            if (bForwardHit || bReverseHit)
            {
                Result.bCollisionHit = true;
                break;
            }
        }
    }

    WriteResultsAndExit();
}

void URuntimeAssetImportSmokeGameInstance::WriteResultsAndExit()
{
    bool bOverallSuccess = FormatResults.Num() == UE_ARRAY_COUNT(SmokeAssets);
    TArray<TSharedPtr<FJsonValue>> JsonFormats;
    for (const FRuntimeAssetImportSmokeFormatResult &Result : FormatResults)
    {
        const bool bFormatSuccess = Result.bImportSuccess && Result.bComponentRegistered && Result.TriangleCount > 0 &&
                                    Result.MaterialCount > 0 && Result.bMaterialSlot0Valid && Result.bBoundsNonZero &&
                                    Result.bCollisionEnabled && Result.bCollisionData && Result.bCollisionHit &&
                                    Result.bAttachedToOwnerRoot && Result.bFollowedOwnerTransform;
        bOverallSuccess &= bFormatSuccess;

        TSharedRef<FJsonObject> JsonFormat = MakeShared<FJsonObject>();
        JsonFormat->SetStringField(TEXT("Format"), Result.Format);
        JsonFormat->SetBoolField(TEXT("ImportSuccess"), Result.bImportSuccess);
        JsonFormat->SetBoolField(TEXT("ComponentRegistered"), Result.bComponentRegistered);
        JsonFormat->SetNumberField(TEXT("TriangleCount"), Result.TriangleCount);
        JsonFormat->SetNumberField(TEXT("MaterialCount"), Result.MaterialCount);
        JsonFormat->SetBoolField(TEXT("MaterialSlot0Valid"), Result.bMaterialSlot0Valid);
        JsonFormat->SetBoolField(TEXT("BoundsNonZero"), Result.bBoundsNonZero);
        JsonFormat->SetBoolField(TEXT("CollisionEnabled"), Result.bCollisionEnabled);
        JsonFormat->SetBoolField(TEXT("CollisionData"), Result.bCollisionData);
        JsonFormat->SetBoolField(TEXT("CollisionHit"), Result.bCollisionHit);
        JsonFormat->SetBoolField(TEXT("AttachedToOwnerRoot"), Result.bAttachedToOwnerRoot);
        JsonFormat->SetBoolField(TEXT("FollowedOwnerTransform"), Result.bFollowedOwnerTransform);
        JsonFormats.Add(MakeShared<FJsonValueObject>(JsonFormat));
    }

    TSharedRef<FJsonObject> JsonRoot = MakeShared<FJsonObject>();
    JsonRoot->SetBoolField(TEXT("OverallSuccess"), bOverallSuccess);
    JsonRoot->SetStringField(TEXT("EngineVersion"), FString::Printf(TEXT("%d.%d"), FEngineVersion::Current().GetMajor(),
                                                                    FEngineVersion::Current().GetMinor()));
    JsonRoot->SetArrayField(TEXT("Formats"), JsonFormats);

    FString JsonText;
    const TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonText);
    const bool bSerialized = FJsonSerializer::Serialize(JsonRoot, JsonWriter);
    const FString ResultPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RuntimeAssetImportSmoke.json"));
    const bool bSaved = bSerialized && FFileHelper::SaveStringToFile(
                                           JsonText, *ResultPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    if (!bSaved)
    {
        UE_LOG(LogRuntimeAssetImportSmoke, Error, TEXT("Failed to write smoke result JSON: '%s'."), *ResultPath);
    }
    else
    {
        UE_LOG(LogRuntimeAssetImportSmoke, Display, TEXT("Smoke result JSON: '%s'."), *ResultPath);
    }

    if (GLog != nullptr)
    {
        GLog->FlushThreadedLogs();
        GLog->Flush();
    }
    FPlatformMisc::RequestExit(false);
}
