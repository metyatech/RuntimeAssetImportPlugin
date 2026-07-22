// Copyright (c) 2026 metyatech. All rights reserved.

#pragma once

#include "Components/DynamicMeshComponent.h"
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LoadedMeshData.h"
#include "ProceduralMeshComponent.h"

#include "AssetConstructor.generated.h"

/** Result of ConstructProceduralMeshComponentFromAssetFile. */
UENUM(BlueprintType)
enum class EConstructProceduralMeshComponentFromAssetFileResult : uint8
{
    Success,
    Failure
};

/** Result of ConstructDynamicMeshComponentFromAssetFile. */
UENUM(BlueprintType)
enum class EConstructDynamicMeshComponentFromAssetFileResult : uint8
{
    Success,
    Failure
};

/** Blueprint function library for constructing runtime mesh components. */
UCLASS()
class RUNTIMEASSETIMPORT_API UAssetConstructor : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Construct a hierarchy of Procedural Mesh Components from loaded mesh data.
     * DynamicMeshComponent is recommended. ProceduralMeshComponent can cause
     * movement or network issues when independently created by multiplayer peers.
     */
    UFUNCTION(BlueprintCallable, Category = "Runtime Asset Import|Procedural Mesh")
    static UPARAM(DisplayName = "Root Procedural Mesh Component") UProceduralMeshComponent
        *ConstructProceduralMeshComponentFromMeshData(const FLoadedMeshData &MeshData,
                                                      UMaterialInterface *ParentMaterialInterface, AActor *Owner,
                                                      bool ShouldRegisterComponentToOwner = true);

    /** Construct a hierarchy of Dynamic Mesh Components from loaded mesh data. */
    UFUNCTION(BlueprintCallable, Category = "Runtime Asset Import|Dynamic Mesh")
    static UPARAM(DisplayName = "Root Dynamic Mesh Component")
        UDynamicMeshComponent *ConstructDynamicMeshComponentFromMeshData(const FLoadedMeshData &MeshData,
                                                                         UMaterialInterface *ParentMaterialInterface,
                                                                         AActor *Owner,
                                                                         bool ShouldRegisterComponentToOwner = true);

    /**
     * Synchronously load a file and construct a Procedural Mesh Component hierarchy.
     */
    UFUNCTION(BlueprintCallable, Category = "Runtime Asset Import|Procedural Mesh",
              meta = (ExpandEnumAsExecs = "ConstructProceduralMeshComponentFromAssetFileResult"))
    static UPARAM(DisplayName = "Root Procedural Mesh Component")
        UProceduralMeshComponent *ConstructProceduralMeshComponentFromAssetFile(
            const FString &FilePath, UMaterialInterface *ParentMaterialInterface, AActor *Owner,
            EConstructProceduralMeshComponentFromAssetFileResult &ConstructProceduralMeshComponentFromAssetFileResult,
            bool ShouldRegisterComponentToOwner = true);

    /** Synchronously load a file and construct a Dynamic Mesh Component hierarchy. */
    UFUNCTION(BlueprintCallable, Category = "Runtime Asset Import|Dynamic Mesh",
              meta = (ExpandEnumAsExecs = "ConstructDynamicMeshComponentFromAssetFileResult"))
    static UPARAM(DisplayName = "Root Dynamic Mesh Component")
        UDynamicMeshComponent *ConstructDynamicMeshComponentFromAssetFile(
            const FString &FilePath, UMaterialInterface *ParentMaterialInterface, AActor *Owner,
            EConstructDynamicMeshComponentFromAssetFileResult &ConstructDynamicMeshComponentFromAssetFileResult,
            bool ShouldRegisterComponentToOwner = true);
};
