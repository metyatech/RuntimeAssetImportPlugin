// Copyright (c) 2026 metyatech. All rights reserved.

#include "AssetConstructor.h"

#include "HasFeatureFix.h"
#include "AssetConstructorHelpers.h"
#include "AssetLoader.h"
#include "CreateMeshFromMeshDataOnProceduralMeshComponentLatentAction.h"
#include "Engine/Engine.h"
#include "Engine/LatentActionManager.h"

void UAssetConstructor::CreateMeshFromMeshDataOnProceduralMeshComponent(
    const UObject* WorldContextObject, FLatentActionInfo LatentActionInfo,
    const FLoadedMeshData&    MeshData,
    UMaterialInterface*       ParentMaterialInterface,
    UProceduralMeshComponent* TargetProceduralMeshComponent) {
	// check to WorldContextObject is properly set
	check(WorldContextObject != nullptr);

	// check to ParentMaterialInterface is properly set
	check(ParentMaterialInterface != nullptr);

	// check to TargetProceduralMeshComponent is properly set
	check(TargetProceduralMeshComponent != nullptr);

	const auto World = GEngine->GetWorldFromContextObject(
	    WorldContextObject, EGetWorldErrorMode::Assert);
	check(World != nullptr);

	FLatentActionManager& LatentActionManager = World->GetLatentActionManager();

	LatentActionManager.AddNewAction(
	    LatentActionInfo.CallbackTarget, LatentActionInfo.UUID,
	    new FCreateMeshFromMeshDataOnProceduralMeshComponentLatentAction(
	        LatentActionInfo, MeshData, *ParentMaterialInterface,
	        *TargetProceduralMeshComponent));
}

UProceduralMeshComponent*
    UAssetConstructor::ConstructProceduralMeshComponentFromMeshData(
        const FLoadedMeshData& MeshData,
        UMaterialInterface* ParentMaterialInterface, AActor* const Owner,
        const bool ShouldRegisterComponentToOwner) {
	// check to ParentMaterialInterface is properly set
	check(ParentMaterialInterface != nullptr);

	// check to Owner is properly set
	check(Owner != nullptr);

	return ConstructMeshComponentFromMeshData<UProceduralMeshComponent>(
	    MeshData, ParentMaterialInterface, Owner, ShouldRegisterComponentToOwner);
}

UStaticMeshComponent*
    UAssetConstructor::ConstructStaticMeshComponentFromMeshData(
        const FLoadedMeshData& MeshData,
        UMaterialInterface* ParentMaterialInterface, AActor* const Owner,
        const bool ShouldRegisterComponentToOwner) {
	// check to ParentMaterialInterface is properly set
	check(ParentMaterialInterface != nullptr);

	// check to Owner is properly set
	check(Owner != nullptr);

	return ConstructMeshComponentFromMeshData<UStaticMeshComponent>(
	    MeshData, ParentMaterialInterface, Owner, ShouldRegisterComponentToOwner);
}

UDynamicMeshComponent*
    UAssetConstructor::ConstructDynamicMeshComponentFromMeshData(
        const FLoadedMeshData& MeshData,
        UMaterialInterface* ParentMaterialInterface, AActor* const Owner,
        const bool ShouldRegisterComponentToOwner) {
	// check to ParentMaterialInterface is properly set
	check(ParentMaterialInterface != nullptr);

	// check to Owner is properly set
	check(Owner != nullptr);

	return ConstructMeshComponentFromMeshData<UDynamicMeshComponent>(
	    MeshData, ParentMaterialInterface, Owner, ShouldRegisterComponentToOwner);
}

UProceduralMeshComponent*
    UAssetConstructor::ConstructProceduralMeshComponentFromAssetFile(
        const FString&            FilePath,
        UMaterialInterface* const ParentMaterialInterface, AActor* const Owner,
        EConstructProceduralMeshComponentFromAssetFileResult&
                   ConstructProceduralMeshComponentFromAssetFileResult,
        const bool ShouldRegisterComponentToOwner) {
	// check to ParentMaterialInterface is properly set
	check(ParentMaterialInterface != nullptr);

	// check to Owner is properly set
	check(Owner != nullptr);

	// load mesh from asset file(path: FilePath)
	ELoadMeshFromAssetFileResult LoadMeshFromAssetFileResult;
	const auto& LoadedMeshData = UAssetLoader::LoadMeshFromAssetFile(
	    FilePath, LoadMeshFromAssetFileResult);

	// check load result
	if (ELoadMeshFromAssetFileResult::Failure == LoadMeshFromAssetFileResult) {
		ConstructProceduralMeshComponentFromAssetFileResult =
		    EConstructProceduralMeshComponentFromAssetFileResult::Failure;
		return nullptr;
	}

	// assume the result is success
	ConstructProceduralMeshComponentFromAssetFileResult =
	    EConstructProceduralMeshComponentFromAssetFileResult::Success;

	// construct from loaded mesh data
	return ConstructProceduralMeshComponentFromMeshData(
	    LoadedMeshData, ParentMaterialInterface, Owner,
	    ShouldRegisterComponentToOwner);
}

UStaticMeshComponent*
    UAssetConstructor::ConstructStaticMeshComponentFromAssetFile(
        const FString& FilePath, UMaterialInterface* ParentMaterialInterface,
        AActor* Owner,
        EConstructStaticMeshComponentFromAssetFileResult&
             ConstructStaticMeshComponentFromAssetFileResult,
        bool ShouldRegisterComponentToOwner) {
	// check to ParentMaterialInterface is properly set
	check(ParentMaterialInterface != nullptr);

	// check to Owner is properly set
	check(Owner != nullptr);

	// load mesh from asset file(path: FilePath)
	ELoadMeshFromAssetFileResult LoadMeshFromAssetFileResult;
	const auto& LoadedMeshData = UAssetLoader::LoadMeshFromAssetFile(
	    FilePath, LoadMeshFromAssetFileResult);

	// check load result
	if (ELoadMeshFromAssetFileResult::Failure == LoadMeshFromAssetFileResult) {
		ConstructStaticMeshComponentFromAssetFileResult =
		    EConstructStaticMeshComponentFromAssetFileResult::Failure;
		return nullptr;
	}

	// assume the result is success
	ConstructStaticMeshComponentFromAssetFileResult =
	    EConstructStaticMeshComponentFromAssetFileResult::Success;

	// construct from loaded mesh data
	return ConstructStaticMeshComponentFromMeshData(
	    LoadedMeshData, ParentMaterialInterface, Owner,
	    ShouldRegisterComponentToOwner);
}

UDynamicMeshComponent*
    UAssetConstructor::ConstructDynamicMeshComponentFromAssetFile(
        const FString& FilePath, UMaterialInterface* ParentMaterialInterface,
        AActor* Owner,
        EConstructDynamicMeshComponentFromAssetFileResult&
             ConstructDynamicMeshComponentFromAssetFileResult,
        bool ShouldRegisterComponentToOwner) {
	// check to ParentMaterialInterface is properly set
	check(ParentMaterialInterface != nullptr);

	// check to Owner is properly set
	check(Owner != nullptr);

	// load mesh from asset file(path: FilePath)
	ELoadMeshFromAssetFileResult LoadMeshFromAssetFileResult;
	const auto& LoadedMeshData = UAssetLoader::LoadMeshFromAssetFile(
	    FilePath, LoadMeshFromAssetFileResult);

	// check load result
	if (ELoadMeshFromAssetFileResult::Failure == LoadMeshFromAssetFileResult) {
		ConstructDynamicMeshComponentFromAssetFileResult =
		    EConstructDynamicMeshComponentFromAssetFileResult::Failure;
		return nullptr;
	}

	// assume the result is success
	ConstructDynamicMeshComponentFromAssetFileResult =
	    EConstructDynamicMeshComponentFromAssetFileResult::Success;

	// construct from loaded mesh data
	return ConstructDynamicMeshComponentFromMeshData(
	    LoadedMeshData, ParentMaterialInterface, Owner,
	    ShouldRegisterComponentToOwner);
}
