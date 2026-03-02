// Copyright (c) 2026 metyatech. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "LoadedMaterialData.h"
#include "LoadedMeshNode.h"

#include "LoadedMeshData.generated.h"

/**
 * Data of the loaded mesh.
 * A tree-like nodes represent a single data.
 */
USTRUCT(BlueprintType)
struct RUNTIMEASSETIMPORT_API FLoadedMeshData {
	GENERATED_BODY()

	// List of all nodes.
	// The parent-child relationship is indicated by
	// FLoadedMeshNode::ParentNodeIndex.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TArray<FLoadedMeshNode> NodeList;

	// List of materials. Which mesh (or more precisely, mesh section) uses which
	// material is indicated by FLoadedMeshSectionData::MaterialIndex.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TArray<FLoadedMaterialData> MaterialList;
};
