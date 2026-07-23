// Copyright (c) 2026 metyatech. All rights reserved.

#include "RuntimeAssetImportSmokeGameInstance.h"

#include "AssetConstructor.h"
#include "AssetLoader.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformMisc.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TimerManager.h"
#include "UDynamicMesh.h"

DEFINE_LOG_CATEGORY_STATIC(LogRuntimeAssetImportSmoke, Log, All);

namespace
{
    enum class ESmokeMaterialExpectation : uint8
    {
        Any,
        Color,
        RedTexture,
    };

    struct FSmokeAsset
    {
        const TCHAR *Format;
        const TCHAR *FileName;
        ESmokeMaterialExpectation MaterialExpectation = ESmokeMaterialExpectation::Any;
        bool bMemoryImport = false;
    };

    constexpr FSmokeAsset SmokeAssets[] = {
        {TEXT("FBX"), TEXT("test_triangle.fbx")},
        {TEXT("OBJ"), TEXT("test_triangle.obj"), ESmokeMaterialExpectation::Color},
        {TEXT("DAE"), TEXT("test_triangle.dae")},
        {TEXT("glTF"), TEXT("test_scene.gltf")},
        {TEXT("GLB"), TEXT("test_triangle.glb")},
        {TEXT("ExternalTexture"), TEXT("test_external_texture.obj"), ESmokeMaterialExpectation::RedTexture},
        {TEXT("EmbeddedTextureFile"), TEXT("test_embedded_texture.gltf"), ESmokeMaterialExpectation::RedTexture},
        {TEXT("EmbeddedTextureMemory"), TEXT("test_embedded_texture.gltf"), ESmokeMaterialExpectation::RedTexture,
         true},
        {TEXT("ExternalBuffer"), TEXT("test_external_buffer.gltf")},
    };

    const FLoadedMaterialData *FindSmokeFirstUsedMaterial(const FLoadedMeshData &MeshData)
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

    bool LoadSmokeAssetBytes(const FString &Path, TArray<uint8> &OutBytes)
    {
        OutBytes.Reset();
        return FFileHelper::LoadFileToArray(OutBytes, *Path);
    }

    uint32 CalculateSmokePngCrc32(const uint8 *Data, const int32 ByteCount)
    {
        uint32 Crc = 0xffffffffu;
        for (int32 ByteIndex = 0; ByteIndex < ByteCount; ++ByteIndex)
        {
            Crc ^= Data[ByteIndex];
            for (int32 BitIndex = 0; BitIndex < 8; ++BitIndex)
            {
                const uint32 Mask = 0u - (Crc & 1u);
                Crc = (Crc >> 1u) ^ (0xedb88320u & Mask);
            }
        }
        return ~Crc;
    }

    void WriteSmokeBigEndianUint32(TArray<uint8> &Bytes, const int32 Offset, const uint32 Value)
    {
        Bytes[Offset] = static_cast<uint8>(Value >> 24u);
        Bytes[Offset + 1] = static_cast<uint8>(Value >> 16u);
        Bytes[Offset + 2] = static_cast<uint8>(Value >> 8u);
        Bytes[Offset + 3] = static_cast<uint8>(Value);
    }

    bool MakeOversizedSmokePng(const FString &SmokeAssetDirectory, TArray<uint8> &OutBytes)
    {
        constexpr uint32 OversizedDimension = 16385;
        if (!LoadSmokeAssetBytes(FPaths::Combine(SmokeAssetDirectory, TEXT("textures/test_red.png")), OutBytes) ||
            OutBytes.Num() < 33 || FMemory::Memcmp(OutBytes.GetData() + 12, "IHDR", 4) != 0)
        {
            return false;
        }

        WriteSmokeBigEndianUint32(OutBytes, 16, OversizedDimension);
        WriteSmokeBigEndianUint32(OutBytes, 20, 1);
        WriteSmokeBigEndianUint32(OutBytes, 29, CalculateSmokePngCrc32(OutBytes.GetData() + 12, 17));
        return true;
    }

    bool MakeOversizedEmbeddedSmokeGltf(const FString &SmokeAssetDirectory, const TArray<uint8> &OversizedPngBytes,
                                        TArray<uint8> &OutGltfBytes)
    {
        FString GltfText;
        TArray<uint8> OriginalPngBytes;
        if (!FFileHelper::LoadFileToString(GltfText,
                                           *FPaths::Combine(SmokeAssetDirectory, TEXT("test_embedded_texture.gltf"))) ||
            !LoadSmokeAssetBytes(FPaths::Combine(SmokeAssetDirectory, TEXT("textures/test_red.png")), OriginalPngBytes))
        {
            return false;
        }

        const FString OriginalBase64 = FBase64::Encode(OriginalPngBytes);
        const FString OversizedBase64 = FBase64::Encode(OversizedPngBytes);
        if (GltfText.ReplaceInline(*OriginalBase64, *OversizedBase64, ESearchCase::CaseSensitive) != 1)
        {
            return false;
        }

        FTCHARToUTF8 Utf8Gltf(*GltfText);
        OutGltfBytes.Reset(Utf8Gltf.Length());
        OutGltfBytes.Append(reinterpret_cast<const uint8 *>(Utf8Gltf.Get()), Utf8Gltf.Length());
        return true;
    }

    bool SmokeHasNoImportedExternalTexture(const FLoadedMeshData &MeshData)
    {
        for (const FLoadedMaterialData &Material : MeshData.MaterialList)
        {
            if (Material.ColorStatus == EColorStatus::TextureIsSet || !Material.CompressedTextureData.IsEmpty())
            {
                return false;
            }
        }
        return true;
    }

    bool SmokeHasRejectedCompressedTexture(const FLoadedMeshData &MeshData)
    {
        const FLoadedMaterialData *Material = FindSmokeFirstUsedMaterial(MeshData);
        return Material != nullptr && Material->ColorStatus == EColorStatus::TextureWasSetButError &&
               Material->CompressedTextureData.IsEmpty();
    }

    FLoadedMaterialData *FindMutableSmokeFirstUsedMaterial(FLoadedMeshData &MeshData)
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

    void RunCompressedMetadataSmoke(UWorld *World, UMaterialInterface *ParentMaterial,
                                    const FString &SmokeAssetDirectory, bool &OutConstructionGuardValid,
                                    bool &OutFileTextureDenied, bool &OutMemoryTextureDenied)
    {
        OutConstructionGuardValid = false;
        OutFileTextureDenied = false;
        OutMemoryTextureDenied = false;

        TArray<uint8> OversizedPngBytes;
        TArray<uint8> OversizedGltfBytes;
        if (World == nullptr || ParentMaterial == nullptr ||
            !MakeOversizedSmokePng(SmokeAssetDirectory, OversizedPngBytes) ||
            !MakeOversizedEmbeddedSmokeGltf(SmokeAssetDirectory, OversizedPngBytes, OversizedGltfBytes))
        {
            return;
        }

        const FString OversizedGltfPath =
            FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RuntimeAssetImportOversizedTexture.gltf"));
        if (FFileHelper::SaveArrayToFile(OversizedGltfBytes, *OversizedGltfPath))
        {
            ELoadMeshFromAssetFileResult FileResult = ELoadMeshFromAssetFileResult::Failure;
            const FLoadedMeshData FileData = UAssetLoader::LoadMeshFromAssetFile(OversizedGltfPath, FileResult);
            OutFileTextureDenied =
                FileResult == ELoadMeshFromAssetFileResult::Success && SmokeHasRejectedCompressedTexture(FileData);
        }
        IFileManager::Get().Delete(*OversizedGltfPath, false, true);

        ELoadMeshFromAssetDataResult MemoryResult = ELoadMeshFromAssetDataResult::Failure;
        FLoadedMeshData MemoryData = UAssetLoader::LoadMeshFromAssetData(OversizedGltfBytes, MemoryResult);
        OutMemoryTextureDenied =
            MemoryResult == ELoadMeshFromAssetDataResult::Success && SmokeHasRejectedCompressedTexture(MemoryData);

        FLoadedMaterialData *Material = FindMutableSmokeFirstUsedMaterial(MemoryData);
        AActor *Owner = World->SpawnActor<AActor>();
        if (Material == nullptr || Owner == nullptr)
        {
            if (Owner != nullptr)
            {
                World->DestroyActor(Owner);
            }
            return;
        }

        Material->ColorStatus = EColorStatus::TextureIsSet;
        Material->CompressedTextureData = OversizedPngBytes;
        const int32 InitialComponentCount = Owner->GetComponents().Num();
        UDynamicMeshComponent *UnexpectedComponent =
            UAssetConstructor::ConstructDynamicMeshComponentFromMeshData(MemoryData, ParentMaterial, Owner);
        OutConstructionGuardValid =
            UnexpectedComponent == nullptr && Owner->GetComponents().Num() == InitialComponentCount;
        World->DestroyActor(Owner);
    }

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

    const FString SmokeAssetDirectory = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("SmokeAssets"));
    TArray<uint8> ExpectedRedPngBytes;
    const bool bExpectedRedPngLoaded =
        LoadSmokeAssetBytes(FPaths::Combine(SmokeAssetDirectory, TEXT("textures/test_red.png")), ExpectedRedPngBytes);

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

        const FString AssetPath = FPaths::Combine(SmokeAssetDirectory, SmokeAsset.FileName);
        FLoadedMeshData MeshData;
        bool bLoaded = false;
        if (SmokeAsset.bMemoryImport)
        {
            TArray<uint8> AssetBytes;
            ELoadMeshFromAssetDataResult LoadResult = ELoadMeshFromAssetDataResult::Failure;
            if (LoadSmokeAssetBytes(AssetPath, AssetBytes))
            {
                MeshData = UAssetLoader::LoadMeshFromAssetData(AssetBytes, LoadResult);
            }
            bLoaded = LoadResult == ELoadMeshFromAssetDataResult::Success;
        }
        else
        {
            ELoadMeshFromAssetFileResult LoadResult = ELoadMeshFromAssetFileResult::Failure;
            MeshData = UAssetLoader::LoadMeshFromAssetFile(AssetPath, LoadResult);
            bLoaded = LoadResult == ELoadMeshFromAssetFileResult::Success;
        }

        const FLoadedMaterialData *LoadedMaterial = FindSmokeFirstUsedMaterial(MeshData);
        if (SmokeAsset.MaterialExpectation == ESmokeMaterialExpectation::Color)
        {
            Result.bMaterialScalarValid = false;
            Result.bMaterialVectorValid = false;
            Result.bColorStatusValid =
                LoadedMaterial != nullptr && LoadedMaterial->ColorStatus == EColorStatus::ColorIsSet;
            Result.bImportedColorValid = LoadedMaterial != nullptr &&
                                         FMath::IsNearlyEqual(LoadedMaterial->Color.R, 0.8f) &&
                                         FMath::IsNearlyEqual(LoadedMaterial->Color.G, 0.4f) &&
                                         FMath::IsNearlyEqual(LoadedMaterial->Color.B, 0.2f) &&
                                         FMath::IsNearlyEqual(LoadedMaterial->Color.A, 1.0f);
        }
        else if (SmokeAsset.MaterialExpectation == ESmokeMaterialExpectation::RedTexture)
        {
            Result.bMaterialScalarValid = false;
            Result.bMaterialTextureValid = false;
            Result.bColorStatusValid =
                LoadedMaterial != nullptr && LoadedMaterial->ColorStatus == EColorStatus::TextureIsSet;
            Result.bTextureBytesValid = bExpectedRedPngLoaded && LoadedMaterial != nullptr &&
                                        LoadedMaterial->CompressedTextureData == ExpectedRedPngBytes;
        }

        UDynamicMeshComponent *Component =
            bLoaded ? UAssetConstructor::ConstructDynamicMeshComponentFromMeshData(MeshData, ParentMaterial, Owner)
                    : nullptr;

        Result.Owner = Owner;
        Result.OwnerRoot = OwnerRoot;
        Result.Component = Component;
        Result.bImportSuccess = bLoaded && Component != nullptr;
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
            for (int32 MaterialSlot = 0; MaterialSlot < GeneratedComponent->GetNumMaterials(); ++MaterialSlot)
            {
                UMaterialInstanceDynamic *MaterialInstance =
                    Cast<UMaterialInstanceDynamic>(GeneratedComponent->GetMaterial(MaterialSlot));
                if (MaterialInstance == nullptr)
                {
                    continue;
                }

                if (SmokeAsset.MaterialExpectation == ESmokeMaterialExpectation::Color)
                {
                    Result.bMaterialScalarValid |= FMath::IsNearlyEqual(
                        MaterialInstance->K2_GetScalarParameterValue(TEXT("TextureBlendIntensityForBaseColor")), 0.0f);
                    Result.bMaterialVectorValid |=
                        MaterialInstance->K2_GetVectorParameterValue(TEXT("BaseColor4"))
                            .Equals(FLinearColor(0.8f, 0.4f, 0.2f, 1.0f), KINDA_SMALL_NUMBER);
                }
                else if (SmokeAsset.MaterialExpectation == ESmokeMaterialExpectation::RedTexture)
                {
                    Result.bMaterialScalarValid |= FMath::IsNearlyEqual(
                        MaterialInstance->K2_GetScalarParameterValue(TEXT("TextureBlendIntensityForBaseColor")), 1.0f);
                    Result.bMaterialTextureValid |=
                        MaterialInstance->K2_GetTextureParameterValue(TEXT("BaseColorTexture")) != nullptr;
                }
            }
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

    bool bMemoryExternalAccessDenied = false;
    TArray<uint8> ExternalTextureObjBytes;
    TArray<uint8> ExternalBufferGltfBytes;
    const bool bExternalTextureBytesLoaded = LoadSmokeAssetBytes(
        FPaths::Combine(SmokeAssetDirectory, TEXT("test_external_texture.obj")), ExternalTextureObjBytes);
    const bool bExternalBufferBytesLoaded = LoadSmokeAssetBytes(
        FPaths::Combine(SmokeAssetDirectory, TEXT("test_external_buffer.gltf")), ExternalBufferGltfBytes);
    if (bExternalTextureBytesLoaded && bExternalBufferBytesLoaded)
    {
        ELoadMeshFromAssetDataResult ExternalTextureMemoryResult = ELoadMeshFromAssetDataResult::Failure;
        const FLoadedMeshData ExternalTextureMemoryData =
            UAssetLoader::LoadMeshFromAssetData(ExternalTextureObjBytes, ExternalTextureMemoryResult);
        ELoadMeshFromAssetDataResult ExternalBufferMemoryResult = ELoadMeshFromAssetDataResult::Success;
        const FLoadedMeshData ExternalBufferMemoryData =
            UAssetLoader::LoadMeshFromAssetData(ExternalBufferGltfBytes, ExternalBufferMemoryResult);
        bMemoryExternalAccessDenied = ExternalTextureMemoryResult == ELoadMeshFromAssetDataResult::Success &&
                                      SmokeHasNoImportedExternalTexture(ExternalTextureMemoryData) &&
                                      ExternalBufferMemoryResult == ELoadMeshFromAssetDataResult::Failure &&
                                      ExternalBufferMemoryData.NodeList.IsEmpty();
    }
    for (FRuntimeAssetImportSmokeFormatResult &Result : FormatResults)
    {
        Result.bMemoryExternalAccessDenied = bMemoryExternalAccessDenied;
    }

    RunCompressedMetadataSmoke(World, ParentMaterial, SmokeAssetDirectory, bCompressedMetadataGuardValid,
                               bOversizedFileTextureDenied, bOversizedMemoryTextureDenied);

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
    bool bOverallSuccess = FormatResults.Num() == UE_ARRAY_COUNT(SmokeAssets) && bCompressedMetadataGuardValid &&
                           bOversizedFileTextureDenied && bOversizedMemoryTextureDenied;
    TArray<TSharedPtr<FJsonValue>> JsonFormats;
    for (const FRuntimeAssetImportSmokeFormatResult &Result : FormatResults)
    {
        const bool bFormatSuccess =
            Result.bImportSuccess && Result.bComponentRegistered && Result.TriangleCount > 0 &&
            Result.MaterialCount > 0 && Result.bMaterialSlot0Valid && Result.bBoundsNonZero &&
            Result.bCollisionEnabled && Result.bCollisionData && Result.bCollisionHit && Result.bAttachedToOwnerRoot &&
            Result.bFollowedOwnerTransform && Result.bColorStatusValid && Result.bImportedColorValid &&
            Result.bTextureBytesValid && Result.bMaterialScalarValid && Result.bMaterialVectorValid &&
            Result.bMaterialTextureValid && Result.bMemoryExternalAccessDenied;
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
        JsonFormat->SetBoolField(TEXT("ColorStatusValid"), Result.bColorStatusValid);
        JsonFormat->SetBoolField(TEXT("ImportedColorValid"), Result.bImportedColorValid);
        JsonFormat->SetBoolField(TEXT("TextureBytesValid"), Result.bTextureBytesValid);
        JsonFormat->SetBoolField(TEXT("MaterialScalarValid"), Result.bMaterialScalarValid);
        JsonFormat->SetBoolField(TEXT("MaterialVectorValid"), Result.bMaterialVectorValid);
        JsonFormat->SetBoolField(TEXT("MaterialTextureValid"), Result.bMaterialTextureValid);
        JsonFormat->SetBoolField(TEXT("MemoryExternalAccessDenied"), Result.bMemoryExternalAccessDenied);
        JsonFormats.Add(MakeShared<FJsonValueObject>(JsonFormat));
    }

    TSharedRef<FJsonObject> JsonRoot = MakeShared<FJsonObject>();
    JsonRoot->SetBoolField(TEXT("OverallSuccess"), bOverallSuccess);
    JsonRoot->SetBoolField(TEXT("CompressedMetadataGuardValid"), bCompressedMetadataGuardValid);
    JsonRoot->SetBoolField(TEXT("OversizedFileTextureDenied"), bOversizedFileTextureDenied);
    JsonRoot->SetBoolField(TEXT("OversizedMemoryTextureDenied"), bOversizedMemoryTextureDenied);
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
