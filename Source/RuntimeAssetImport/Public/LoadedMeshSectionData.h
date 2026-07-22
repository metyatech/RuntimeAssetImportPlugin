// Copyright (c) 2026 metyatech. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

#include "LoadedMeshSectionData.generated.h"

/**
 * Mesh section data that make up a portion of a single mesh node.
 */
USTRUCT(BlueprintType)
struct RUNTIMEASSETIMPORT_API FLoadedMeshSectionData
{
    GENERATED_BODY()

    // Coordinates of vertices
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Runtime Asset Import|Mesh Section")
    TArray<FVector> Vertices;

    // Each element of this array represents an index in the Vertices array. It
    // is meaningful in pairs of three from the beginning of the array. For
    // example, if Triangles is {0,1,2,2,1,3}, the first three elements (0,1,2)
    // represent a triangle whose vertices are the elements at indices 0,1,2
    // in the Vertices array, and the next three elements (2,1,3) represent a
    // triangle whose vertices are the elements at indices 0,1,2 in the
    // Vertices array. The order is also important: (0,1,2) indicates that when
    // the right-hand screw is turned in the order 0 -> 1 -> 2, the side to which
    // the right-hand screw advances is the front side of the triangle. See
    // details: https://monsho.hatenablog.com/entry/2015/06/20/010747
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Runtime Asset Import|Mesh Section")
    TArray<int32> Triangles;

    // An array of normal vectors for each element of Vertices. Must have
    // the same number of elements as Vertices.
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Runtime Asset Import|Mesh Section")
    TArray<FVector> Normals;

    // An array of texture coordinates for each element of Vertices.
    // Must have the same number of elements as Vertices.
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Runtime Asset Import|Mesh Section")
    TArray<FVector2D> UV0Channel;

    // Array of vertex color for each element of Vertices.
    // Must have the same number of elements as Vertices.
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Runtime Asset Import|Mesh Section")
    TArray<FLinearColor> VertexColors0;

    // An array indicating the tangent direction of each element of Vertices.
    // Must have the same number of elements as Vertices.
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Runtime Asset Import|Mesh Section")
    TArray<FProcMeshTangent> Tangents;

    // Index in FLoadedMeshData::MaterialList of the material used by this mesh
    // section. INDEX_NONE means no material has been assigned.
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Runtime Asset Import|Mesh Section")
    int32 MaterialIndex = INDEX_NONE;
};
