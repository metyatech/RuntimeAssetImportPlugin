// Copyright (c) 2026 metyatech. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "LoadedMeshData.h"
#include "ProceduralMeshComponent.h"

/**
 * Internal class for
 * AssetConstructor::CreateMeshFromMeshDataOnProceduralMeshComponent
 */
class FCreateMeshFromMeshDataOnProceduralMeshComponentLatentAction
    : public FPendingLatentAction {
public:
	FCreateMeshFromMeshDataOnProceduralMeshComponentLatentAction(
	    const FLatentActionInfo& InLatentInfo, const FLoadedMeshData& InMeshData,
	    UMaterialInterface&       InOutParentMaterialInterface,
	    UProceduralMeshComponent& InOutTargetProceduralMeshComponent);

public:
	// this function is called every frame to check if it has finished.
	virtual void UpdateOperation(FLatentResponse& Response) override;

	/* internal functions */
private:
	// finish latent action
	void Finish();

	/* internal fields */
private:
	bool IsRunning = false;

	FName          ExecutionFunction;
	int32          OutputLink;
	FWeakObjectPtr CallbackTarget;
};
