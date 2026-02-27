// Fill out your copyright notice in the Description page of Project Settings.

#include "AssetLoader.h"

#include "ImageUtils.h"
#include "LogAssetLoader.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#pragma region forward declarations of static functions
/**
 * Load Ai(Assimp) Scene
 * @param AiImporter Assimp Importer
 * @param FilePath Path to the file
 * @return a valid pointer in case of success, nullptr in case of failure.
 */
static const aiScene* LoadAiScene(Assimp::Importer& AiImporter,
                                  const FString&    FilePath);

/**
 * Load Ai(Assimp) Scene
 * @param AiImporter Assimp Importer
 * @param AssetData Asset data on memory
 * @return a valid pointer in case of success, nullptr in case of failure.
 */
static const aiScene* LoadAiScene(Assimp::Importer&    AiImporter,
                                  const TArray<uint8>& AssetData);

/**
 * Construct mesh data from AiScene
 * @param        AiScene           assimp's scene object.
 */
static FLoadedMeshData ConstructMeshData(const aiScene& AiScene);

/**
 * Transform the coordinate system of an assimp scene to the UE coordinate
 * system.
 * Directly overwrite mTransformation of the root node.
 * @param AiScene Ai(Assimp) Scene
 */
static void TransformToUECoordinateSystem(const aiScene& AiScene);

/**
 * Get UnitScaleFactor meta data from the scene
 * @param AiScene Ai(Assimp) Scene
 * @return the value if data is available, otherwise 1.0f
 */
static float GetAiUnitScaleFactor(const aiScene& AiScene);

/**
 * Generate a transformation matrix to transform from the Ai(Assimp) coordinate
 * system to the UE coordinate system.
 * @param AiScene Ai(Assimp) Scene
 */
static aiMatrix4x4t<float> GenerateAi_UE_XformMatrix(const aiScene& AiScene);

/**
 * Generate material list from Ai(Assimp) Scene object.
 * @param AiScene Ai(Assimp) Scene
 */
static TArray<FLoadedMaterialData> GenerateMaterialList(const aiScene& AiScene);

/**
 * Construct mesh data recursively from the AiNode
 * @param        AiScene           assimp's scene object.
 * @param        AiNode            assimp's node object to start treeing.
 * @param[out]   MeshData          constructed mesh data
 * @param        ParentNodeIndex   index of the parent node.
 *                                 if AiNode is the root node, specify -1.
 */
static void ConstructMeshData(const aiScene& AiScene, const aiNode& AiNode,
                              FLoadedMeshData& MeshData, int ParentNodeIndex);

/**
 * Convert assimp's matrix to UE's matrix
 * Return transpose of the assimp's matrix as the UE's matrix. (since one is
 * transpose of the other one).
 * @param   AiMatrix4x4   assimp's matrix
 * @return  UE's matrix
 */
static FMatrix AiMatrixToUEMatrix(const aiMatrix4x4& AiMatrix4x4);
#pragma endregion

FLoadedMeshData UAssetLoader::LoadMeshFromAssetFile(
    const FString&                FilePath,
    ELoadMeshFromAssetFileResult& LoadMeshFromAssetFileResult) {
	// construct Ai(Assimp) Importer
	Assimp::Importer AiImporter;

	// load AiScene
	const auto& AiScene = LoadAiScene(AiImporter, FilePath);

	// When a scene fails to load
	if (nullptr == AiScene) {
		// assume the result is failure
		LoadMeshFromAssetFileResult = ELoadMeshFromAssetFileResult::Failure;

		// return empty mesh data
		return {};
	}

	// assume the result is success
	LoadMeshFromAssetFileResult = ELoadMeshFromAssetFileResult::Success;

	// construct mesh data
	FLoadedMeshData MeshData = ConstructMeshData(*AiScene);

	// return mesh data
	return MeshData;
}

FLoadedMeshData UAssetLoader::LoadMeshFromAssetData(
    const TArray<uint8>&          AssetData,
    ELoadMeshFromAssetDataResult& LoadMeshFromAssetDataResult) {
	// construct Ai(Assimp) Importer
	Assimp::Importer AiImporter;

	// load AiScene
	const auto& AiScene = LoadAiScene(AiImporter, AssetData);

	// When a scene fails to load
	if (nullptr == AiScene) {
		// assume the result is failure
		LoadMeshFromAssetDataResult = ELoadMeshFromAssetDataResult::Failure;

		// return empty mesh data
		return {};
	}

	// assume the result is success
	LoadMeshFromAssetDataResult = ELoadMeshFromAssetDataResult::Success;

	// construct mesh data
	FLoadedMeshData MeshData = ConstructMeshData(*AiScene);

	// return mesh data
	return MeshData;
}

#pragma region        definitions of static functions
static constexpr auto AiImportFlags =
    aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
    aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals |
    aiProcess_OptimizeMeshes | aiProcess_RemoveRedundantMaterials |
    aiProcess_ImproveCacheLocality | aiProcess_FindInvalidData |
    aiProcess_EmbedTextures | aiProcess_GenUVCoords |
    aiProcess_TransformUVCoords | aiProcess_MakeLeftHanded | aiProcess_FlipUVs;

static const aiScene* LoadAiScene(Assimp::Importer& AiImporter,
                                  const FString&    FilePath) {
	// import
	return AiImporter.ReadFile(TCHAR_TO_UTF8(*FilePath), AiImportFlags);
}

static const aiScene* LoadAiScene(Assimp::Importer&    AiImporter,
                                  const TArray<uint8>& AssetData) {
	if (AssetData.IsEmpty()) {
		return nullptr;
	}
	// import
	return AiImporter.ReadFileFromMemory(&AssetData[0], AssetData.Num(),
	                                     AiImportFlags);
}

static FLoadedMeshData ConstructMeshData(const aiScene& AiScene) {
	// Transform the coordinate system of Ai(Assimp) Scene to the UE coordinate
	// system.
	TransformToUECoordinateSystem(AiScene);

	// output mesh data
	FLoadedMeshData MeshData;

	// make a list of materials
	MeshData.MaterialList = GenerateMaterialList(AiScene);

	// construct mesh data from Root Node
	ConstructMeshData(AiScene, *AiScene.mRootNode, /*out*/ MeshData, -1);

	// return mesh data
	return MeshData;
}

static void TransformToUECoordinateSystem(const aiScene& AiScene) {
	// Generate a transformation matrix to transform from
	// the Ai(Assimp) coordinate system to the UE coordinate system.
	const auto& Ai_UE_XformMatrix = GenerateAi_UE_XformMatrix(AiScene);

	// get assimp root node
	auto& AiRootNode = AiScene.mRootNode;

	// get root node's transformation ref
	auto& AiRootNodeXForm = AiRootNode->mTransformation;

	// override assimp root node's transform
	AiRootNodeXForm = Ai_UE_XformMatrix * AiRootNodeXForm;
}

static float GetAiUnitScaleFactor(const aiScene& AiScene) {
	// get meta data of AiScene
	const auto& AiMetaData = AiScene.mMetaData;

	// if scene doesn't have meta data
	if (nullptr == AiMetaData) {
		return 1.0f;
	}

	// if scene has meta data, try to get meta data "UnitScaleFactor"
	float MetaDataUnitScaleFactor;
	bool  HasUnitScaleFactor =
	    AiMetaData->Get("UnitScaleFactor", /* out */ MetaDataUnitScaleFactor);

	// if there is not meta data "UnitScaleFactor"
	if (!HasUnitScaleFactor) {
		return 1.0f;
	}

	// if there is meta data "UnitScaleFactor", return it.
	return MetaDataUnitScaleFactor;
}

static aiMatrix4x4t<float> GenerateAi_UE_XformMatrix(const aiScene& AiScene) {
	// get AiUnitScaleFactor
	const float& AiUnitScaleFactor = GetAiUnitScaleFactor(AiScene);

	// Generate scaling matrix to convert from Assimp units to UE units
	aiMatrix4x4t<float> Scale_Ai_UE;
	aiMatrix4x4t<float>::Scaling(aiVector3t<float>(AiUnitScaleFactor),
	                             Scale_Ai_UE);

	// Generate a rotation matrix to convert from YUp in Assimp to ZUp in UE
	aiMatrix4x4t<float> Rot_AiYUp_UEZUp;
	aiMatrix4x4t<float>::RotationX(PI / 2.0f, Rot_AiYUp_UEZUp);

	return Scale_Ai_UE * Rot_AiYUp_UEZUp;
}

static TArray<FLoadedMaterialData>
    GenerateMaterialList(const aiScene& AiScene) {
	TArray<FLoadedMaterialData> MaterialList;
	const auto&                 NumMaterials = AiScene.mNumMaterials;
	MaterialList.AddDefaulted(NumMaterials);

	if (0 == NumMaterials) {
		UE_LOG(LogAssetLoader, Display, TEXT("There is no Materials."));
	}
	for (auto i = decltype(NumMaterials){0}; i < NumMaterials; ++i) {
		// get reference of the material
		auto& MaterialData = MaterialList[i];

		// get ai(assimp) material
		const auto& AiMaterial = AiScene.mMaterials[i];

		// get number of textures
		const auto& NumTexture = AiMaterial->GetTextureCount(aiTextureType_DIFFUSE);

		// maybe, in case Vector4D Color is set
		if (0 == NumTexture) {
			// log that no texture is found
			UE_LOG(LogAssetLoader, Log,
			       TEXT("No texture is found for material in index %d"), i);

			// set ColorStatus that color is set
			MaterialData.ColorStatus = EColorStatus::ColorIsSet;

			aiColor4D   AiDiffuse;
			const auto& GetDiffuseResult =
			    AiMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, AiDiffuse);
			switch (GetDiffuseResult) {
			case aiReturn_FAILURE:
				UE_LOG(LogAssetLoader, Error,
				       TEXT("No color is set for material in index %d"), i);
				break;
			case aiReturn_OUTOFMEMORY:
				UE_LOG(LogAssetLoader, Error,
				       TEXT("Color couldn't get due to out of memory"));
				break;
			default:
				verifyf(aiReturn_SUCCESS == GetDiffuseResult,
				        TEXT("Bug. GetDiffuseResult should be aiReturn_SUCCESS."));

				MaterialData.Color =
				    FLinearColor{AiDiffuse.r, AiDiffuse.g, AiDiffuse.b, AiDiffuse.a};
				break;
			}
		}
		// if texture is set
		else {
			verifyf(NumTexture == 1,
			        TEXT("Currently, only one texture is supported "
			             "for diffuse (%d textures are found for material in "
			             "index %d)"),
			        NumTexture, i);

			aiString    AiTexture0Path;
			const auto& AiGetTextureResult = AiMaterial->Get(
			    AI_MATKEY_TEXTURE(aiTextureType_DIFFUSE, 0), AiTexture0Path);
			switch (AiGetTextureResult) {
			case aiReturn_FAILURE:
				// log
				UE_LOG(LogAssetLoader, Error,
				       TEXT("Failed to get texture for material in index %d"), i);

				// set ColorStatus as error
				MaterialData.ColorStatus = EColorStatus::TextureWasSetButError;
				break;
			case aiReturn_OUTOFMEMORY:
				// log
				UE_LOG(LogAssetLoader, Error,
				       TEXT("Failed to get texture due to out of memory"));

				// set ColorStatus as error
				MaterialData.ColorStatus = EColorStatus::TextureWasSetButError;
				break;
			default:
				verifyf(aiReturn_SUCCESS == AiGetTextureResult,
				        TEXT("Bug. AiGetTextureResult should be aiReturn_SUCCESS."));

				// set ColorStatus that texture is set
				MaterialData.ColorStatus = EColorStatus::TextureIsSet;

				const auto& AiTexture0 =
				    AiScene.GetEmbeddedTexture(AiTexture0Path.C_Str());

				if (nullptr == AiTexture0) {
					// TODO: load from file
					UE_LOG(LogAssetLoader, Error,
					       TEXT("Texture %s is not embedded in the file and "
					            "cannot be read."),
					       UTF8_TO_TCHAR(AiTexture0Path.C_Str()));
				} else {
					// get width and height
					const auto& Width  = AiTexture0->mWidth;
					const auto& Height = AiTexture0->mHeight;

					// if NOT compressed data
					if (Height != 0) {
						TArray64<uint8> CompressedTextureData;

						FImageView ImageView(AiTexture0->pcData, Width, Height,
						                     ERawImageFormat::BGRA8);
						FImageUtils::CompressImage(CompressedTextureData, TEXT("png"),
						                           ImageView);

						MaterialData.CompressedTextureData =
						    MoveTemp(CompressedTextureData);
					}
					// if compressed data
					else {
						// when AiTexture0 is compressed
						const auto& Size = AiTexture0->mWidth;
						const auto& SeqData =
						    reinterpret_cast<const uint8*>(AiTexture0->pcData);

						MaterialData.CompressedTextureData =
						    decltype(MaterialData.CompressedTextureData)(SeqData, Size);
					}
				}

				break;
			}
		}

		verifyf(MaterialData.ColorStatus != EColorStatus::None,
		        TEXT("Bug. Color status was not set in index %d."), i);

		MaterialList[i] = MaterialData;
	}

	return MaterialList;
}

static void ConstructMeshData(const aiScene& AiScene, const aiNode& AiNode,
                              FLoadedMeshData& MeshData, int ParentNodeIndex) {
	// create node
	FLoadedMeshNode Node;

	// set index of parent node
	Node.ParentNodeIndex = ParentNodeIndex;

	// get/set node name
	const auto&   AiNodeName = AiNode.mName;
	const FString NodeName   = UTF8_TO_TCHAR(AiNodeName.C_Str());
	Node.Name                = NodeName;

	// get/set RelativeTransform
	const auto& AiTransformMatrix = AiNode.mTransformation;
	Node.RelativeTransform =
	    static_cast<FTransform>(AiMatrixToUEMatrix(AiTransformMatrix));

	// get number of mesh sections
	const auto& NumMeshes = AiNode.mNumMeshes;

	// reserve capacity of array
	Node.Sections.AddDefaulted(NumMeshes);

	// for each sections
	for (auto i = decltype(NumMeshes){0}; i < NumMeshes; ++i) {
		// get assimp mesh
		const auto& AiMeshIndex = AiNode.mMeshes[i];
		const auto& AiMesh      = AiScene.mMeshes[AiMeshIndex];

		// get reference of the section
		auto& Section = Node.Sections[i];

		// convert to unreal Vertex format
		Section.Vertices = [&AiMesh, MeshIndex = i, NodeName]() {
			TArray<FVector> Vertices;
			const auto&     NumVertices = AiMesh->mNumVertices;
			Vertices.AddUninitialized(NumVertices);
			const auto& AiVertices = AiMesh->mVertices;

			if (!AiMesh->HasPositions()) {
				UE_LOG(LogAssetLoader, Display,
				       TEXT("There is no Vertices in index %d in %s."), MeshIndex,
				       *NodeName);
			} else {
				check(NumVertices > 0 && AiVertices != nullptr);
				for (auto i = decltype(NumVertices){0}; i < NumVertices; ++i) {
					const auto& AiVertex = AiVertices[i];
					Vertices[i]          = {AiVertex.x, AiVertex.y, AiVertex.z};
				}
			}

			return Vertices;
		}();

		// convert to unreal Triangle format
		Section.Triangles = [&AiMesh, MeshIndex = i, NodeName]() {
			TArray<int32> Triangles;
			const auto&   NumFaces = AiMesh->mNumFaces;
			const auto&   AiFaces  = AiMesh->mFaces;

			if (!AiMesh->HasFaces()) {
				UE_LOG(LogAssetLoader, Display,
				       TEXT("There is no Faces in index %d in %s."), MeshIndex,
				       *NodeName);
			} else {
				check(NumFaces > 0 && AiFaces != nullptr);

				Triangles.AddUninitialized(NumFaces * 3);
				for (auto i = decltype(NumFaces){0}; i < NumFaces; ++i) {
					const auto& AiFace = AiFaces[i];
					checkf(AiFace.mNumIndices == 3,
					       TEXT("Each face must be triangular."));

					for (int_fast8_t triangle_i = 0; triangle_i < 3; ++triangle_i) {
						Triangles[3 * i + triangle_i] = AiFace.mIndices[triangle_i];
					}
				}
			}

			return Triangles;
		}();

		// convert to unreal Normal format
		Section.Normals = [&AiMesh, MeshIndex = i, NodeName]() {
			TArray<FVector> Normals;
			const auto&     NumNormals =
			    AiMesh->mNumVertices; // num of Normals == num of Vertices
			Normals.AddUninitialized(NumNormals);
			const auto& AiNormals = AiMesh->mNormals;

			if (!AiMesh->HasNormals()) {
				UE_LOG(LogAssetLoader, Display,
				       TEXT("There is no Normal data in index %d in %s."), MeshIndex,
				       *NodeName);
			} else {
				check(NumNormals > 0 && AiNormals != nullptr);
				for (auto i = decltype(NumNormals){0}; i < NumNormals; ++i) {
					const auto& AiNormal = AiNormals[i];
					Normals[i]           = {AiNormal.x, AiNormal.y, AiNormal.z};
				}
			}

			return Normals;
		}();

		// convert to unreal UV0 format
		Section.UV0Channel = [&AiMesh, MeshIndex = i, NodeName]() {
			TArray<FVector2D> UV0Channel;
			const auto&       NumVertices = AiMesh->mNumVertices;
			UV0Channel.AddUninitialized(NumVertices);
			const auto& AiUVChannels = AiMesh->mTextureCoords;

			const auto& NumUVChannels = AiMesh->GetNumUVChannels();

			// if there is no UV Channels
			if (!AiMesh->HasTextureCoords(0)) {
				// log
				UE_LOG(LogAssetLoader, Log,
				       TEXT("There is no UV channels in index %d in %s."), MeshIndex,
				       *NodeName);
			} else {
				check(NumUVChannels > 0 && AiUVChannels != nullptr);
				ensureMsgf(
				    1 == NumUVChannels,
				    TEXT("Currently only 1 UV channel is supported in index %d in %s."),
				    MeshIndex, *NodeName);

				const auto& AiUV0Channel = AiUVChannels[0];
				if (0 == NumVertices || nullptr == AiUV0Channel) {
					check(0 == NumVertices && nullptr == AiUV0Channel);
					// log
					UE_LOG(LogAssetLoader, Warning,
					       TEXT("The first UV channel exists but there is no vertex or "
					            "channel "
					            "data in index %d in %s."),
					       MeshIndex, *NodeName);
				} else {
					for (auto i = decltype(NumVertices){0}; i < NumVertices; ++i) {
						const auto& AiUV0 = AiUV0Channel[i];
						UV0Channel[i]     = {AiUV0.x, AiUV0.y};
					}
				}
			}

			return UV0Channel;
		}();

		// convert to unreal Vertex Color format
		Section.VertexColors0 = [&AiMesh, MeshIndex = i, NodeName]() {
			TArray<FLinearColor> VertexColors0;
			const auto&          NumVertices = AiMesh->mNumVertices;
			VertexColors0.AddUninitialized(NumVertices);
			const auto& AiVertexColors = AiMesh->mColors;

			const auto& NumVertexColorChannels = AiMesh->GetNumColorChannels();

			// if there is no Vertex Color Channels
			if (!AiMesh->HasVertexColors(0)) {
				// log
				UE_LOG(LogAssetLoader, Verbose,
				       TEXT("There is no Vertex Color channels in index %d in %s."),
				       MeshIndex, *NodeName);
			} else {
				check(NumVertexColorChannels > 0 && AiVertexColors != nullptr);
				ensureMsgf(1 == NumVertexColorChannels,
				           TEXT("Currently only 1 Vertex Color channel is supported in "
				                "index %d in %s."),
				           MeshIndex, *NodeName);

				const auto& AiVertexColors0 = AiVertexColors[0];
				if (0 == NumVertices || nullptr == AiVertexColors0) {
					check(0 == NumVertices && nullptr == AiVertexColors0);
					// log
					UE_LOG(LogAssetLoader, Warning,
					       TEXT("The first Vertex Color channel exists but there is no "
					            "vertex or "
					            "channel data in index %d in %s."),
					       MeshIndex, *NodeName);
				} else {
					for (auto i = decltype(NumVertices){0}; i < NumVertices; ++i) {
						const auto& AiVertexColor = AiVertexColors0[i];
						VertexColors0[i]          = {AiVertexColor.r, AiVertexColor.g,
						                             AiVertexColor.b, AiVertexColor.a};
					}
				}
			}

			return VertexColors0;
		}();

		// convert to unreal Tangent format
		Section.Tangents = [&AiMesh, MeshIndex = i, NodeName]() {
			TArray<FProcMeshTangent> Tangents;
			const auto&              NumTangents =
			    AiMesh->mNumVertices; // num of Tangents == num of Vertices
			Tangents.AddUninitialized(NumTangents);
			const auto& AiTangents = AiMesh->mTangents;

			if (!AiMesh->HasTangentsAndBitangents()) {
				UE_LOG(LogAssetLoader, Display,
				       TEXT("There is no Tangent data in index %d in %s."), MeshIndex,
				       *NodeName);
			} else {
				check(NumTangents > 0 && AiTangents != nullptr);
				for (auto i = decltype(NumTangents){0}; i < NumTangents; ++i) {
					const auto& AiTangent = AiTangents[i];
					Tangents[i]           = {AiTangent.x, AiTangent.y, AiTangent.z};
				}
			}

			return Tangents;
		}();

		// set Material
		Section.MaterialIndex = AiMesh->mMaterialIndex;
	}

	// add node to node list (MeshData.NodeList)
	MeshData.NodeList.Add(MoveTemp(Node));

	// get this node's index
	const auto NodeIndex = MeshData.NodeList.Num() - 1;

	// Recursively add children's mesh nodes
	const auto& NumChildren = AiNode.mNumChildren;
	for (auto i = decltype(NumChildren){0}; i < NumChildren; ++i) {
		// get assimp child Node
		const auto& AiChildNode = *AiNode.mChildren[i];

		// construct mesh data
		ConstructMeshData(AiScene, AiChildNode, MeshData, NodeIndex);
	}
}

static FMatrix AiMatrixToUEMatrix(const aiMatrix4x4& AiMatrix4x4) {
	// give a short name
	const auto& M = AiMatrix4x4;

	// return transpose of assimp matrix
	return {{M.a1, M.b1, M.c1, M.d1},
	        {M.a2, M.b2, M.c2, M.d2},
	        {M.a3, M.b3, M.c3, M.d3},
	        {M.a4, M.b4, M.c4, M.d4}};
}
#pragma endregion
