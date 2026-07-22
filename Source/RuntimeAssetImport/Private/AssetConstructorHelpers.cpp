// Copyright (c) 2026 metyatech. All rights reserved.

#include "AssetConstructorHelpers.h"

#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Materials/MaterialInterface.h"

namespace
{
    bool HasExpectedArraySize(const int32 ArraySize, const int32 VertexCount)
    {
        return ArraySize == 0 || ArraySize == VertexCount;
    }

    bool HasMaterialParameter(const UMaterialInterface &MaterialInterface, const EMaterialParameterType ParameterType,
                              const FName ParameterName)
    {
        FMemoryImageMaterialParameterInfo ParameterInfo(ParameterName);
        FMaterialParameterMetadata Metadata;
        return MaterialInterface.GetParameterDefaultValue(ParameterType, ParameterInfo, Metadata);
    }

    bool RequireMaterialParameter(const UMaterialInterface &MaterialInterface, const int32 MaterialIndex,
                                  const EMaterialParameterType ParameterType, const FName ParameterName)
    {
        if (HasMaterialParameter(MaterialInterface, ParameterType, ParameterName))
        {
            return true;
        }

        UE_LOG(LogAssetConstructor, Error, TEXT("Material index %d: Parent Material '%s' is missing parameter '%s'."),
               MaterialIndex, *MaterialInterface.GetName(), *ParameterName.ToString());
        return false;
    }
} // namespace

bool ValidateMeshDataForConstruction(const FLoadedMeshData &MeshData, FString &OutErrorMessage)
{
    OutErrorMessage.Reset();
    if (MeshData.NodeList.IsEmpty())
    {
        OutErrorMessage = TEXT("NodeList is empty.");
        return false;
    }
    if (MeshData.NodeList[0].ParentNodeIndex != INDEX_NONE)
    {
        OutErrorMessage = FString::Printf(TEXT("Node index 0 field ParentNodeIndex must be INDEX_NONE, got %d."),
                                          MeshData.NodeList[0].ParentNodeIndex);
        return false;
    }

    for (int32 NodeIndex = 0; NodeIndex < MeshData.NodeList.Num(); ++NodeIndex)
    {
        const FLoadedMeshNode &Node = MeshData.NodeList[NodeIndex];
        if (NodeIndex > 0 && (Node.ParentNodeIndex < 0 || Node.ParentNodeIndex >= NodeIndex))
        {
            OutErrorMessage =
                FString::Printf(TEXT("Node index %d field ParentNodeIndex must be between 0 and %d, got %d."),
                                NodeIndex, NodeIndex - 1, Node.ParentNodeIndex);
            return false;
        }
        if (Node.RelativeTransform.ContainsNaN())
        {
            OutErrorMessage =
                FString::Printf(TEXT("Node index %d field RelativeTransform contains NaN or Inf."), NodeIndex);
            return false;
        }

        for (int32 SectionIndex = 0; SectionIndex < Node.Sections.Num(); ++SectionIndex)
        {
            const FLoadedMeshSectionData &Section = Node.Sections[SectionIndex];
            const int32 VertexCount = Section.Vertices.Num();
            if (VertexCount < 3)
            {
                OutErrorMessage = FString::Printf(
                    TEXT("Node index %d, section index %d field Vertices requires at least 3 values, got %d."),
                    NodeIndex, SectionIndex, VertexCount);
                return false;
            }
            if (Section.Triangles.IsEmpty())
            {
                OutErrorMessage = FString::Printf(TEXT("Node index %d, section index %d field Triangles is empty."),
                                                  NodeIndex, SectionIndex);
                return false;
            }
            if (Section.Triangles.Num() % 3 != 0)
            {
                OutErrorMessage = FString::Printf(
                    TEXT("Node index %d, section index %d field Triangles count must be divisible by 3, got %d."),
                    NodeIndex, SectionIndex, Section.Triangles.Num());
                return false;
            }
            for (int32 TriangleIndex = 0; TriangleIndex < Section.Triangles.Num(); ++TriangleIndex)
            {
                const int32 VertexIndex = Section.Triangles[TriangleIndex];
                if (VertexIndex < 0 || VertexIndex >= VertexCount)
                {
                    OutErrorMessage = FString::Printf(
                        TEXT("Node index %d, section index %d field Triangles[%d]=%d is outside vertex count %d."),
                        NodeIndex, SectionIndex, TriangleIndex, VertexIndex, VertexCount);
                    return false;
                }
            }

            const struct
            {
                const TCHAR *Name;
                int32 Size;
            } OptionalArrays[] = {
                {TEXT("Normals"), Section.Normals.Num()},
                {TEXT("UV0Channel"), Section.UV0Channel.Num()},
                {TEXT("VertexColors0"), Section.VertexColors0.Num()},
                {TEXT("Tangents"), Section.Tangents.Num()},
            };
            for (const auto &OptionalArray : OptionalArrays)
            {
                if (!HasExpectedArraySize(OptionalArray.Size, VertexCount))
                {
                    OutErrorMessage =
                        FString::Printf(TEXT("Node index %d, section index %d field %s count must be 0 or %d, got %d."),
                                        NodeIndex, SectionIndex, OptionalArray.Name, VertexCount, OptionalArray.Size);
                    return false;
                }
            }

            if (!MeshData.MaterialList.IsValidIndex(Section.MaterialIndex))
            {
                OutErrorMessage = FString::Printf(
                    TEXT("Node index %d, section index %d field MaterialIndex=%d is outside material count %d."),
                    NodeIndex, SectionIndex, Section.MaterialIndex, MeshData.MaterialList.Num());
                return false;
            }
        }
    }

    for (int32 MaterialIndex = 0; MaterialIndex < MeshData.MaterialList.Num(); ++MaterialIndex)
    {
        const FLoadedMaterialData &MaterialData = MeshData.MaterialList[MaterialIndex];
        if (MaterialData.ColorStatus == EColorStatus::TextureIsSet && MaterialData.CompressedTextureData.IsEmpty())
        {
            OutErrorMessage = FString::Printf(
                TEXT("Material index %d field CompressedTextureData is empty while ColorStatus is TextureIsSet."),
                MaterialIndex);
            return false;
        }
    }
    return true;
}

bool GenerateMaterialInstances(UObject &Owner, const TArray<FLoadedMaterialData> &MaterialDataList,
                               UMaterialInterface *ParentMaterialInterface,
                               TArray<UMaterialInstanceDynamic *> &OutMaterialInstances)
{
    OutMaterialInstances.Reset();
    if (ParentMaterialInterface == nullptr)
    {
        UE_LOG(LogAssetConstructor, Error, TEXT("Material generation failed: ParentMaterialInterface is null."));
        return false;
    }

    OutMaterialInstances.Reserve(MaterialDataList.Num());
    for (int32 MaterialIndex = 0; MaterialIndex < MaterialDataList.Num(); ++MaterialIndex)
    {
        const FLoadedMaterialData &MaterialData = MaterialDataList[MaterialIndex];
        UTexture2D *Texture = nullptr;

        if (MaterialData.ColorStatus == EColorStatus::ColorIsSet)
        {
            if (!RequireMaterialParameter(*ParentMaterialInterface, MaterialIndex, EMaterialParameterType::Scalar,
                                          TEXT("TextureBlendIntensityForBaseColor")) ||
                !RequireMaterialParameter(*ParentMaterialInterface, MaterialIndex, EMaterialParameterType::Vector,
                                          TEXT("BaseColor4")))
            {
                OutMaterialInstances.Reset();
                return false;
            }
        }
        else if (MaterialData.ColorStatus == EColorStatus::TextureIsSet)
        {
            if (!RequireMaterialParameter(*ParentMaterialInterface, MaterialIndex, EMaterialParameterType::Scalar,
                                          TEXT("TextureBlendIntensityForBaseColor")) ||
                !RequireMaterialParameter(*ParentMaterialInterface, MaterialIndex, EMaterialParameterType::Texture,
                                          TEXT("BaseColorTexture")))
            {
                OutMaterialInstances.Reset();
                return false;
            }
            Texture = FImageUtils::ImportBufferAsTexture2D(MaterialData.CompressedTextureData);
            if (Texture == nullptr)
            {
                UE_LOG(LogAssetConstructor, Error,
                       TEXT("Material index %d: embedded texture could not be converted to UTexture2D."),
                       MaterialIndex);
                OutMaterialInstances.Reset();
                return false;
            }
        }

        UMaterialInstanceDynamic *MaterialInstance = UMaterialInstanceDynamic::Create(ParentMaterialInterface, &Owner);
        if (MaterialInstance == nullptr)
        {
            UE_LOG(LogAssetConstructor, Error, TEXT("Material index %d: MID creation failed."), MaterialIndex);
            OutMaterialInstances.Reset();
            return false;
        }

        if (MaterialData.ColorStatus == EColorStatus::ColorIsSet)
        {
            MaterialInstance->SetScalarParameterValue(TEXT("TextureBlendIntensityForBaseColor"), 0.0f);
            MaterialInstance->SetVectorParameterValue(TEXT("BaseColor4"), MaterialData.Color);
        }
        else if (MaterialData.ColorStatus == EColorStatus::TextureIsSet)
        {
            MaterialInstance->SetScalarParameterValue(TEXT("TextureBlendIntensityForBaseColor"), 1.0f);
            MaterialInstance->SetTextureParameterValue(TEXT("BaseColorTexture"), Texture);
        }
        else
        {
            UE_LOG(
                LogAssetConstructor, Warning,
                TEXT("Material index %d has no usable embedded color or texture; Parent Material defaults are used."),
                MaterialIndex);
        }
        OutMaterialInstances.Add(MaterialInstance);
    }
    return true;
}
