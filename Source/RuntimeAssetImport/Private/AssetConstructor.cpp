// Copyright (c) 2026 metyatech. All rights reserved.

#include "AssetConstructor.h"

#include "AssetConstructorHelpers.h"
#include "AssetLoader.h"
#include "LogAssetConstructor.h"

namespace
{
    bool ValidateConstructionArguments(const TCHAR *FunctionName, UMaterialInterface *ParentMaterialInterface,
                                       AActor *Owner)
    {
        if (Owner == nullptr)
        {
            UE_LOG(LogAssetConstructor, Error, TEXT("%s failed: Owner is null."), FunctionName);
            return false;
        }
        if (ParentMaterialInterface == nullptr)
        {
            UE_LOG(LogAssetConstructor, Error, TEXT("%s failed: ParentMaterialInterface is null."), FunctionName);
            return false;
        }
        return true;
    }
} // namespace

UProceduralMeshComponent *UAssetConstructor::ConstructProceduralMeshComponentFromMeshData(
    const FLoadedMeshData &MeshData, UMaterialInterface *ParentMaterialInterface, AActor *const Owner,
    const bool ShouldRegisterComponentToOwner)
{
    if (!ValidateConstructionArguments(TEXT("ConstructProceduralMeshComponentFromMeshData"), ParentMaterialInterface,
                                       Owner))
    {
        return nullptr;
    }
    return ConstructMeshComponentFromMeshData<UProceduralMeshComponent>(MeshData, ParentMaterialInterface, Owner,
                                                                        ShouldRegisterComponentToOwner);
}

UDynamicMeshComponent *UAssetConstructor::ConstructDynamicMeshComponentFromMeshData(
    const FLoadedMeshData &MeshData, UMaterialInterface *ParentMaterialInterface, AActor *const Owner,
    const bool ShouldRegisterComponentToOwner)
{
    if (!ValidateConstructionArguments(TEXT("ConstructDynamicMeshComponentFromMeshData"), ParentMaterialInterface,
                                       Owner))
    {
        return nullptr;
    }
    return ConstructMeshComponentFromMeshData<UDynamicMeshComponent>(MeshData, ParentMaterialInterface, Owner,
                                                                     ShouldRegisterComponentToOwner);
}

UProceduralMeshComponent *UAssetConstructor::ConstructProceduralMeshComponentFromAssetFile(
    const FString &FilePath, UMaterialInterface *const ParentMaterialInterface, AActor *const Owner,
    EConstructProceduralMeshComponentFromAssetFileResult &ConstructProceduralMeshComponentFromAssetFileResult,
    const bool ShouldRegisterComponentToOwner)
{
    ConstructProceduralMeshComponentFromAssetFileResult = EConstructProceduralMeshComponentFromAssetFileResult::Failure;
    if (!ValidateConstructionArguments(TEXT("ConstructProceduralMeshComponentFromAssetFile"), ParentMaterialInterface,
                                       Owner))
    {
        return nullptr;
    }
    if (FilePath.IsEmpty())
    {
        UE_LOG(LogAssetConstructor, Error,
               TEXT("ConstructProceduralMeshComponentFromAssetFile failed: FilePath is empty."));
        return nullptr;
    }

    ELoadMeshFromAssetFileResult LoadResult = ELoadMeshFromAssetFileResult::Failure;
    const FLoadedMeshData LoadedMeshData = UAssetLoader::LoadMeshFromAssetFile(FilePath, LoadResult);
    if (LoadResult != ELoadMeshFromAssetFileResult::Success)
    {
        return nullptr;
    }

    UProceduralMeshComponent *Component = ConstructProceduralMeshComponentFromMeshData(
        LoadedMeshData, ParentMaterialInterface, Owner, ShouldRegisterComponentToOwner);
    if (Component == nullptr)
    {
        return nullptr;
    }

    ConstructProceduralMeshComponentFromAssetFileResult = EConstructProceduralMeshComponentFromAssetFileResult::Success;
    return Component;
}

UDynamicMeshComponent *UAssetConstructor::ConstructDynamicMeshComponentFromAssetFile(
    const FString &FilePath, UMaterialInterface *const ParentMaterialInterface, AActor *const Owner,
    EConstructDynamicMeshComponentFromAssetFileResult &ConstructDynamicMeshComponentFromAssetFileResult,
    const bool ShouldRegisterComponentToOwner)
{
    ConstructDynamicMeshComponentFromAssetFileResult = EConstructDynamicMeshComponentFromAssetFileResult::Failure;
    if (!ValidateConstructionArguments(TEXT("ConstructDynamicMeshComponentFromAssetFile"), ParentMaterialInterface,
                                       Owner))
    {
        return nullptr;
    }
    if (FilePath.IsEmpty())
    {
        UE_LOG(LogAssetConstructor, Error,
               TEXT("ConstructDynamicMeshComponentFromAssetFile failed: FilePath is empty."));
        return nullptr;
    }

    ELoadMeshFromAssetFileResult LoadResult = ELoadMeshFromAssetFileResult::Failure;
    const FLoadedMeshData LoadedMeshData = UAssetLoader::LoadMeshFromAssetFile(FilePath, LoadResult);
    if (LoadResult != ELoadMeshFromAssetFileResult::Success)
    {
        return nullptr;
    }

    UDynamicMeshComponent *Component = ConstructDynamicMeshComponentFromMeshData(
        LoadedMeshData, ParentMaterialInterface, Owner, ShouldRegisterComponentToOwner);
    if (Component == nullptr)
    {
        return nullptr;
    }

    ConstructDynamicMeshComponentFromAssetFileResult = EConstructDynamicMeshComponentFromAssetFileResult::Success;
    return Component;
}
