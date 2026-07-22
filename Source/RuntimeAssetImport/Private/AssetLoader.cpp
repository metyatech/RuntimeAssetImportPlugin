// Copyright (c) 2026 metyatech. All rights reserved.

#include "AssetLoader.h"

#include "HasFeatureFix.h"
#include "ImageUtils.h"
#include "LogAssetLoader.h"
#include "Misc/Paths.h"

#include <assimp/Importer.hpp>
#include <assimp/pbrmaterial.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace
{
    constexpr unsigned int AiImportFlags =
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_CalcTangentSpace |
        aiProcess_GenSmoothNormals | aiProcess_OptimizeMeshes | aiProcess_RemoveRedundantMaterials |
        aiProcess_ImproveCacheLocality | aiProcess_FindInvalidData | aiProcess_EmbedTextures | aiProcess_GenUVCoords |
        aiProcess_TransformUVCoords | aiProcess_MakeLeftHanded | aiProcess_FlipUVs;
    constexpr int32 MaximumNodeDepth = 1024;

    FString GetNodeName(const aiNode &Node)
    {
        const FString Name = UTF8_TO_TCHAR(Node.mName.C_Str());
        return Name.IsEmpty() ? TEXT("<unnamed>") : Name;
    }

    bool IsFiniteMatrix(const aiMatrix4x4 &Matrix)
    {
        return FMath::IsFinite(Matrix.a1) && FMath::IsFinite(Matrix.a2) && FMath::IsFinite(Matrix.a3) &&
               FMath::IsFinite(Matrix.a4) && FMath::IsFinite(Matrix.b1) && FMath::IsFinite(Matrix.b2) &&
               FMath::IsFinite(Matrix.b3) && FMath::IsFinite(Matrix.b4) && FMath::IsFinite(Matrix.c1) &&
               FMath::IsFinite(Matrix.c2) && FMath::IsFinite(Matrix.c3) && FMath::IsFinite(Matrix.c4) &&
               FMath::IsFinite(Matrix.d1) && FMath::IsFinite(Matrix.d2) && FMath::IsFinite(Matrix.d3) &&
               FMath::IsFinite(Matrix.d4);
    }

    FMatrix AiMatrixToUEMatrix(const aiMatrix4x4 &Matrix)
    {
        return {{Matrix.a1, Matrix.b1, Matrix.c1, Matrix.d1},
                {Matrix.a2, Matrix.b2, Matrix.c2, Matrix.d2},
                {Matrix.a3, Matrix.b3, Matrix.c3, Matrix.d3},
                {Matrix.a4, Matrix.b4, Matrix.c4, Matrix.d4}};
    }

    float GetAiUnitScaleFactor(const aiScene &Scene)
    {
        if (Scene.mMetaData == nullptr)
        {
            return 1.0f;
        }

        float UnitScaleFactor = 1.0f;
        if (!Scene.mMetaData->Get("UnitScaleFactor", UnitScaleFactor) || !FMath::IsFinite(UnitScaleFactor))
        {
            return 1.0f;
        }
        return UnitScaleFactor;
    }

    aiMatrix4x4 GenerateAiToUETransform(const aiScene &Scene)
    {
        aiMatrix4x4 Scale;
        aiMatrix4x4::Scaling(aiVector3D(GetAiUnitScaleFactor(Scene)), Scale);

        aiMatrix4x4 Rotation;
        aiMatrix4x4::RotationX(PI / 2.0f, Rotation);
        return Scale * Rotation;
    }

    void TransformToUECoordinateSystem(const aiScene &Scene)
    {
        Scene.mRootNode->mTransformation = GenerateAiToUETransform(Scene) * Scene.mRootNode->mTransformation;
    }

    FLoadedMaterialData MakeDefaultMaterialData()
    {
        FLoadedMaterialData MaterialData;
        MaterialData.ColorStatus = EColorStatus::ColorIsSet;
        MaterialData.Color = FLinearColor::White;
        return MaterialData;
    }

    FLoadedMaterialData ConvertMaterial(const aiScene &Scene, const aiMaterial *Material, const int32 MaterialIndex)
    {
        if (Material == nullptr)
        {
            UE_LOG(LogAssetLoader, Warning, TEXT("Material index %d is null; using white."), MaterialIndex);
            return MakeDefaultMaterialData();
        }

        aiTextureType TextureType = aiTextureType_DIFFUSE;
        unsigned int TextureCount = Material->GetTextureCount(aiTextureType_DIFFUSE);
        if (TextureCount == 0)
        {
            TextureType = aiTextureType_BASE_COLOR;
            TextureCount = Material->GetTextureCount(aiTextureType_BASE_COLOR);
        }

        if (TextureCount > 1)
        {
            UE_LOG(LogAssetLoader, Warning,
                   TEXT("Material index %d has %u diffuse/base-color textures; only the first is used."), MaterialIndex,
                   TextureCount);
        }

        if (TextureCount == 0)
        {
            FLoadedMaterialData MaterialData = MakeDefaultMaterialData();
            aiColor4D Color;
            aiReturn ColorResult = Material->Get(AI_MATKEY_BASE_COLOR, Color);
            if (ColorResult != aiReturn_SUCCESS)
            {
                ColorResult = Material->Get(AI_MATKEY_COLOR_DIFFUSE, Color);
            }
            if (ColorResult == aiReturn_SUCCESS)
            {
                MaterialData.Color = FLinearColor(Color.r, Color.g, Color.b, Color.a);
            }
            else
            {
                UE_LOG(LogAssetLoader, Warning,
                       TEXT("Material index %d has no readable diffuse/base color; using white."), MaterialIndex);
            }
            return MaterialData;
        }

        FLoadedMaterialData MaterialData;
        MaterialData.ColorStatus = EColorStatus::TextureWasSetButError;

        aiString TexturePath;
        if (Material->Get(AI_MATKEY_TEXTURE(TextureType, 0), TexturePath) != aiReturn_SUCCESS)
        {
            UE_LOG(LogAssetLoader, Warning, TEXT("Material index %d texture path could not be read."), MaterialIndex);
            return MaterialData;
        }

        const aiTexture *Texture = Scene.GetEmbeddedTexture(TexturePath.C_Str());
        if (Texture == nullptr)
        {
            UE_LOG(LogAssetLoader, Warning, TEXT("Material index %d texture '%s' is external and is not loaded."),
                   MaterialIndex, UTF8_TO_TCHAR(TexturePath.C_Str()));
            return MaterialData;
        }

        if (Texture->mHeight == 0)
        {
            if (Texture->mWidth == 0 || Texture->pcData == nullptr)
            {
                UE_LOG(LogAssetLoader, Warning, TEXT("Material index %d embedded texture data is empty."),
                       MaterialIndex);
                return MaterialData;
            }

            const uint8 *TextureBytes = reinterpret_cast<const uint8 *>(Texture->pcData);
            MaterialData.CompressedTextureData.Append(TextureBytes, static_cast<int32>(Texture->mWidth));
        }
        else
        {
            if (Texture->mWidth == 0 || Texture->pcData == nullptr)
            {
                UE_LOG(LogAssetLoader, Warning, TEXT("Material index %d raw embedded texture data is empty."),
                       MaterialIndex);
                return MaterialData;
            }

            TArray64<uint8> CompressedTextureData;
            const FImageView ImageView(Texture->pcData, Texture->mWidth, Texture->mHeight, ERawImageFormat::BGRA8);
            FImageUtils::CompressImage(CompressedTextureData, TEXT("png"), ImageView);
            if (!CompressedTextureData.IsEmpty())
            {
                MaterialData.CompressedTextureData = MoveTemp(CompressedTextureData);
            }
        }

        if (MaterialData.CompressedTextureData.IsEmpty())
        {
            UE_LOG(LogAssetLoader, Warning, TEXT("Material index %d embedded texture could not be encoded."),
                   MaterialIndex);
            return MaterialData;
        }

        MaterialData.ColorStatus = EColorStatus::TextureIsSet;
        return MaterialData;
    }

    bool GenerateMaterialList(const aiScene &Scene, TArray<FLoadedMaterialData> &OutMaterialList,
                              FString &OutErrorMessage)
    {
        OutMaterialList.Reset();
        if (Scene.mNumMaterials == 0 || Scene.mMaterials == nullptr)
        {
            UE_LOG(LogAssetLoader, Warning, TEXT("The scene has no readable materials; using white."));
            OutMaterialList.Add(MakeDefaultMaterialData());
            return true;
        }

        if (Scene.mNumMaterials > static_cast<unsigned int>(MAX_int32))
        {
            OutErrorMessage = FString::Printf(TEXT("Scene field mNumMaterials is too large: %u."), Scene.mNumMaterials);
            return false;
        }

        OutMaterialList.Reserve(static_cast<int32>(Scene.mNumMaterials));
        for (unsigned int MaterialIndex = 0; MaterialIndex < Scene.mNumMaterials; ++MaterialIndex)
        {
            OutMaterialList.Add(
                ConvertMaterial(Scene, Scene.mMaterials[MaterialIndex], static_cast<int32>(MaterialIndex)));
        }
        return true;
    }

    bool ConstructMeshData(const aiScene &Scene, const aiNode &Node, const int32 ParentNodeIndex, const int32 Depth,
                           TSet<const aiNode *> &VisitedNodes, FLoadedMeshData &OutMeshData, FString &OutErrorMessage)
    {
        const int32 NodeIndex = OutMeshData.NodeList.Num();
        const FString NodeName = GetNodeName(Node);

        if (Depth > MaximumNodeDepth)
        {
            OutErrorMessage = FString::Printf(TEXT("Node index %d ('%s') field Depth exceeds %d: %d."), NodeIndex,
                                              *NodeName, MaximumNodeDepth, Depth);
            return false;
        }
        if (VisitedNodes.Contains(&Node))
        {
            OutErrorMessage =
                FString::Printf(TEXT("Node index %d ('%s') was visited more than once."), NodeIndex, *NodeName);
            return false;
        }
        VisitedNodes.Add(&Node);

        if (!IsFiniteMatrix(Node.mTransformation))
        {
            OutErrorMessage = FString::Printf(TEXT("Node index %d ('%s') field mTransformation contains NaN or Inf."),
                                              NodeIndex, *NodeName);
            return false;
        }
        if (Node.mNumChildren > 0 && Node.mChildren == nullptr)
        {
            OutErrorMessage = FString::Printf(TEXT("Node index %d ('%s') field mChildren is null for %u children."),
                                              NodeIndex, *NodeName, Node.mNumChildren);
            return false;
        }
        if (Node.mNumMeshes > 0 && Node.mMeshes == nullptr)
        {
            OutErrorMessage = FString::Printf(TEXT("Node index %d ('%s') field mMeshes is null for %u meshes."),
                                              NodeIndex, *NodeName, Node.mNumMeshes);
            return false;
        }

        FLoadedMeshNode LoadedNode;
        LoadedNode.Name = NodeName;
        LoadedNode.ParentNodeIndex = ParentNodeIndex;
        LoadedNode.RelativeTransform = FTransform(AiMatrixToUEMatrix(Node.mTransformation));
        if (LoadedNode.RelativeTransform.ContainsNaN())
        {
            OutErrorMessage = FString::Printf(TEXT("Node index %d ('%s') converted transform contains NaN or Inf."),
                                              NodeIndex, *NodeName);
            return false;
        }

        LoadedNode.Sections.Reserve(static_cast<int32>(Node.mNumMeshes));
        for (unsigned int NodeMeshIndex = 0; NodeMeshIndex < Node.mNumMeshes; ++NodeMeshIndex)
        {
            const unsigned int SceneMeshIndex = Node.mMeshes[NodeMeshIndex];
            if (SceneMeshIndex >= Scene.mNumMeshes)
            {
                OutErrorMessage = FString::Printf(
                    TEXT("Node index %d ('%s'), mesh index %u: field mMeshes[%u]=%u is outside scene mesh count %u."),
                    NodeIndex, *NodeName, NodeMeshIndex, NodeMeshIndex, SceneMeshIndex, Scene.mNumMeshes);
                return false;
            }

            const aiMesh *Mesh = Scene.mMeshes[SceneMeshIndex];
            if (Mesh == nullptr)
            {
                OutErrorMessage =
                    FString::Printf(TEXT("Node index %d ('%s'), mesh index %u: scene mesh pointer is null."), NodeIndex,
                                    *NodeName, SceneMeshIndex);
                return false;
            }
            if (Mesh->mNumVertices < 3 || Mesh->mVertices == nullptr || !Mesh->HasPositions())
            {
                OutErrorMessage = FString::Printf(
                    TEXT("Node index %d ('%s'), mesh index %u: field mVertices is invalid (count=%u, pointer=%p)."),
                    NodeIndex, *NodeName, SceneMeshIndex, Mesh->mNumVertices, Mesh->mVertices);
                return false;
            }
            if (Mesh->mNumVertices > static_cast<unsigned int>(MAX_int32))
            {
                OutErrorMessage =
                    FString::Printf(TEXT("Node index %d ('%s'), mesh index %u: mNumVertices is too large: %u."),
                                    NodeIndex, *NodeName, SceneMeshIndex, Mesh->mNumVertices);
                return false;
            }
            if (Mesh->mNumFaces == 0 || Mesh->mFaces == nullptr || !Mesh->HasFaces())
            {
                OutErrorMessage = FString::Printf(
                    TEXT("Node index %d ('%s'), mesh index %u: field mFaces is invalid (count=%u, pointer=%p)."),
                    NodeIndex, *NodeName, SceneMeshIndex, Mesh->mNumFaces, Mesh->mFaces);
                return false;
            }
            if (Mesh->mNumFaces > static_cast<unsigned int>(MAX_int32 / 3))
            {
                OutErrorMessage =
                    FString::Printf(TEXT("Node index %d ('%s'), mesh index %u: mNumFaces is too large: %u."), NodeIndex,
                                    *NodeName, SceneMeshIndex, Mesh->mNumFaces);
                return false;
            }

            FLoadedMeshSectionData Section;
            Section.Vertices.Reserve(static_cast<int32>(Mesh->mNumVertices));
            for (unsigned int VertexIndex = 0; VertexIndex < Mesh->mNumVertices; ++VertexIndex)
            {
                const aiVector3D &Vertex = Mesh->mVertices[VertexIndex];
                if (!FMath::IsFinite(Vertex.x) || !FMath::IsFinite(Vertex.y) || !FMath::IsFinite(Vertex.z))
                {
                    OutErrorMessage = FString::Printf(
                        TEXT("Node index %d ('%s'), mesh index %u: field mVertices[%u] contains NaN or Inf."),
                        NodeIndex, *NodeName, SceneMeshIndex, VertexIndex);
                    return false;
                }
                Section.Vertices.Add(FVector(Vertex.x, Vertex.y, Vertex.z));
            }

            Section.Triangles.Reserve(static_cast<int32>(Mesh->mNumFaces * 3));
            for (unsigned int FaceIndex = 0; FaceIndex < Mesh->mNumFaces; ++FaceIndex)
            {
                const aiFace &Face = Mesh->mFaces[FaceIndex];
                if (Face.mIndices == nullptr)
                {
                    OutErrorMessage =
                        FString::Printf(TEXT("Node index %d ('%s'), mesh index %u: field mFaces[%u].mIndices is null."),
                                        NodeIndex, *NodeName, SceneMeshIndex, FaceIndex);
                    return false;
                }
                if (Face.mNumIndices != 3)
                {
                    OutErrorMessage = FString::Printf(
                        TEXT("Node index %d ('%s'), mesh index %u: field mFaces[%u].mNumIndices must be 3, got %u."),
                        NodeIndex, *NodeName, SceneMeshIndex, FaceIndex, Face.mNumIndices);
                    return false;
                }
                for (unsigned int TriangleIndex = 0; TriangleIndex < 3; ++TriangleIndex)
                {
                    const unsigned int VertexIndex = Face.mIndices[TriangleIndex];
                    if (VertexIndex >= Mesh->mNumVertices)
                    {
                        OutErrorMessage = FString::Printf(
                            TEXT("Node index %d ('%s'), mesh index %u: field mFaces[%u].mIndices[%u]=%u exceeds "
                                 "vertex count %u."),
                            NodeIndex, *NodeName, SceneMeshIndex, FaceIndex, TriangleIndex, VertexIndex,
                            Mesh->mNumVertices);
                        return false;
                    }
                    Section.Triangles.Add(static_cast<int32>(VertexIndex));
                }
            }

            if (Mesh->HasNormals() && Mesh->mNormals != nullptr)
            {
                Section.Normals.Reserve(static_cast<int32>(Mesh->mNumVertices));
                for (unsigned int VertexIndex = 0; VertexIndex < Mesh->mNumVertices; ++VertexIndex)
                {
                    const aiVector3D &Normal = Mesh->mNormals[VertexIndex];
                    Section.Normals.Add(FVector(Normal.x, Normal.y, Normal.z));
                }
            }

            const unsigned int UVChannelCount = Mesh->GetNumUVChannels();
            if (UVChannelCount > 1)
            {
                UE_LOG(LogAssetLoader, Warning,
                       TEXT("Node index %d ('%s'), mesh index %u has %u UV channels; only channel 0 is used."),
                       NodeIndex, *NodeName, SceneMeshIndex, UVChannelCount);
            }
            if (Mesh->HasTextureCoords(0) && Mesh->mTextureCoords[0] != nullptr)
            {
                Section.UV0Channel.Reserve(static_cast<int32>(Mesh->mNumVertices));
                for (unsigned int VertexIndex = 0; VertexIndex < Mesh->mNumVertices; ++VertexIndex)
                {
                    const aiVector3D &UV = Mesh->mTextureCoords[0][VertexIndex];
                    Section.UV0Channel.Add(FVector2D(UV.x, UV.y));
                }
            }

            const unsigned int VertexColorChannelCount = Mesh->GetNumColorChannels();
            if (VertexColorChannelCount > 1)
            {
                UE_LOG(
                    LogAssetLoader, Warning,
                    TEXT("Node index %d ('%s'), mesh index %u has %u vertex-color channels; only channel 0 is used."),
                    NodeIndex, *NodeName, SceneMeshIndex, VertexColorChannelCount);
            }
            if (Mesh->HasVertexColors(0) && Mesh->mColors[0] != nullptr)
            {
                Section.VertexColors0.Reserve(static_cast<int32>(Mesh->mNumVertices));
                for (unsigned int VertexIndex = 0; VertexIndex < Mesh->mNumVertices; ++VertexIndex)
                {
                    const aiColor4D &Color = Mesh->mColors[0][VertexIndex];
                    Section.VertexColors0.Add(FLinearColor(Color.r, Color.g, Color.b, Color.a));
                }
            }

            if (Mesh->HasTangentsAndBitangents() && Mesh->mTangents != nullptr)
            {
                Section.Tangents.Reserve(static_cast<int32>(Mesh->mNumVertices));
                for (unsigned int VertexIndex = 0; VertexIndex < Mesh->mNumVertices; ++VertexIndex)
                {
                    const aiVector3D &Tangent = Mesh->mTangents[VertexIndex];
                    Section.Tangents.Add(FProcMeshTangent(Tangent.x, Tangent.y, Tangent.z));
                }
            }

            if (Mesh->mMaterialIndex < static_cast<unsigned int>(OutMeshData.MaterialList.Num()))
            {
                Section.MaterialIndex = static_cast<int32>(Mesh->mMaterialIndex);
            }
            else
            {
                UE_LOG(LogAssetLoader, Warning,
                       TEXT("Node index %d ('%s'), mesh index %u material index %u is outside %d materials; using 0."),
                       NodeIndex, *NodeName, SceneMeshIndex, Mesh->mMaterialIndex, OutMeshData.MaterialList.Num());
                Section.MaterialIndex = 0;
            }
            LoadedNode.Sections.Add(MoveTemp(Section));
        }

        OutMeshData.NodeList.Add(MoveTemp(LoadedNode));
        for (unsigned int ChildIndex = 0; ChildIndex < Node.mNumChildren; ++ChildIndex)
        {
            const aiNode *Child = Node.mChildren[ChildIndex];
            if (Child == nullptr)
            {
                OutErrorMessage = FString::Printf(TEXT("Node index %d ('%s') field mChildren[%u] is null."), NodeIndex,
                                                  *NodeName, ChildIndex);
                return false;
            }
            if (!ConstructMeshData(Scene, *Child, NodeIndex, Depth + 1, VisitedNodes, OutMeshData, OutErrorMessage))
            {
                return false;
            }
        }
        return true;
    }

    bool ConstructLoadedMeshData(const aiScene &Scene, FLoadedMeshData &OutMeshData, FString &OutErrorMessage)
    {
        OutMeshData = FLoadedMeshData();
        if (Scene.mRootNode == nullptr)
        {
            OutErrorMessage = TEXT("Scene field mRootNode is null.");
            return false;
        }
        if (Scene.mNumMeshes == 0)
        {
            OutErrorMessage = TEXT("Scene field mNumMeshes is zero.");
            return false;
        }
        if (Scene.mMeshes == nullptr)
        {
            OutErrorMessage = FString::Printf(TEXT("Scene field mMeshes is null for %u meshes."), Scene.mNumMeshes);
            return false;
        }
        if (!GenerateMaterialList(Scene, OutMeshData.MaterialList, OutErrorMessage))
        {
            return false;
        }

        TransformToUECoordinateSystem(Scene);
        TSet<const aiNode *> VisitedNodes;
        if (!ConstructMeshData(Scene, *Scene.mRootNode, INDEX_NONE, 0, VisitedNodes, OutMeshData, OutErrorMessage))
        {
            return false;
        }

        bool HasMeshSection = false;
        for (const FLoadedMeshNode &Node : OutMeshData.NodeList)
        {
            if (!Node.Sections.IsEmpty())
            {
                HasMeshSection = true;
                break;
            }
        }
        if (!HasMeshSection)
        {
            OutErrorMessage = TEXT("Constructed mesh data contains no mesh sections.");
            return false;
        }
        return true;
    }
} // namespace

FLoadedMeshData UAssetLoader::LoadMeshFromAssetFile(const FString &FilePath,
                                                    ELoadMeshFromAssetFileResult &LoadMeshFromAssetFileResult)
{
    LoadMeshFromAssetFileResult = ELoadMeshFromAssetFileResult::Failure;
    if (FilePath.IsEmpty())
    {
        UE_LOG(LogAssetLoader, Error, TEXT("Asset file path is empty: '%s'."), *FilePath);
        return {};
    }
    if (!FPaths::FileExists(FilePath))
    {
        UE_LOG(LogAssetLoader, Error, TEXT("Asset file does not exist: '%s'."), *FilePath);
        return {};
    }

    Assimp::Importer Importer;
    const aiScene *Scene = Importer.ReadFile(TCHAR_TO_UTF8(*FilePath), AiImportFlags);
    if (Scene == nullptr)
    {
        UE_LOG(LogAssetLoader, Error, TEXT("Assimp failed to load '%s': %s"), *FilePath,
               UTF8_TO_TCHAR(Importer.GetErrorString()));
        return {};
    }

    FLoadedMeshData MeshData;
    FString ErrorMessage;
    if (!ConstructLoadedMeshData(*Scene, MeshData, ErrorMessage))
    {
        UE_LOG(LogAssetLoader, Error, TEXT("Asset file '%s' is invalid: %s"), *FilePath, *ErrorMessage);
        return {};
    }

    LoadMeshFromAssetFileResult = ELoadMeshFromAssetFileResult::Success;
    return MeshData;
}

FLoadedMeshData UAssetLoader::LoadMeshFromAssetData(const TArray<uint8> &AssetData,
                                                    ELoadMeshFromAssetDataResult &LoadMeshFromAssetDataResult)
{
    LoadMeshFromAssetDataResult = ELoadMeshFromAssetDataResult::Failure;
    if (AssetData.IsEmpty())
    {
        UE_LOG(LogAssetLoader, Error, TEXT("Asset byte array is empty."));
        return {};
    }

    Assimp::Importer Importer;
    const aiScene *Scene =
        Importer.ReadFileFromMemory(AssetData.GetData(), static_cast<size_t>(AssetData.Num()), AiImportFlags);
    if (Scene == nullptr)
    {
        UE_LOG(LogAssetLoader, Error, TEXT("Assimp failed to load asset bytes: %s"),
               UTF8_TO_TCHAR(Importer.GetErrorString()));
        return {};
    }

    FLoadedMeshData MeshData;
    FString ErrorMessage;
    if (!ConstructLoadedMeshData(*Scene, MeshData, ErrorMessage))
    {
        UE_LOG(LogAssetLoader, Error, TEXT("Asset byte array contains invalid mesh data: %s"), *ErrorMessage);
        return {};
    }

    LoadMeshFromAssetDataResult = ELoadMeshFromAssetDataResult::Success;
    return MeshData;
}
