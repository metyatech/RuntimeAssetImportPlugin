// Copyright (c) 2026 metyatech. All rights reserved.

#include "AssetConstructorHelpers.h"

#include "AssetImportLimits.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Materials/MaterialInterface.h"

namespace
{
    bool HasExpectedArraySize(const int32 ArraySize, const int32 VertexCount)
    {
        return ArraySize == 0 || ArraySize == VertexCount;
    }

    bool IsFiniteVector(const FVector &Value)
    {
        return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
    }

    bool IsFiniteVector2D(const FVector2D &Value) { return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y); }

    bool IsFiniteColor(const FLinearColor &Value)
    {
        return FMath::IsFinite(Value.R) && FMath::IsFinite(Value.G) && FMath::IsFinite(Value.B) &&
               FMath::IsFinite(Value.A);
    }

    bool IsFiniteQuaternion(const FQuat &Value)
    {
        return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z) &&
               FMath::IsFinite(Value.W);
    }

    bool IsFiniteTransform(const FTransform &Value)
    {
        return IsFiniteVector(Value.GetTranslation()) && IsFiniteVector(Value.GetScale3D()) &&
               IsFiniteQuaternion(Value.GetRotation()) && Value.GetRotation().IsNormalized();
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
        if (!IsFiniteTransform(Node.RelativeTransform))
        {
            OutErrorMessage = FString::Printf(
                TEXT("Node index %d, section index %d field RelativeTransform element index %d contains a "
                     "non-finite or non-normalized value."),
                NodeIndex, INDEX_NONE, 0);
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

            for (int32 ElementIndex = 0; ElementIndex < Section.Vertices.Num(); ++ElementIndex)
            {
                if (!IsFiniteVector(Section.Vertices[ElementIndex]))
                {
                    OutErrorMessage = FString::Printf(
                        TEXT("Node index %d, section index %d field Vertices element index %d contains NaN or Inf."),
                        NodeIndex, SectionIndex, ElementIndex);
                    return false;
                }
            }
            for (int32 ElementIndex = 0; ElementIndex < Section.Normals.Num(); ++ElementIndex)
            {
                if (!IsFiniteVector(Section.Normals[ElementIndex]))
                {
                    OutErrorMessage = FString::Printf(
                        TEXT("Node index %d, section index %d field Normals element index %d contains NaN or Inf."),
                        NodeIndex, SectionIndex, ElementIndex);
                    return false;
                }
            }
            for (int32 ElementIndex = 0; ElementIndex < Section.UV0Channel.Num(); ++ElementIndex)
            {
                if (!IsFiniteVector2D(Section.UV0Channel[ElementIndex]))
                {
                    OutErrorMessage = FString::Printf(
                        TEXT("Node index %d, section index %d field UV0Channel element index %d contains NaN or Inf."),
                        NodeIndex, SectionIndex, ElementIndex);
                    return false;
                }
            }
            for (int32 ElementIndex = 0; ElementIndex < Section.VertexColors0.Num(); ++ElementIndex)
            {
                if (!IsFiniteColor(Section.VertexColors0[ElementIndex]))
                {
                    OutErrorMessage = FString::Printf(
                        TEXT("Node index %d, section index %d field VertexColors0 element index %d contains NaN or "
                             "Inf."),
                        NodeIndex, SectionIndex, ElementIndex);
                    return false;
                }
            }
            for (int32 ElementIndex = 0; ElementIndex < Section.Tangents.Num(); ++ElementIndex)
            {
                if (!IsFiniteVector(Section.Tangents[ElementIndex].TangentX))
                {
                    OutErrorMessage = FString::Printf(
                        TEXT("Node index %d, section index %d field Tangents.TangentX element index %d contains NaN "
                             "or Inf."),
                        NodeIndex, SectionIndex, ElementIndex);
                    return false;
                }
            }
        }
    }

    for (int32 MaterialIndex = 0; MaterialIndex < MeshData.MaterialList.Num(); ++MaterialIndex)
    {
        const FLoadedMaterialData &MaterialData = MeshData.MaterialList[MaterialIndex];
        switch (MaterialData.ColorStatus)
        {
        case EColorStatus::None:
        case EColorStatus::ColorIsSet:
        case EColorStatus::TextureIsSet:
        case EColorStatus::TextureWasSetButError:
            break;
        default:
            OutErrorMessage = FString::Printf(
                TEXT("Material index %d, node index %d, section index %d field ColorStatus element index %d is "
                     "invalid."),
                MaterialIndex, INDEX_NONE, INDEX_NONE, 0);
            return false;
        }
        if (!IsFiniteColor(MaterialData.Color))
        {
            OutErrorMessage = FString::Printf(
                TEXT("Material index %d, node index %d, section index %d field Color element index %d contains NaN "
                     "or Inf."),
                MaterialIndex, INDEX_NONE, INDEX_NONE, 0);
            return false;
        }
        if (MaterialData.ColorStatus == EColorStatus::TextureIsSet &&
            !RuntimeAssetImport::Limits::IsCompressedTextureByteCountValid(
                static_cast<uint64>(MaterialData.CompressedTextureData.Num())))
        {
            OutErrorMessage = FString::Printf(
                TEXT("Material index %d field CompressedTextureData has %d bytes while ColorStatus is TextureIsSet; "
                     "the valid range is 1..%llu bytes."),
                MaterialIndex, MaterialData.CompressedTextureData.Num(),
                RuntimeAssetImport::Limits::MaximumCompressedTextureBytes);
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
