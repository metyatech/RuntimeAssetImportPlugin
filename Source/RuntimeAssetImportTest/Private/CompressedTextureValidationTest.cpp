// Copyright (c) 2026 metyatech. All rights reserved.

#include "AssetConstructor.h"
#include "AssetImportLimits.h"
#include "AssetLoader.h"
#include "CompressedTextureValidation.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Tests/AutomationEditorCommon.h"

namespace
{
    FString ResolveTextureTestAssetPath(const TCHAR *RelativePath)
    {
        return FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("RuntimeAssetImport"),
                               TEXT("Source/RuntimeAssetImportTest/TestAssets"), RelativePath);
    }

    uint32 CalculatePngCrc32(const uint8 *Data, const int32 ByteCount)
    {
        uint32 Crc = 0xffffffffu;
        for (int32 ByteIndex = 0; ByteIndex < ByteCount; ++ByteIndex)
        {
            Crc ^= Data[ByteIndex];
            for (int32 BitIndex = 0; BitIndex < 8; ++BitIndex)
            {
                const uint32 Mask = 0u - (Crc & 1u);
                Crc = (Crc >> 1u) ^ (0xedb88320u & Mask);
            }
        }
        return ~Crc;
    }

    void WriteBigEndianUint32(TArray<uint8> &Bytes, const int32 Offset, const uint32 Value)
    {
        Bytes[Offset] = static_cast<uint8>(Value >> 24u);
        Bytes[Offset + 1] = static_cast<uint8>(Value >> 16u);
        Bytes[Offset + 2] = static_cast<uint8>(Value >> 8u);
        Bytes[Offset + 3] = static_cast<uint8>(Value);
    }

    bool LoadRedPng(TArray<uint8> &OutBytes)
    {
        return FFileHelper::LoadFileToArray(OutBytes, *ResolveTextureTestAssetPath(TEXT("textures/test_red.png")));
    }

    bool MakePngWithDimensions(const uint32 Width, const uint32 Height, TArray<uint8> &OutBytes)
    {
        if (!LoadRedPng(OutBytes) || OutBytes.Num() < 33)
        {
            return false;
        }

        static constexpr uint8 ExpectedSignature[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
        if (FMemory::Memcmp(OutBytes.GetData(), ExpectedSignature, UE_ARRAY_COUNT(ExpectedSignature)) != 0 ||
            FMemory::Memcmp(OutBytes.GetData() + 12, "IHDR", 4) != 0)
        {
            return false;
        }

        WriteBigEndianUint32(OutBytes, 16, Width);
        WriteBigEndianUint32(OutBytes, 20, Height);
        WriteBigEndianUint32(OutBytes, 29, CalculatePngCrc32(OutBytes.GetData() + 12, 17));
        return true;
    }

    bool MakeEmbeddedTextureGltf(const TArray<uint8> &PngBytes, TArray<uint8> &OutGltfBytes)
    {
        FString GltfText;
        TArray<uint8> OriginalPngBytes;
        if (!FFileHelper::LoadFileToString(GltfText,
                                           *ResolveTextureTestAssetPath(TEXT("test_embedded_texture.gltf"))) ||
            !LoadRedPng(OriginalPngBytes))
        {
            return false;
        }

        const FString OriginalBase64 = FBase64::Encode(OriginalPngBytes);
        const FString ReplacementBase64 = FBase64::Encode(PngBytes);
        if (GltfText.ReplaceInline(*OriginalBase64, *ReplacementBase64, ESearchCase::CaseSensitive) != 1)
        {
            return false;
        }

        FTCHARToUTF8 Utf8Gltf(*GltfText);
        OutGltfBytes.Reset(Utf8Gltf.Length());
        OutGltfBytes.Append(reinterpret_cast<const uint8 *>(Utf8Gltf.Get()), Utf8Gltf.Length());
        return true;
    }

    FLoadedMeshData MakeTextureTriangleMeshData(const TArray<uint8> &TextureBytes)
    {
        FLoadedMeshData MeshData;
        FLoadedMaterialData Material;
        Material.ColorStatus = EColorStatus::TextureIsSet;
        Material.CompressedTextureData = TextureBytes;
        MeshData.MaterialList.Add(Material);

        FLoadedMeshNode Node;
        Node.Name = TEXT("Root");
        Node.ParentNodeIndex = INDEX_NONE;
        Node.RelativeTransform = FTransform::Identity;

        FLoadedMeshSectionData Section;
        Section.Vertices = {FVector(0.0, 0.0, 0.0), FVector(100.0, 0.0, 0.0), FVector(0.0, 100.0, 0.0)};
        Section.Triangles = {0, 1, 2};
        Section.Normals = {FVector::UpVector, FVector::UpVector, FVector::UpVector};
        Section.UV0Channel = {FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), FVector2D(0.0, 1.0)};
        Section.VertexColors0 = {FLinearColor::White, FLinearColor::White, FLinearColor::White};
        Section.Tangents = {FProcMeshTangent(FVector::ForwardVector, false),
                            FProcMeshTangent(FVector::ForwardVector, false),
                            FProcMeshTangent(FVector::ForwardVector, false)};
        Section.MaterialIndex = 0;
        Node.Sections.Add(Section);
        MeshData.NodeList.Add(Node);
        return MeshData;
    }

    UMaterialInterface *LoadTextureTestParentMaterial()
    {
        return LoadObject<UMaterialInterface>(
            nullptr, TEXT("/RuntimeAssetImport/AssetImporterMeshMaterial.AssetImporterMeshMaterial"));
    }

    bool TestBothTextureConstructorsReject(FAutomationTestBase &Test, const FString &Label,
                                           const TArray<uint8> &TextureBytes)
    {
        UWorld *World = FAutomationEditorCommonUtils::CreateNewMap();
        AActor *Owner = World != nullptr ? World->SpawnActor<AActor>() : nullptr;
        UMaterialInterface *ParentMaterial = LoadTextureTestParentMaterial();
        if (World == nullptr || Owner == nullptr || ParentMaterial == nullptr)
        {
            Test.AddError(Label + TEXT(" could not create its world, owner, or Parent Material."));
            return false;
        }

        const FLoadedMeshData MeshData = MakeTextureTriangleMeshData(TextureBytes);
        const int32 InitialComponentCount = Owner->GetComponents().Num();
        Test.AddExpectedError(TEXT("LogAssetConstructor:"), EAutomationExpectedErrorFlags::Contains, 2);
        bool Passed = Test.TestNull(
            *(Label + TEXT(" ProceduralMesh should reject the texture")),
            UAssetConstructor::ConstructProceduralMeshComponentFromMeshData(MeshData, ParentMaterial, Owner));
        Passed &= Test.TestEqual(*(Label + TEXT(" ProceduralMesh should not leak a component")),
                                 Owner->GetComponents().Num(), InitialComponentCount);
        Passed &= Test.TestNull(
            *(Label + TEXT(" DynamicMesh should reject the texture")),
            UAssetConstructor::ConstructDynamicMeshComponentFromMeshData(MeshData, ParentMaterial, Owner));
        Passed &= Test.TestEqual(*(Label + TEXT(" DynamicMesh should not leak a component")),
                                 Owner->GetComponents().Num(), InitialComponentCount);
        World->DestroyActor(Owner);
        return Passed;
    }

    const FLoadedMaterialData *FindTextureTestMaterial(const FLoadedMeshData &MeshData)
    {
        for (const FLoadedMeshNode &Node : MeshData.NodeList)
        {
            for (const FLoadedMeshSectionData &Section : Node.Sections)
            {
                if (MeshData.MaterialList.IsValidIndex(Section.MaterialIndex))
                {
                    return &MeshData.MaterialList[Section.MaterialIndex];
                }
            }
        }
        return nullptr;
    }

    bool TestRejectedImportedTexture(FAutomationTestBase &Test, const FString &Label, const FLoadedMeshData &MeshData)
    {
        const FLoadedMaterialData *Material = FindTextureTestMaterial(MeshData);
        bool Passed = Test.TestNotNull(*(Label + TEXT(" should have a used material")), Material);
        if (Material != nullptr)
        {
            Passed &= Test.TestEqual(*(Label + TEXT(" should report TextureWasSetButError")), Material->ColorStatus,
                                     EColorStatus::TextureWasSetButError);
            Passed &= Test.TestTrue(*(Label + TEXT(" should clear compressed texture bytes")),
                                    Material->CompressedTextureData.IsEmpty());
        }
        return Passed;
    }

    class FScopedTextureTestDirectory
    {
    public:
        FScopedTextureTestDirectory()
        {
            Directory = FPaths::Combine(FPlatformProcess::UserTempDir(),
                                        FString::Printf(TEXT("RuntimeAssetImportTexture-%s"),
                                                        *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
            bReady = IFileManager::Get().MakeDirectory(*Directory, true);
        }

        ~FScopedTextureTestDirectory() { IFileManager::Get().DeleteDirectory(*Directory, false, true); }

        bool IsReady() const { return bReady; }
        FString MakePath(const TCHAR *FileName) const { return FPaths::Combine(Directory, FileName); }

    private:
        FString Directory;
        bool bReady = false;
    };
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCompressedTextureDimensionLimitRejected,
                                 "RuntimeAssetImport.Security.CompressedTexture.DimensionLimitRejected",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCompressedTextureDimensionLimitRejected::RunTest(const FString &Parameters)
{
    TArray<uint8> PngBytes;
    if (!TestTrue(TEXT("Dimension-limit PNG should be generated"),
                  MakePngWithDimensions(RuntimeAssetImport::Limits::MaximumRawTextureDimension + 1, 1, PngBytes)))
    {
        return false;
    }

    RuntimeAssetImport::CompressedTextureValidation::FCompressedTextureMetadata Metadata;
    FString Error;
    bool Passed = TestFalse(
        TEXT("Dimension-limit PNG metadata should be rejected"),
        RuntimeAssetImport::CompressedTextureValidation::ValidateCompressedTexturePayload(PngBytes, Metadata, Error));
    Passed &= TestTrue(TEXT("Dimension denial should identify the dimension limit"),
                       Error.Contains(TEXT("dimension-limit=16384")));
    Passed &= TestBothTextureConstructorsReject(*this, TEXT("Dimension-limit PNG"), PngBytes);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCompressedTexturePixelLimitRejected,
                                 "RuntimeAssetImport.Security.CompressedTexture.PixelLimitRejected",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCompressedTexturePixelLimitRejected::RunTest(const FString &Parameters)
{
    TArray<uint8> PngBytes;
    if (!TestTrue(TEXT("Pixel-limit PNG should be generated"), MakePngWithDimensions(16384, 4097, PngBytes)))
    {
        return false;
    }

    RuntimeAssetImport::CompressedTextureValidation::FCompressedTextureMetadata Metadata;
    FString Error;
    bool Passed = TestFalse(
        TEXT("Pixel-limit PNG metadata should be rejected"),
        RuntimeAssetImport::CompressedTextureValidation::ValidateCompressedTexturePayload(PngBytes, Metadata, Error));
    Passed &=
        TestTrue(TEXT("Pixel denial should identify the pixel limit"), Error.Contains(TEXT("pixel-limit=67108864")));
    Passed &= TestBothTextureConstructorsReject(*this, TEXT("Pixel-limit PNG"), PngBytes);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCompressedTextureInvalidHeaderRejected,
                                 "RuntimeAssetImport.Security.CompressedTexture.InvalidCompressedHeaderRejected",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCompressedTextureInvalidHeaderRejected::RunTest(const FString &Parameters)
{
    const TArray<uint8> InvalidBytes = {0x52, 0x41, 0x49, 0x2d, 0x49, 0x4e, 0x56, 0x41, 0x4c, 0x49, 0x44};
    RuntimeAssetImport::CompressedTextureValidation::FCompressedTextureMetadata Metadata;
    FString Error;
    bool Passed = TestFalse(TEXT("Invalid compressed header should be rejected"),
                            RuntimeAssetImport::CompressedTextureValidation::ValidateCompressedTexturePayload(
                                InvalidBytes, Metadata, Error));
    Passed &= TestTrue(TEXT("Invalid header denial should identify the invalid format"),
                       Error.Contains(TEXT("format=Invalid")));
    Passed &= TestBothTextureConstructorsReject(*this, TEXT("Invalid compressed header"), InvalidBytes);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCompressedTextureFileImportOversizedEmbeddedRejected,
    "RuntimeAssetImport.Security.CompressedTexture.FileImportOversizedEmbeddedTextureRejectedBeforeDecode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCompressedTextureFileImportOversizedEmbeddedRejected::RunTest(const FString &Parameters)
{
    TArray<uint8> PngBytes;
    TArray<uint8> GltfBytes;
    FScopedTextureTestDirectory TemporaryDirectory;
    const FString GltfPath = TemporaryDirectory.MakePath(TEXT("oversized.gltf"));
    if (!TestTrue(TEXT("Temporary texture directory should initialize"), TemporaryDirectory.IsReady()) ||
        !TestTrue(TEXT("Oversized PNG should be generated"),
                  MakePngWithDimensions(RuntimeAssetImport::Limits::MaximumRawTextureDimension + 1, 1, PngBytes)) ||
        !TestTrue(TEXT("Oversized embedded glTF should be generated"), MakeEmbeddedTextureGltf(PngBytes, GltfBytes)) ||
        !TestTrue(TEXT("Oversized embedded glTF should be saved"), FFileHelper::SaveArrayToFile(GltfBytes, *GltfPath)))
    {
        return false;
    }

    AddExpectedError(TEXT("compressed texture was rejected"), EAutomationExpectedErrorFlags::Contains, 1);
    ELoadMeshFromAssetFileResult Result = ELoadMeshFromAssetFileResult::Failure;
    const FLoadedMeshData MeshData = UAssetLoader::LoadMeshFromAssetFile(GltfPath, Result);
    bool Passed = TestEqual(TEXT("Oversized embedded texture file should preserve geometry import"), Result,
                            ELoadMeshFromAssetFileResult::Success);
    Passed &= TestRejectedImportedTexture(*this, TEXT("Oversized embedded texture file"), MeshData);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCompressedTextureMemoryImportOversizedEmbeddedRejected,
    "RuntimeAssetImport.Security.CompressedTexture.MemoryImportOversizedEmbeddedTextureRejectedBeforeDecode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCompressedTextureMemoryImportOversizedEmbeddedRejected::RunTest(const FString &Parameters)
{
    TArray<uint8> PngBytes;
    TArray<uint8> GltfBytes;
    if (!TestTrue(TEXT("Oversized PNG should be generated"),
                  MakePngWithDimensions(RuntimeAssetImport::Limits::MaximumRawTextureDimension + 1, 1, PngBytes)) ||
        !TestTrue(TEXT("Oversized embedded glTF should be generated"), MakeEmbeddedTextureGltf(PngBytes, GltfBytes)))
    {
        return false;
    }

    AddExpectedError(TEXT("compressed texture was rejected"), EAutomationExpectedErrorFlags::Contains, 1);
    ELoadMeshFromAssetDataResult Result = ELoadMeshFromAssetDataResult::Failure;
    const FLoadedMeshData MeshData = UAssetLoader::LoadMeshFromAssetData(GltfBytes, Result);
    bool Passed = TestEqual(TEXT("Oversized embedded texture memory input should preserve geometry import"), Result,
                            ELoadMeshFromAssetDataResult::Success);
    Passed &= TestRejectedImportedTexture(*this, TEXT("Oversized embedded texture memory input"), MeshData);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCompressedTextureValidRegression,
                                 "RuntimeAssetImport.Security.CompressedTexture.ValidCompressedTextureStillWorks",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCompressedTextureValidRegression::RunTest(const FString &Parameters)
{
    TArray<uint8> RedPngBytes;
    if (!TestTrue(TEXT("Valid red PNG should load"), LoadRedPng(RedPngBytes)))
    {
        return false;
    }

    RuntimeAssetImport::CompressedTextureValidation::FCompressedTextureMetadata Metadata;
    FString Error;
    bool Passed = TestTrue(TEXT("Valid red PNG metadata should pass"),
                           RuntimeAssetImport::CompressedTextureValidation::ValidateCompressedTexturePayload(
                               RedPngBytes, Metadata, Error));
    Passed &= TestEqual(TEXT("Valid red PNG width"), Metadata.Width, static_cast<int64>(1));
    Passed &= TestEqual(TEXT("Valid red PNG height"), Metadata.Height, static_cast<int64>(1));

    ELoadMeshFromAssetFileResult ExternalResult = ELoadMeshFromAssetFileResult::Failure;
    const FLoadedMeshData ExternalData = UAssetLoader::LoadMeshFromAssetFile(
        ResolveTextureTestAssetPath(TEXT("test_external_texture.obj")), ExternalResult);
    Passed &= TestEqual(TEXT("Valid external texture import should succeed"), ExternalResult,
                        ELoadMeshFromAssetFileResult::Success);
    const FLoadedMaterialData *ExternalMaterial = FindTextureTestMaterial(ExternalData);
    Passed &= TestTrue(TEXT("Valid external texture should remain TextureIsSet"),
                       ExternalMaterial != nullptr && ExternalMaterial->ColorStatus == EColorStatus::TextureIsSet);

    const FString EmbeddedPath = ResolveTextureTestAssetPath(TEXT("test_embedded_texture.gltf"));
    ELoadMeshFromAssetFileResult EmbeddedFileResult = ELoadMeshFromAssetFileResult::Failure;
    const FLoadedMeshData EmbeddedFileData = UAssetLoader::LoadMeshFromAssetFile(EmbeddedPath, EmbeddedFileResult);
    Passed &= TestEqual(TEXT("Valid embedded texture file import should succeed"), EmbeddedFileResult,
                        ELoadMeshFromAssetFileResult::Success);
    const FLoadedMaterialData *EmbeddedFileMaterial = FindTextureTestMaterial(EmbeddedFileData);
    Passed &=
        TestTrue(TEXT("Valid embedded texture file should remain TextureIsSet"),
                 EmbeddedFileMaterial != nullptr && EmbeddedFileMaterial->ColorStatus == EColorStatus::TextureIsSet);

    TArray<uint8> EmbeddedBytes;
    ELoadMeshFromAssetDataResult EmbeddedMemoryResult = ELoadMeshFromAssetDataResult::Failure;
    FLoadedMeshData EmbeddedMemoryData;
    if (TestTrue(TEXT("Valid embedded texture bytes should load"),
                 FFileHelper::LoadFileToArray(EmbeddedBytes, *EmbeddedPath)))
    {
        EmbeddedMemoryData = UAssetLoader::LoadMeshFromAssetData(EmbeddedBytes, EmbeddedMemoryResult);
    }
    Passed &= TestEqual(TEXT("Valid embedded texture memory import should succeed"), EmbeddedMemoryResult,
                        ELoadMeshFromAssetDataResult::Success);
    const FLoadedMaterialData *EmbeddedMemoryMaterial = FindTextureTestMaterial(EmbeddedMemoryData);
    Passed &= TestTrue(TEXT("Valid embedded texture memory should remain TextureIsSet"),
                       EmbeddedMemoryMaterial != nullptr &&
                           EmbeddedMemoryMaterial->ColorStatus == EColorStatus::TextureIsSet);
    return Passed;
}
