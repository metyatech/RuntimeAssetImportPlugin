// Copyright (c) 2026 metyatech. All rights reserved.

#include "AssetConstructorHelpers.h"

#include "HasFeatureFix.h"
#include "ImageUtils.h"
#include "LogAssetConstructor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

TArray<UMaterialInstanceDynamic*> GenerateMaterialInstances(
    UObject& Owner, const TArray<FLoadedMaterialData>& MaterialDataList,
    UMaterialInterface& ParentMaterialInterface) {
	TArray<UMaterialInstanceDynamic*> MaterialInstances;
	const auto&                       NumMaterials = MaterialDataList.Num();
	MaterialInstances.AddUninitialized(NumMaterials);

	if (0 == NumMaterials) {
		UE_LOG(LogAssetConstructor, Display, TEXT("There is no Materials."));
	}
	for (auto i = decltype(NumMaterials){0}; i < NumMaterials; ++i) {
		// create material
		UMaterialInstanceDynamic* MaterialInstance =
		    UMaterialInstanceDynamic::Create(&ParentMaterialInterface, &Owner);

		const auto& MaterialData = MaterialDataList[i];

		switch (MaterialData.ColorStatus) {
		case EColorStatus::None:
			// if nothing is set
			UE_LOG(LogAssetConstructor, Error,
			       TEXT("color status is not set in index %d"), i);

			break;
		case EColorStatus::ColorIsSet: {
			// log that no texture is found
			UE_LOG(LogAssetConstructor, Log,
			       TEXT("No texture is found for material in index %d"), i);

			VerifyMaterialParameter(ParentMaterialInterface,
			                        EMaterialParameterType::Scalar,
			                        "TextureBlendIntensityForBaseColor");
			VerifyMaterialParameter(ParentMaterialInterface,
			                        EMaterialParameterType::Vector, "BaseColor4");

			// set to use color
			MaterialInstance->SetScalarParameterValue(
			    "TextureBlendIntensityForBaseColor", 0.0f);

			// get color
			const auto& Color = MaterialData.Color;

			// set color
			MaterialInstance->SetVectorParameterValue("BaseColor4", Color);

			break;
		}
		case EColorStatus::TextureIsSet: {
			// get compressed texture data
			const auto& CompressedTextureData = MaterialData.CompressedTextureData;

			// get texture
			UTexture2D* Texture0 =
			    FImageUtils::ImportBufferAsTexture2D(CompressedTextureData);

			VerifyMaterialParameter(ParentMaterialInterface,
			                        EMaterialParameterType::Scalar,
			                        "TextureBlendIntensityForBaseColor");
			VerifyMaterialParameter(ParentMaterialInterface,
			                        EMaterialParameterType::Texture,
			                        "BaseColorTexture");

			// set to use texture
			MaterialInstance->SetScalarParameterValue(
			    "TextureBlendIntensityForBaseColor", 1.0f);

			// set texture
			MaterialInstance->SetTextureParameterValue("BaseColorTexture", Texture0);

			break;
		}
		case EColorStatus::TextureWasSetButError:
			// if the texture is in the status of failure
			UE_LOG(LogAssetConstructor, Warning,
			       TEXT("The original data had a texture set, but it failed to load, "
			            "so skip setting the texture in index %d"),
			       i);

			break;
		default:
			verifyf(false, TEXT("Bug. Color status is not None, ColorIsSet, "
			                    "TextureIsSet, or TextureWasSetButError."));
			break;
		}

		MaterialInstances[i] = MaterialInstance;
	}

	return MaterialInstances;
}

void VerifyMaterialParameter(
    const UMaterialInterface&     MaterialInterface,
    const EMaterialParameterType& MaterialParameterType,
    const FName&                  ParameterName) {
	FMemoryImageMaterialParameterInfo MemoryImageMaterialParameterInfo(
	    ParameterName);
	FMaterialParameterMetadata MaterialParameterMetadata;

	// check if parameter exists
	const auto& ParameterExists = MaterialInterface.GetParameterDefaultValue(
	    MaterialParameterType, MemoryImageMaterialParameterInfo,
	    MaterialParameterMetadata);

	// verify
	verifyf(ParameterExists, TEXT("Material %s doesn't have %s parameter."),
	        *MaterialInterface.GetFName().ToString(), *ParameterName.ToString());
}
