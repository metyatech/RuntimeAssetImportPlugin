// Copyright (c) 2026 metyatech. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

#include "RuntimeAssetImportSmokeGameInstance.generated.h"

class AActor;
class UDynamicMeshComponent;
class USceneComponent;

struct FRuntimeAssetImportSmokeFormatResult
{
    FString Format;
    bool bImportSuccess = false;
    bool bComponentRegistered = false;
    int32 TriangleCount = 0;
    int32 MaterialCount = 0;
    bool bMaterialSlot0Valid = false;
    bool bBoundsNonZero = false;
    bool bCollisionEnabled = false;
    bool bCollisionData = false;
    bool bCollisionHit = false;
    bool bAttachedToOwnerRoot = false;
    bool bFollowedOwnerTransform = false;
    FTransform InitialComponentWorldTransform = FTransform::Identity;
    TWeakObjectPtr<AActor> Owner;
    TWeakObjectPtr<USceneComponent> OwnerRoot;
    TWeakObjectPtr<UDynamicMeshComponent> Component;
    TWeakObjectPtr<UDynamicMeshComponent> GeometryComponent;
};

UCLASS()
class RUNTIMEASSETIMPORTSAMPLE_API URuntimeAssetImportSmokeGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    virtual void Init() override;
    virtual void Shutdown() override;

private:
    void HandlePostWorldInitialization(UWorld *World, const UWorld::InitializationValues InitializationValues);
    void RunSmoke(UWorld *World);
    void FinalizeSmoke(UWorld *World);
    void WriteResultsAndExit();

    FDelegateHandle WorldInitializationHandle;
    TArray<FRuntimeAssetImportSmokeFormatResult> FormatResults;
    bool bSmokeStarted = false;
};
