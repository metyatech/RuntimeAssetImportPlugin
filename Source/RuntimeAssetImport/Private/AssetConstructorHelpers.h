// Copyright (c) 2026 metyatech. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/DynamicMeshComponent.h"
#include "LoadedMeshData.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "ProceduralMeshConversion.h"

/**
 * Generate material instances from array of material data.
 * @param Owner Owner of the material instances
 * @param MaterialDataList array of material data
 * @param InOutParentMaterialInterface Parent MaterialInterface from which
 *                                the material instance was created
 * @return array of the material instances
 */
TArray<UMaterialInstanceDynamic*>
    GenerateMaterialInstances(UObject&                           Owner,
                              const TArray<FLoadedMaterialData>& MaterialDatas,
                              UMaterialInterface& ParentMaterialInterface);

/**
 * Verify the specified material has the specified parameter.
 * Unreal "verifyf" macro is used for verifying.
 * @param   MaterialInterface       material interface to check for the presence
 *                                  of parameter
 * @param   MaterialParameterType   type of parameter to be verified
 * @param   ParameterName           name of parameter to be verified
 */
void VerifyMaterialParameter(
    const UMaterialInterface&     MaterialInterface,
    const EMaterialParameterType& MaterialParameterType,
    const FName&                  ParameterName);

/**
 * template function to construct specified mesh component from mesh data.
 * @tparam  MeshComponentT UProceduralMesh/UStaticMesh/UDynamicMesh
 * @param   InMeshData                    loaded mesh data
 * @param   InOutParentMaterialInterface     The base material interface used to
 *                                      create materials for the constructed
 *                                      meshes.
 * @param   Owner                       Owner of the returned dynamic mesh
 *                                      component, its descendants and its
 *                                      material instances.
 * @param   ShouldRegisterComponentToOwner    Whether to register components
 *                                            to Owner. Must be turned ON to
 *                                            be reflected in the scene.
 */
template <typename MeshComponentT>
MeshComponentT* ConstructMeshComponentFromMeshData(
    const FLoadedMeshData&    MeshData,
    UMaterialInterface* const ParentMaterialInterface, AActor* const Owner,
    const bool ShouldRegisterComponentToOwner) {
	// check that the NodeList in MeshData has at least one node (because there
	// must be a root node)
	check(!MeshData.NodeList.IsEmpty());

	// check to ParentMaterialInterface is properly set
	check(ParentMaterialInterface != nullptr);

	// check to Owner is properly set
	check(Owner != nullptr);

	// get node list
	const auto& NodeList = MeshData.NodeList;

	// number of the NodeList
	const auto& NumNodeList = NodeList.Num();

	// list of mesh components to be made
	TArray<MeshComponentT*> MeshComponentList;
	MeshComponentList.AddUninitialized(NumNodeList);

	// get material list
	const auto& MaterialList = MeshData.MaterialList;

	// generate material instances
	const auto& MaterialInstances =
	    GenerateMaterialInstances(*Owner, MaterialList, *ParentMaterialInterface);

	// construct Mesh Component Tree
	for (auto Node_i = decltype(NumNodeList){0}; Node_i < NumNodeList; ++Node_i) {
		// get reference of the node
		const auto& Node = NodeList[Node_i];

		// new MeshComponent
		const auto& MeshComponent = NewObject<MeshComponentT>(Owner);

		// set RelativeTransform
		MeshComponent->SetRelativeTransform(Node.RelativeTransform);

		// make MeshComponent network addressable
		MeshComponent->SetNetAddressable();

		// get reference of the sections
		const auto& Sections = Node.Sections;

		// get number of sections
		const auto& NumSections = Sections.Num();

		// create mesh sections
		if constexpr (TypeTests::TAreTypesEqual_V<UProceduralMeshComponent,
		                                          MeshComponentT>) {
			for (auto Section_i = decltype(NumSections){0}; Section_i < NumSections;
			     ++Section_i) {
				// get reference of the section
				const auto& Section = Sections[Section_i];

				// CreateCollision parameter
				constexpr auto CreateCollision = true;

				// bSRGBCConversion parameter
				constexpr auto bSRGBCConversion = false;

				// create mesh section
				MeshComponent->CreateMeshSection_LinearColor(
				    Section_i, Section.Vertices, Section.Triangles, Section.Normals,
				    Section.UV0Channel, Section.VertexColors0, Section.Tangents,
				    CreateCollision, bSRGBCConversion);

				// set Material
				const auto& MaterialIndex    = Section.MaterialIndex;
				const auto& MaterialInstance = MaterialInstances[MaterialIndex];
				MeshComponent->SetMaterial(Section_i, MaterialInstance);
			}
		} else {
			// create transient Procedural Mesh Component
			const auto& SrcProcMeshComp = NewObject<UProceduralMeshComponent>(Owner);

			// set RelativeTransform
			SrcProcMeshComp->SetRelativeTransform(Node.RelativeTransform);

			// create meshes of Procedural Mesh Component
			for (auto Section_i = decltype(NumSections){0}; Section_i < NumSections;
			     ++Section_i) {
				// get reference of the section
				const auto& Section = Sections[Section_i];

				// CreateCollision parameter
				constexpr auto CreateCollision = true;

				// bSRGBCConversion parameter
				constexpr auto bSRGBCConversion = false;

				// create mesh section
				SrcProcMeshComp->CreateMeshSection_LinearColor(
				    Section_i, Section.Vertices, Section.Triangles, Section.Normals,
				    Section.UV0Channel, Section.VertexColors0, Section.Tangents,
				    CreateCollision, bSRGBCConversion);

				// set Material
				const auto& MaterialIndex    = Section.MaterialIndex;
				const auto& MaterialInstance = MaterialInstances[MaterialIndex];
				SrcProcMeshComp->SetMaterial(Section_i, MaterialInstance);
			}

			// get description of ProceduralMesh
			auto ProceduralMeshDescription = BuildMeshDescription(SrcProcMeshComp);

			if constexpr (TypeTests::TAreTypesEqual_V<UStaticMeshComponent,
			                                          MeshComponentT>) {
				// new StaticMesh
				const auto& StaticMesh      = NewObject<UStaticMesh>(Owner);
				StaticMesh->bAllowCPUAccess = true;
				StaticMesh->NeverStream     = true;
				StaticMesh->InitResources();
				StaticMesh->SetLightingGuid();

				// copy meshes
				// create parameters of BuildFromMeshDescriptions function
				{
					UStaticMesh::FBuildMeshDescriptionsParams BuildMeshDescriptionsParams;
#if !WITH_EDITOR
					BuildMeshDescriptionsParams.bFastBuild =
					    true; // set fast build (mandatory in non-editor builds)
					BuildMeshDescriptionsParams.bAllowCpuAccess = true;
#endif

					// copy meshes from SrcProcMeshComp
					StaticMesh->BuildFromMeshDescriptions({&ProceduralMeshDescription},
					                                      BuildMeshDescriptionsParams);
				}

				// copy collisions
				StaticMesh->CalculateExtendedBounds();
				StaticMesh->SetBodySetup(SrcProcMeshComp->ProcMeshBodySetup);

				// copy materials
				for (const auto& MaterialInterface : SrcProcMeshComp->GetMaterials()) {
					StaticMesh->AddMaterial(MaterialInterface);
				}

#if WITH_EDITOR
				StaticMesh->PostEditChange();
#endif

				StaticMesh->MarkPackageDirty();

				// set static mesh
				MeshComponent->SetStaticMesh(StaticMesh);
			} else if constexpr (TypeTests::TAreTypesEqual_V<UDynamicMeshComponent,
			                                                 MeshComponentT>) {
				UE::Geometry::FDynamicMesh3   DynamicMesh3;
				FMeshDescriptionToDynamicMesh MeshDescriptionToDynamicMesh;
				MeshDescriptionToDynamicMesh.Convert(&ProceduralMeshDescription,
				                                     DynamicMesh3, true);

				// enable collisions
				MeshComponent->EnableComplexAsSimpleCollision();
				MeshComponent->SetCollisionProfileName(
				    UCollisionProfile::BlockAllDynamic_ProfileName);

				// set materials
				MeshComponent->ConfigureMaterialSet(SrcProcMeshComp->GetMaterials());

				// set
				MeshComponent->SetMesh(MoveTemp(DynamicMesh3));
			} else {
				// type check error
				static_assert(
				    []() {
					    return false;
				    }(),
				    "Only UProceduralMeshComponent, UStaticMeshComponent or "
				    "UDynamicMeshComponent is "
				    "supported for MeshComponentT.");
			}
		}

		// get parent node index
		const auto& ParentNodeIndex = Node.ParentNodeIndex;

		// if creating a root node
		if (0 == Node_i) {
			if (ShouldRegisterComponentToOwner) {
				// register root to owning actor (Owner) to reflect in the unreal's
				// scene
				MeshComponent->RegisterComponent();
			}
		}
		// if creating a non-root node
		else {
			// get parent Mesh Component
			const auto& ParentMeshComp = MeshComponentList[ParentNodeIndex];

			// if ShouldRegisterComponentToOwner is ON
			if (ShouldRegisterComponentToOwner) {
				// Setup of attachment of this component to parent component
				MeshComponent->SetupAttachment(ParentMeshComp);

				// register component to owning actor (Owner) to reflect in the unreal's
				// scene
				MeshComponent->RegisterComponent();
			}
			// if ShouldRegisterComponentToOwner is OFF
			else {
				// attach this component to parent component
				MeshComponent->AttachToComponent(
				    ParentMeshComp, FAttachmentTransformRules::KeepRelativeTransform);
			}
		}

		// set created Mesh Component
		MeshComponentList[Node_i] = MeshComponent;
	}

	// return root MeshComponent of MeshComponentTree
	return MeshComponentList[0];
}
