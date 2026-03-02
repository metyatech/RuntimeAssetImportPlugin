// Copyright (c) 2026 metyatech. All rights reserved.

#pragma once

#include "Components/DynamicMeshComponent.h"
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LoadedMeshData.h"
#include "ProceduralMeshComponent.h"

#include "AssetConstructor.generated.h"

/**
 * Type representing the result of executing
 * ConcertProceduralMeshComponentFromAssetFile function.
 */
UENUM(BlueprintType)
enum class EConstructProceduralMeshComponentFromAssetFileResult : uint8 {
	/* Success to load */
	Success,

	/* Failed to load scene */
	Failure
};

/**
 * Type representing the result of executing
 * ConcertStaticMeshComponentFromAssetFile function.
 */
UENUM(BlueprintType)
enum class EConstructStaticMeshComponentFromAssetFileResult : uint8 {
	/* Success to load */
	Success,

	/* Failed to load scene */
	Failure
};

/**
 * Type representing the result of executing
 * ConcertDynamicMeshComponentFromAssetFile function.
 */
UENUM(BlueprintType)
enum class EConstructDynamicMeshComponentFromAssetFileResult : uint8 {
	/* Success to load */
	Success,

	/* Failed to load scene */
	Failure
};

/**
 * Blueprint Function Library for easy constructing of assets at runtime.
 */
UCLASS()
class RUNTIMEASSETIMPORT_API UAssetConstructor
    : public UBlueprintFunctionLibrary {
	GENERATED_BODY()
public:
	/**
	 * Create mesh sections on specified procedural mesh component
	 * @param   MeshData                    mesh data
	 * @param   ParentMaterialInterface     The base material interface used to
	 *                                      create materials for the constructed
	 *                                      meshes.
	 * @param   TargetProceduralMeshComponent Target procedural mesh component to
	 *                                        generate mesh sections on.
	 */
	UFUNCTION(BlueprintCallable, meta = (Latent, LatentInfo = "LatentActionInfo",
	                                     WorldContext = "WorldContextObject"))
	static void CreateMeshFromMeshDataOnProceduralMeshComponent(
	    const UObject* WorldContextObject, FLatentActionInfo LatentActionInfo,
	    const FLoadedMeshData&    MeshData,
	    UMaterialInterface*       ParentMaterialInterface,
	    UProceduralMeshComponent* TargetProceduralMeshComponent);

public:
	/**
	 * Construct structured Procedural Mesh Component from the mesh data.
	 * @param   MeshData                    mesh data
	 * @param   ParentMaterialInterface     The base material interface used to
	 *                                      create materials for the constructed
	 *                                      meshes.
	 * @param   Owner                       Owner of the returned procedural mesh
	 *                                      component, its descendants and its
	 *                                      material instances.
	 * @param   ShouldRegisterComponentToOwner    Whether to register components
	 *                                            to Owner. Must be turned ON to
	 *                                            be reflected in the scene.
	 * @return  the root of the constructed Procedural Mesh Components.
	 * @details  If you have no particular preference, use the
	 *           ConstructDynamicMeshComponentFromMeshData function (Not
	 *           implemented yet, but will be). In multiplayer, when a client and
	 *           server each create a mesh by this function and the client walks
	 *           on it, there occurs a bug that prevents normal movement with a
	 *           warning "LogNetPackageMap: Warning:
	 *           UPackageMapClient::InternalLoadObject: Unable to resolve default
	 *           guid from client".
	 */
	UFUNCTION(BlueprintCallable)
	static UPARAM(DisplayName = "Root Procedural Mesh Component")
	    UProceduralMeshComponent* ConstructProceduralMeshComponentFromMeshData(
	        const FLoadedMeshData& MeshData,
	        UMaterialInterface* ParentMaterialInterface, AActor* Owner,
	        bool ShouldRegisterComponentToOwner = true);

	/**
	 * [Experimental] Construct structured Static Mesh Component from the mesh
	 * data.
	 * WARNING: In packaged builds (no editor), materials display as a
	 * checkerboard pattern. Use ConstructDynamicMeshComponentFromMeshData
	 * (stable, recommended) instead unless you have a specific reason.
	 * @param   MeshData                    mesh data
	 * @param   ParentMaterialInterface     The base material interface used to
	 *                                      create materials for the constructed
	 *                                      meshes.
	 * @param   Owner                       Owner of the returned procedural mesh
	 *                                      component, its descendants and its
	 *                                      material instances.
	 * @param   ShouldRegisterComponentToOwner    Whether to register components
	 *                                            to Owner. Must be turned ON to
	 *                                            be reflected in the scene.
	 * @return  the root of the constructed Static Mesh Components.
	 * @details  In the absence of an editor, such as a packaged game, the
	 *           material disappears and a checkerboard appears.
	 */
	UFUNCTION(BlueprintCallable,
	          meta = (Category = "RuntimeAssetImport|Experimental",
	                  ToolTip = "[Experimental] Construct Static Mesh Component from mesh data. WARNING: materials show as checkerboard in packaged builds. Prefer ConstructDynamicMeshComponentFromMeshData."))
	static UPARAM(DisplayName = "Root Static Mesh Component")
	    UStaticMeshComponent* ConstructStaticMeshComponentFromMeshData(
	        const FLoadedMeshData& MeshData,
	        UMaterialInterface* ParentMaterialInterface, AActor* Owner,
	        bool ShouldRegisterComponentToOwner = true);

	/**
	 * Construct structured Dynamic Mesh Component from the mesh data.
	 * @param   MeshData                    mesh data
	 * @param   ParentMaterialInterface     The base material interface used to
	 *                                      create materials for the constructed
	 *                                      meshes.
	 * @param   Owner                       Owner of the returned dynamic mesh
	 *                                      component, its descendants and its
	 *                                      material instances.
	 * @param   ShouldRegisterComponentToOwner    Whether to register components
	 *                                            to Owner. Must be turned ON to
	 *                                            be reflected in the scene.
	 * @return  the root of the constructed Dynamic Mesh Components.
	 */
	UFUNCTION(BlueprintCallable)
	static UPARAM(DisplayName = "Root Dynamic Mesh Component")
	    UDynamicMeshComponent* ConstructDynamicMeshComponentFromMeshData(
	        const FLoadedMeshData& MeshData,
	        UMaterialInterface* ParentMaterialInterface, AActor* Owner,
	        bool ShouldRegisterComponentToOwner = true);

public:
	/**
	 * Construct structured Procedural Mesh Component from the specified asset
	 * file. The file format must be one supported by assimp.
	 * @param   FilePath                    Path to the asset file.
	 * @param   ParentMaterialInterface     The base material interface used to
	 *                                      create materials for the constructed
	 *                                      meshes.
	 * @param   Owner                       Owner of the returned procedural mesh
	 *                                      component, its descendants and its
	 *                                      material instances.
	 * @param[out]   ConstructProceduralMeshComponentFromAssetFileResult
	 *                  Result of the execution.
	 * @param   ShouldRegisterComponentToOwner    Whether to register components
	 *                                            to Owner. Must be turned ON to
	 *                                            be reflected in the scene.
	 * @return  This function returns the root of the constructed Procedural Mesh
	 *          Components. It also returns the result of the execution with
	 *          ConstructProceduralMeshComponentFromAssetFileResult.
	 *          If the result is Success, the return value is valid,
	 *          If the result is Failure, the return value is nullptr.
	 * @details  If you have no particular preference, use the
	 *           ConstructDynamicMeshComponentFromAssetFile function.
	 *           In multiplayer, when a client and server each create a mesh by
	 *           this function and the client walks on it, there occurs a bug that
	 *           prevents normal movement with a warning "LogNetPackageMap:
	 *           Warning: UPackageMapClient::InternalLoadObject: Unable to resolve
	 *           default guid from client".
	 */
	UFUNCTION(BlueprintCallable,
	          meta = (ExpandEnumAsExecs =
	                      "ConstructProceduralMeshComponentFromAssetFileResult"))
	static UPARAM(DisplayName = "Root Procedural Mesh Component")
	    UProceduralMeshComponent* ConstructProceduralMeshComponentFromAssetFile(
	        const FString& FilePath, UMaterialInterface* ParentMaterialInterface,
	        AActor* Owner,
	        EConstructProceduralMeshComponentFromAssetFileResult&
	             ConstructProceduralMeshComponentFromAssetFileResult,
	        bool ShouldRegisterComponentToOwner = true);

	/**
	 * [Experimental] Construct structured Static Mesh Component from the
	 * specified asset file. The file format must be one supported by assimp.
	 * WARNING: In packaged builds (no editor), materials display as a
	 * checkerboard pattern. Use ConstructDynamicMeshComponentFromAssetFile
	 * (stable, recommended) instead unless you have a specific reason.
	 * @param   FilePath                    Path to the asset file.
	 * @param   ParentMaterialInterface     The base material interface used to
	 *                                      create materials for the constructed
	 *                                      meshes.
	 * @param   Owner                       Owner of the returned static mesh
	 *                                      component, its descendants and its
	 *                                      material instances.
	 * @param[out]   ConstructStaticMeshComponentFromAssetFileResult
	 *                  Result of the execution.
	 * @param   ShouldRegisterComponentToOwner    Whether to register components
	 *                                            to Owner. Must be turned ON to
	 *                                            be reflected in the scene.
	 * @return  This function returns the root of the constructed Static Mesh
	 *          Components. It also returns the result of the execution with
	 *          ConstructStaticMeshComponentFromAssetFileResult.
	 *          If the result is Success, the return value is valid,
	 *          If the result is Failure, the return value is nullptr.
	 * @details  In the absence of an editor, such as a packaged game, the
	 *           material disappears and a checkerboard appears.
	 */
	UFUNCTION(BlueprintCallable,
	          meta = (ExpandEnumAsExecs =
	                      "ConstructStaticMeshComponentFromAssetFileResult",
	                  Category = "RuntimeAssetImport|Experimental",
	                  ToolTip = "[Experimental] Construct Static Mesh Component from asset file. WARNING: materials show as checkerboard in packaged builds. Prefer ConstructDynamicMeshComponentFromAssetFile."))
	static UPARAM(DisplayName = "Root Static Mesh Component")
	    UStaticMeshComponent* ConstructStaticMeshComponentFromAssetFile(
	        const FString& FilePath, UMaterialInterface* ParentMaterialInterface,
	        AActor* Owner,
	        EConstructStaticMeshComponentFromAssetFileResult&
	             ConstructStaticMeshComponentFromAssetFileResult,
	        bool ShouldRegisterComponentToOwner = true);

	/**
	 * Construct structured Dynamic Mesh Component from the specified asset
	 * file. The file format must be one supported by assimp.
	 * @param   FilePath                    Path to the asset file.
	 * @param   ParentMaterialInterface     The base material interface used to
	 *                                      create materials for the constructed
	 *                                      meshes.
	 * @param   Owner                       Owner of the returned dynamic mesh
	 *                                      component, its descendants and its
	 *                                      material instances.
	 * @param[out]   ConstructDynamicMeshComponentFromAssetFileResult
	 *                  Result of the execution.
	 * @param   ShouldRegisterComponentToOwner    Whether to register components
	 *                                            to Owner. Must be turned ON to
	 *                                            be reflected in the scene.
	 * @return  This function returns the root of the constructed Dynamic Mesh
	 *          Components. It also returns the result of the execution with
	 *          ConstructDynamicMeshComponentFromAssetFileResult.
	 *          If the result is Success, the return value is valid,
	 *          If the result is Failure, the return value is nullptr.
	 */
	UFUNCTION(BlueprintCallable,
	          meta = (ExpandEnumAsExecs =
	                      "ConstructDynamicMeshComponentFromAssetFileResult"))
	static UPARAM(DisplayName = "Root Dynamic Mesh Component")
	    UDynamicMeshComponent* ConstructDynamicMeshComponentFromAssetFile(
	        const FString& FilePath, UMaterialInterface* ParentMaterialInterface,
	        AActor* Owner,
	        EConstructDynamicMeshComponentFromAssetFileResult&
	             ConstructDynamicMeshComponentFromAssetFileResult,
	        bool ShouldRegisterComponentToOwner = true);
};
