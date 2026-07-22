// Copyright (c) 2026 metyatech. All rights reserved.

#pragma once

#include "Components/DynamicMeshComponent.h"
#include "CoreMinimal.h"
#include "Engine/CollisionProfile.h"
#include "LoadedMeshData.h"
#include "LogAssetConstructor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "ProceduralMeshConversion.h"

bool ValidateMeshDataForConstruction(const FLoadedMeshData &MeshData, FString &OutErrorMessage);

bool GenerateMaterialInstances(UObject &Owner, const TArray<FLoadedMaterialData> &MaterialDataList,
                               UMaterialInterface *ParentMaterialInterface,
                               TArray<UMaterialInstanceDynamic *> &OutMaterialInstances);

template <typename MeshComponentT>
void DestroyCreatedComponents(AActor *Owner, USceneComponent *OriginalOwnerRoot, MeshComponentT *GeneratedRoot,
                              const bool bInstalledGeneratedRoot, TArray<MeshComponentT *> &Components)
{
    if (bInstalledGeneratedRoot && Owner != nullptr && Owner->GetRootComponent() == GeneratedRoot)
    {
        Owner->SetRootComponent(OriginalOwnerRoot);
    }

    for (int32 ComponentIndex = Components.Num() - 1; ComponentIndex >= 0; --ComponentIndex)
    {
        MeshComponentT *Component = Components[ComponentIndex];
        if (!IsValid(Component))
        {
            continue;
        }
        if (Component->IsRegistered())
        {
            Component->UnregisterComponent();
        }
        Component->DestroyComponent();
    }
    Components.Reset();
}

template <typename MeshComponentT>
MeshComponentT *ConstructMeshComponentFromMeshData(const FLoadedMeshData &MeshData,
                                                   UMaterialInterface *const ParentMaterialInterface,
                                                   AActor *const Owner, const bool ShouldRegisterComponentToOwner)
{
    if (Owner == nullptr)
    {
        UE_LOG(LogAssetConstructor, Error, TEXT("Mesh construction failed: Owner is null."));
        return nullptr;
    }
    if (ParentMaterialInterface == nullptr)
    {
        UE_LOG(LogAssetConstructor, Error, TEXT("Mesh construction failed: ParentMaterialInterface is null."));
        return nullptr;
    }

    FString ValidationError;
    if (!ValidateMeshDataForConstruction(MeshData, ValidationError))
    {
        UE_LOG(LogAssetConstructor, Error, TEXT("Mesh construction validation failed: %s"), *ValidationError);
        return nullptr;
    }

    TArray<UMaterialInstanceDynamic *> MaterialInstances;
    if (!GenerateMaterialInstances(*Owner, MeshData.MaterialList, ParentMaterialInterface, MaterialInstances))
    {
        return nullptr;
    }

    TArray<MeshComponentT *> MeshComponents;
    MeshComponents.Reserve(MeshData.NodeList.Num());
    int32 ConstructedPrimitiveCount = 0;
    USceneComponent *OriginalOwnerRoot = nullptr;
    MeshComponentT *GeneratedRoot = nullptr;
    bool bInstalledGeneratedRoot = false;

    for (int32 NodeIndex = 0; NodeIndex < MeshData.NodeList.Num(); ++NodeIndex)
    {
        const FLoadedMeshNode &Node = MeshData.NodeList[NodeIndex];
        MeshComponentT *MeshComponent = NewObject<MeshComponentT>(Owner);
        if (MeshComponent == nullptr)
        {
            UE_LOG(LogAssetConstructor, Error, TEXT("Node index %d: failed to create mesh component."), NodeIndex);
            DestroyCreatedComponents(Owner, OriginalOwnerRoot, GeneratedRoot, bInstalledGeneratedRoot, MeshComponents);
            return nullptr;
        }
        MeshComponents.Add(MeshComponent);
        MeshComponent->SetRelativeTransform(Node.RelativeTransform);
        MeshComponent->SetNetAddressable();

        if constexpr (TypeTests::TAreTypesEqual_V<UProceduralMeshComponent, MeshComponentT>)
        {
            for (int32 SectionIndex = 0; SectionIndex < Node.Sections.Num(); ++SectionIndex)
            {
                const FLoadedMeshSectionData &Section = Node.Sections[SectionIndex];
                MeshComponent->CreateMeshSection_LinearColor(SectionIndex, Section.Vertices, Section.Triangles,
                                                             Section.Normals, Section.UV0Channel, Section.VertexColors0,
                                                             Section.Tangents, true, false);
                MeshComponent->SetMaterial(SectionIndex, MaterialInstances[Section.MaterialIndex]);
            }
            ConstructedPrimitiveCount += MeshComponent->GetNumSections();
        }
        else if constexpr (TypeTests::TAreTypesEqual_V<UDynamicMeshComponent, MeshComponentT>)
        {
            UProceduralMeshComponent *SourceProceduralMesh = NewObject<UProceduralMeshComponent>(Owner);
            if (SourceProceduralMesh == nullptr)
            {
                UE_LOG(LogAssetConstructor, Error,
                       TEXT("Node index %d: failed to create transient ProceduralMeshComponent."), NodeIndex);
                DestroyCreatedComponents(Owner, OriginalOwnerRoot, GeneratedRoot, bInstalledGeneratedRoot,
                                         MeshComponents);
                return nullptr;
            }
            SourceProceduralMesh->SetRelativeTransform(Node.RelativeTransform);

            for (int32 SectionIndex = 0; SectionIndex < Node.Sections.Num(); ++SectionIndex)
            {
                const FLoadedMeshSectionData &Section = Node.Sections[SectionIndex];
                SourceProceduralMesh->CreateMeshSection_LinearColor(
                    SectionIndex, Section.Vertices, Section.Triangles, Section.Normals, Section.UV0Channel,
                    Section.VertexColors0, Section.Tangents, true, false);
                SourceProceduralMesh->SetMaterial(SectionIndex, MaterialInstances[Section.MaterialIndex]);
            }

            if (SourceProceduralMesh->GetNumSections() > 0)
            {
                FMeshDescription MeshDescription = BuildMeshDescription(SourceProceduralMesh);
                UE::Geometry::FDynamicMesh3 DynamicMesh;
                FMeshDescriptionToDynamicMesh Converter;
                Converter.Convert(&MeshDescription, DynamicMesh, true);
                if (DynamicMesh.TriangleCount() <= 0)
                {
                    UE_LOG(LogAssetConstructor, Error,
                           TEXT("Node index %d: DynamicMesh conversion produced zero triangles."), NodeIndex);
                    SourceProceduralMesh->DestroyComponent();
                    DestroyCreatedComponents(Owner, OriginalOwnerRoot, GeneratedRoot, bInstalledGeneratedRoot,
                                             MeshComponents);
                    return nullptr;
                }

                ConstructedPrimitiveCount += DynamicMesh.TriangleCount();
                MeshComponent->EnableComplexAsSimpleCollision();
                MeshComponent->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
                MeshComponent->ConfigureMaterialSet(SourceProceduralMesh->GetMaterials());
                MeshComponent->SetMesh(MoveTemp(DynamicMesh));
            }
            SourceProceduralMesh->DestroyComponent();
        }
        else
        {
            static_assert(TypeTests::TAreTypesEqual_V<UProceduralMeshComponent, MeshComponentT> ||
                              TypeTests::TAreTypesEqual_V<UDynamicMeshComponent, MeshComponentT>,
                          "Only UProceduralMeshComponent and UDynamicMeshComponent are supported.");
        }

        if (NodeIndex > 0)
        {
            MeshComponentT *ParentComponent = MeshComponents[Node.ParentNodeIndex];
            MeshComponent->SetupAttachment(ParentComponent);
        }
    }

    if (ConstructedPrimitiveCount <= 0)
    {
        UE_LOG(LogAssetConstructor, Error, TEXT("Mesh construction produced no sections or triangles."));
        DestroyCreatedComponents(Owner, OriginalOwnerRoot, GeneratedRoot, bInstalledGeneratedRoot, MeshComponents);
        return nullptr;
    }

    GeneratedRoot = MeshComponents[0];
    if (!ShouldRegisterComponentToOwner)
    {
        return GeneratedRoot;
    }

    OriginalOwnerRoot = Owner->GetRootComponent();
    if (OriginalOwnerRoot != nullptr)
    {
        GeneratedRoot->SetupAttachment(OriginalOwnerRoot);
    }
    else
    {
        if (!Owner->SetRootComponent(GeneratedRoot))
        {
            UE_LOG(LogAssetConstructor, Error, TEXT("Failed to install the generated component as the Owner root."));
            DestroyCreatedComponents(Owner, OriginalOwnerRoot, GeneratedRoot, bInstalledGeneratedRoot, MeshComponents);
            return nullptr;
        }
        bInstalledGeneratedRoot = true;
    }

    for (int32 NodeIndex = 0; NodeIndex < MeshComponents.Num(); ++NodeIndex)
    {
        MeshComponentT *MeshComponent = MeshComponents[NodeIndex];
        MeshComponent->RegisterComponent();
        if (!MeshComponent->IsRegistered())
        {
            UE_LOG(LogAssetConstructor, Error, TEXT("Node index %d: component registration failed."), NodeIndex);
            DestroyCreatedComponents(Owner, OriginalOwnerRoot, GeneratedRoot, bInstalledGeneratedRoot, MeshComponents);
            return nullptr;
        }
    }
    return GeneratedRoot;
}
