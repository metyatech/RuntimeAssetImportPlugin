// Copyright (c) 2026 metyatech. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "LoadedMeshSectionData.h"

#include "LoadedMeshNode.generated.h"

/**
 * A class that represents a grouping of multiple mesh sections, called a Node.
 * One loaded mesh is made up of tree-like nodes. Each node has a name, a
 * parent, and a Transform relative to the parent. Each node also has multiple
 * mesh sections.
 */
USTRUCT(BlueprintType)
struct RUNTIMEASSETIMPORT_API FLoadedMeshNode {
	GENERATED_BODY()

	// Name of this node
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FString Name;

	// Transform relative to the parent node indicated by ParentNodeIndex
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FTransform RelativeTransform;

	// Actual mesh section data. There may be more than one.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TArray<FLoadedMeshSectionData> Sections;

	// All nodes are stored in FLoadedMeshData::NodeList as a sequence list.
	// The index of the parent node in that array.
	// Min indicates that there is no parent node (i.e., the only root node).
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	int ParentNodeIndex = std::numeric_limits<int>::min();
};
