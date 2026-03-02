// Copyright (c) 2026 metyatech. All rights reserved.

#pragma once

#include "CoreMinimal.h"

#include "LoadedMaterialData.generated.h"

/**
 * color status of diffuse color
 */
UENUM()
enum class EColorStatus {
	// nothing is set
	None,

	// color is set but not texture
	ColorIsSet,

	// texture is set but not color
	TextureIsSet,

	// texture was set but failed to load
	TextureWasSetButError
};

/**
 * Data of the loaded material.
 */
USTRUCT(BlueprintType)
struct RUNTIMEASSETIMPORT_API FLoadedMaterialData {
	GENERATED_BODY()

	// Material diffuse color, available only if ColorStatus is ColorIsSet.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FLinearColor Color = FLinearColor(ForceInit);

	// Texture data compressed into some format, available only if ColorStatus is
	// TextureIsSet.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TArray<uint8> CompressedTextureData;

	// Whether there exists texture or not.
	// If the status is ColorIsSet, the Color property is set
	// (CompressedTextureData is not available);
	// if the status is TextureIsSet, the texture data is stored in
	// CompressedTextureData. (Color property is not available);
	// if the status is TextureWasSetButError, it means that the texture was set
	// but its data could not be loaded, and both Color and CompressedTextureData
	// properties are not available.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	EColorStatus ColorStatus = EColorStatus::None;
};
