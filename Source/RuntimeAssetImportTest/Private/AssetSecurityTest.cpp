// Copyright (c) 2026 metyatech. All rights reserved.

#include "AssetImportLimits.h"
#include "AssetLoader.h"
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RestrictedAssimpIOSystem.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"

#include <winioctl.h>

#include "Windows/HideWindowsPlatformTypes.h"

namespace
{
    constexpr const TCHAR *TriangleObj = TEXT("mtllib model.mtl\n") TEXT("v 0 0 0\n") TEXT("v 1 0 0\n")
        TEXT("v 0 1 0\n") TEXT("vt 0 0\n") TEXT("vt 1 0\n") TEXT("vt 0 1\n") TEXT("vn 0 0 1\n")
            TEXT("usemtl TestMaterial\n") TEXT("f 1/1/1 2/2/1 3/3/1\n");

    struct FMountPointReparseBuffer
    {
        ULONG ReparseTag;
        USHORT ReparseDataLength;
        USHORT Reserved;
        USHORT SubstituteNameOffset;
        USHORT SubstituteNameLength;
        USHORT PrintNameOffset;
        USHORT PrintNameLength;
        WCHAR PathBuffer[1];
    };

    FString GetSecurityTestAssetsDir()
    {
        return FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("RuntimeAssetImport"),
                               TEXT("Source/RuntimeAssetImportTest/TestAssets"));
    }

    bool SaveUtf8(const FString &Path, const FString &Contents)
    {
        return FFileHelper::SaveStringToFile(Contents, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    bool CreateSparseFile(const FString &Path, const uint64 Size)
    {
        HANDLE Handle = CreateFileW(*Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        if (Handle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        DWORD BytesReturned = 0;
        const bool bSparse =
            DeviceIoControl(Handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &BytesReturned, nullptr) != 0;
        LARGE_INTEGER End{};
        End.QuadPart = static_cast<LONGLONG>(Size);
        const bool bSized =
            bSparse && SetFilePointerEx(Handle, End, nullptr, FILE_BEGIN) != 0 && SetEndOfFile(Handle) != 0;
        CloseHandle(Handle);
        return bSized;
    }

    bool CreateAlternateDataStream(const FString &Path, const TArray<uint8> &Bytes)
    {
        HANDLE Handle =
            CreateFileW(*Path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (Handle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        uint64 TotalBytesWritten = 0;
        bool bSucceeded = true;
        while (TotalBytesWritten < static_cast<uint64>(Bytes.Num()))
        {
            const DWORD ChunkSize =
                static_cast<DWORD>(FMath::Min<uint64>(static_cast<uint64>(Bytes.Num()) - TotalBytesWritten, MAXDWORD));
            DWORD ChunkBytesWritten = 0;
            if (!WriteFile(Handle, Bytes.GetData() + TotalBytesWritten, ChunkSize, &ChunkBytesWritten, nullptr) ||
                ChunkBytesWritten == 0)
            {
                bSucceeded = false;
                break;
            }
            TotalBytesWritten += ChunkBytesWritten;
        }
        CloseHandle(Handle);
        return bSucceeded && TotalBytesWritten == static_cast<uint64>(Bytes.Num());
    }

    bool CreateTestHardLink(const FString &LinkPath, const FString &ExistingPath)
    {
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(LinkPath), true);
        return CreateHardLinkW(*LinkPath, *ExistingPath, nullptr) != 0;
    }

    bool CreateJunction(const FString &LinkPath, const FString &TargetPath)
    {
        if (!IFileManager::Get().MakeDirectory(*LinkPath, true))
        {
            return false;
        }

        HANDLE Handle = CreateFileW(*LinkPath, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (Handle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        FString PrintName = FPaths::ConvertRelativePathToFull(TargetPath);
        PrintName.ReplaceInline(TEXT("/"), TEXT("\\"));
        const FString SubstituteName = TEXT("\\??\\") + PrintName;
        const USHORT SubstituteBytes = static_cast<USHORT>(SubstituteName.Len() * sizeof(TCHAR));
        const USHORT PrintBytes = static_cast<USHORT>(PrintName.Len() * sizeof(TCHAR));
        const USHORT PrintOffset = static_cast<USHORT>(SubstituteBytes + sizeof(TCHAR));

        TArray<uint8> ReparseBuffer;
        ReparseBuffer.SetNumZeroed(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
        FMountPointReparseBuffer *ReparseData = reinterpret_cast<FMountPointReparseBuffer *>(ReparseBuffer.GetData());
        ReparseData->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
        ReparseData->SubstituteNameOffset = 0;
        ReparseData->SubstituteNameLength = SubstituteBytes;
        ReparseData->PrintNameOffset = PrintOffset;
        ReparseData->PrintNameLength = PrintBytes;
        FMemory::Memcpy(ReparseData->PathBuffer, *SubstituteName, SubstituteBytes);
        FMemory::Memcpy(reinterpret_cast<uint8 *>(ReparseData->PathBuffer) + PrintOffset, *PrintName, PrintBytes);

        const uint32 PathBufferBytes = PrintOffset + PrintBytes + sizeof(TCHAR);
        ReparseData->ReparseDataLength =
            static_cast<USHORT>(FIELD_OFFSET(FMountPointReparseBuffer, PathBuffer) -
                                FIELD_OFFSET(FMountPointReparseBuffer, SubstituteNameOffset) + PathBufferBytes);
        DWORD BytesReturned = 0;
        const bool bCreated = DeviceIoControl(Handle, FSCTL_SET_REPARSE_POINT, ReparseData,
                                              ReparseData->ReparseDataLength +
                                                  FIELD_OFFSET(FMountPointReparseBuffer, SubstituteNameOffset),
                                              nullptr, 0, &BytesReturned, nullptr) != 0;
        CloseHandle(Handle);
        if (!bCreated)
        {
            RemoveDirectoryW(*LinkPath);
        }
        return bCreated;
    }

    class FScopedAssetSandbox
    {
    public:
        FScopedAssetSandbox()
        {
            BasePath = FPaths::Combine(FPlatformProcess::UserTempDir(),
                                       FString::Printf(TEXT("RuntimeAssetImport-%s"), *FGuid::NewGuid().ToString()));
            RootPath = FPaths::Combine(BasePath, TEXT("model"));
            OutsidePath = FPaths::Combine(BasePath, TEXT("outside"));
            bReady = IFileManager::Get().MakeDirectory(*RootPath, true) &&
                     IFileManager::Get().MakeDirectory(*OutsidePath, true);
        }

        ~FScopedAssetSandbox()
        {
            for (const FString &HardLink : HardLinks)
            {
                DeleteFileW(*HardLink);
            }
            for (const FString &Junction : Junctions)
            {
                RemoveDirectoryW(*Junction);
            }
            IFileManager::Get().DeleteDirectory(*BasePath, false, true);
        }

        bool IsReady() const { return bReady; }
        const FString &GetRootPath() const { return RootPath; }
        const FString &GetOutsidePath() const { return OutsidePath; }

        bool AddJunction(const FString &RelativeLinkPath, const FString &TargetPath)
        {
            const FString LinkPath = FPaths::Combine(RootPath, RelativeLinkPath);
            if (!CreateJunction(LinkPath, TargetPath))
            {
                return false;
            }
            Junctions.Add(LinkPath);
            return true;
        }

        bool AddHardLink(const FString &RelativeLinkPath, const FString &ExistingPath)
        {
            const FString LinkPath = FPaths::Combine(RootPath, RelativeLinkPath);
            if (!CreateTestHardLink(LinkPath, ExistingPath))
            {
                return false;
            }
            HardLinks.Add(LinkPath);
            return true;
        }

        bool WriteRootFile(const FString &RelativePath, const FString &Contents) const
        {
            const FString Path = FPaths::Combine(RootPath, RelativePath);
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
            return SaveUtf8(Path, Contents);
        }

        bool WriteOutsideFile(const FString &RelativePath, const FString &Contents) const
        {
            const FString Path = FPaths::Combine(OutsidePath, RelativePath);
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
            return SaveUtf8(Path, Contents);
        }

        bool CopyRedPng(const FString &DestinationPath) const
        {
            TArray<uint8> Bytes;
            const FString SourcePath = FPaths::Combine(GetSecurityTestAssetsDir(), TEXT("textures/test_red.png"));
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(DestinationPath), true);
            return FFileHelper::LoadFileToArray(Bytes, *SourcePath) &&
                   FFileHelper::SaveArrayToFile(Bytes, *DestinationPath);
        }

    private:
        FString BasePath;
        FString RootPath;
        FString OutsidePath;
        TArray<FString> HardLinks;
        TArray<FString> Junctions;
        bool bReady = false;
    };

    class FScopedProcessCurrentDirectory
    {
    public:
        explicit FScopedProcessCurrentDirectory(const FString &NewDirectory)
        {
            const DWORD RequiredCharacters = GetCurrentDirectoryW(0, nullptr);
            if (RequiredCharacters == 0)
            {
                return;
            }

            TArray<WCHAR> Buffer;
            Buffer.SetNumUninitialized(static_cast<int32>(RequiredCharacters));
            if (GetCurrentDirectoryW(RequiredCharacters, Buffer.GetData()) == 0)
            {
                return;
            }

            OriginalDirectory = Buffer.GetData();
            bChanged = SetCurrentDirectoryW(*NewDirectory) != 0;
        }

        ~FScopedProcessCurrentDirectory()
        {
            if (bChanged)
            {
                SetCurrentDirectoryW(*OriginalDirectory);
            }
        }

        bool IsChanged() const { return bChanged; }

    private:
        FString OriginalDirectory;
        bool bChanged = false;
    };

    bool PrepareObjWithTexture(FScopedAssetSandbox &Sandbox, const FString &TextureReference)
    {
        return Sandbox.WriteRootFile(TEXT("model.obj"), TriangleObj) &&
               Sandbox.WriteRootFile(TEXT("model.mtl"), FString::Printf(TEXT("newmtl TestMaterial\n") TEXT(
                                                                            "Kd 0.0 1.0 0.0\n") TEXT("map_Kd %s\n"),
                                                                        *TextureReference));
    }

    const FLoadedMaterialData *FindSecurityFirstUsedMaterial(const FLoadedMeshData &MeshData)
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

    FLoadedMeshData LoadSandboxObj(FScopedAssetSandbox &Sandbox, ELoadMeshFromAssetFileResult &OutResult)
    {
        return UAssetLoader::LoadMeshFromAssetFile(FPaths::Combine(Sandbox.GetRootPath(), TEXT("model.obj")),
                                                   OutResult);
    }

    bool AssertNoExternalTexture(FAutomationTestBase &Test, const FString &Label, const FLoadedMeshData &MeshData)
    {
        for (const FLoadedMaterialData &Material : MeshData.MaterialList)
        {
            if (Material.ColorStatus == EColorStatus::TextureIsSet || !Material.CompressedTextureData.IsEmpty())
            {
                Test.AddError(Label + TEXT(" unexpectedly imported external texture data."));
                return false;
            }
        }
        return true;
    }

    Assimp::IOStream *OpenRestricted(FRestrictedAssimpIOSystem &IOSystem, const FString &Path)
    {
        FTCHARToUTF8 Utf8Path(*Path);
        return IOSystem.Open(Utf8Path.Get(), "rb");
    }

    bool ExistsRestricted(FRestrictedAssimpIOSystem &IOSystem, const FString &Path)
    {
        FTCHARToUTF8 Utf8Path(*Path);
        return IOSystem.Exists(Utf8Path.Get());
    }

    bool ReadRestrictedStream(Assimp::IOStream &Stream, TArray<uint8> &OutBytes)
    {
        const size_t FileSize = Stream.FileSize();
        if (FileSize > static_cast<size_t>(MAX_int32))
        {
            return false;
        }

        OutBytes.SetNumUninitialized(static_cast<int32>(FileSize));
        return FileSize == 0 || Stream.Read(OutBytes.GetData(), 1, FileSize) == FileSize;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestrictedAssimpIOSafeSubdirectoryAllowed,
                                 "RuntimeAssetImport.Security.Sandbox.SafeSubdirectoryAllowed",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedAssimpIOSafeSubdirectoryAllowed::RunTest(const FString &Parameters)
{
    FScopedAssetSandbox Sandbox;
    if (!TestTrue(TEXT("Sandbox should initialize"), Sandbox.IsReady()))
    {
        return false;
    }
    const FString TexturePath = FPaths::Combine(Sandbox.GetRootPath(), TEXT("textures/red.png"));
    if (!TestTrue(TEXT("Red PNG should copy"), Sandbox.CopyRedPng(TexturePath)) ||
        !TestTrue(TEXT("OBJ and MTL should be written"), PrepareObjWithTexture(Sandbox, TEXT("textures/red.png"))))
    {
        return false;
    }

    FRestrictedAssimpIOSystem DirectIO(FPaths::Combine(Sandbox.GetRootPath(), TEXT("model.obj")));
    Assimp::IOStream *AbsoluteInsideRootStream = OpenRestricted(DirectIO, TexturePath);
    bool Passed = TestTrue(TEXT("Restricted IO should initialize for the safe model"), DirectIO.IsInitialized());
    Passed &= TestNotNull(TEXT("An absolute auxiliary path inside the model root should be allowed"),
                          AbsoluteInsideRootStream);
    if (AbsoluteInsideRootStream != nullptr)
    {
        DirectIO.Close(AbsoluteInsideRootStream);
    }

    ELoadMeshFromAssetFileResult Result = ELoadMeshFromAssetFileResult::Failure;
    const FLoadedMeshData MeshData = LoadSandboxObj(Sandbox, Result);
    Passed &= TestEqual(TEXT("Safe subdirectory texture should load"), Result, ELoadMeshFromAssetFileResult::Success);
    const FLoadedMaterialData *Material = FindSecurityFirstUsedMaterial(MeshData);
    Passed &= TestNotNull(TEXT("Safe texture material should exist"), Material);
    if (Material != nullptr)
    {
        Passed &= TestEqual(TEXT("Safe texture should be set"), Material->ColorStatus, EColorStatus::TextureIsSet);
        Passed &= TestFalse(TEXT("Safe texture bytes should be present"), Material->CompressedTextureData.IsEmpty());
    }
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestrictedAssimpIORelativeEscapeDenied,
                                 "RuntimeAssetImport.Security.Sandbox.RelativeEscapeDenied",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedAssimpIORelativeEscapeDenied::RunTest(const FString &Parameters)
{
    FScopedAssetSandbox Sandbox;
    const FString OutsideTexture = FPaths::Combine(Sandbox.GetOutsidePath(), TEXT("red.png"));
    if (!TestTrue(TEXT("Sandbox should initialize"), Sandbox.IsReady()) ||
        !TestTrue(TEXT("Outside PNG should copy"), Sandbox.CopyRedPng(OutsideTexture)) ||
        !TestTrue(TEXT("Escape OBJ should be written"), PrepareObjWithTexture(Sandbox, TEXT("../outside/red.png"))))
    {
        return false;
    }

    AddExpectedError(TEXT("Restricted Assimp IO denied access"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedError(TEXT("is external and is not loaded"), EAutomationExpectedErrorFlags::Contains, 1);
    ELoadMeshFromAssetFileResult Result = ELoadMeshFromAssetFileResult::Failure;
    const FLoadedMeshData MeshData = LoadSandboxObj(Sandbox, Result);
    bool Passed =
        TestEqual(TEXT("Relative escape may still load geometry"), Result, ELoadMeshFromAssetFileResult::Success);
    Passed &= AssertNoExternalTexture(*this, TEXT("Relative escape"), MeshData);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestrictedAssimpIOAbsoluteEscapeDenied,
                                 "RuntimeAssetImport.Security.Sandbox.AbsoluteEscapeDenied",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedAssimpIOAbsoluteEscapeDenied::RunTest(const FString &Parameters)
{
    FScopedAssetSandbox Sandbox;
    const FString OutsideTexture = FPaths::Combine(Sandbox.GetOutsidePath(), TEXT("red.png"));
    const FString TextureReference = OutsideTexture.Replace(TEXT("\\"), TEXT("/"));
    if (!TestTrue(TEXT("Sandbox should initialize"), Sandbox.IsReady()) ||
        !TestTrue(TEXT("Outside PNG should copy"), Sandbox.CopyRedPng(OutsideTexture)) ||
        !TestTrue(TEXT("Absolute escape OBJ should be written"), PrepareObjWithTexture(Sandbox, TextureReference)))
    {
        return false;
    }

    AddExpectedError(TEXT("Restricted Assimp IO denied access"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedError(TEXT("is external and is not loaded"), EAutomationExpectedErrorFlags::Contains, 1);
    ELoadMeshFromAssetFileResult Result = ELoadMeshFromAssetFileResult::Failure;
    const FLoadedMeshData MeshData = LoadSandboxObj(Sandbox, Result);
    bool Passed =
        TestEqual(TEXT("Absolute escape may still load geometry"), Result, ELoadMeshFromAssetFileResult::Success);
    Passed &= AssertNoExternalTexture(*this, TEXT("Absolute escape"), MeshData);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestrictedAssimpIOJunctionEscapeDenied,
                                 "RuntimeAssetImport.Security.Sandbox.JunctionEscapeDenied",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedAssimpIOJunctionEscapeDenied::RunTest(const FString &Parameters)
{
    FScopedAssetSandbox Sandbox;
    const FString OutsideTexture = FPaths::Combine(Sandbox.GetOutsidePath(), TEXT("red.png"));
    if (!TestTrue(TEXT("Sandbox should initialize"), Sandbox.IsReady()) ||
        !TestTrue(TEXT("Outside PNG should copy"), Sandbox.CopyRedPng(OutsideTexture)) ||
        !TestTrue(TEXT("Junction should be created"), Sandbox.AddJunction(TEXT("linked"), Sandbox.GetOutsidePath())) ||
        !TestTrue(TEXT("Junction OBJ should be written"), PrepareObjWithTexture(Sandbox, TEXT("linked/red.png"))))
    {
        return false;
    }

    AddExpectedError(TEXT("Restricted Assimp IO denied access"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedError(TEXT("is external and is not loaded"), EAutomationExpectedErrorFlags::Contains, 1);
    ELoadMeshFromAssetFileResult Result = ELoadMeshFromAssetFileResult::Failure;
    const FLoadedMeshData MeshData = LoadSandboxObj(Sandbox, Result);
    bool Passed =
        TestEqual(TEXT("Junction escape may still load geometry"), Result, ELoadMeshFromAssetFileResult::Success);
    Passed &= AssertNoExternalTexture(*this, TEXT("Junction escape"), MeshData);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestrictedAssimpIOCanonicalEscapePathsDenied,
                                 "RuntimeAssetImport.Security.Sandbox.CanonicalEscapePathsDenied",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedAssimpIOCanonicalEscapePathsDenied::RunTest(const FString &Parameters)
{
    FScopedAssetSandbox Sandbox;
    const FString OutsideTexture = FPaths::Combine(Sandbox.GetOutsidePath(), TEXT("red.png"));
    if (!TestTrue(TEXT("Sandbox should initialize"), Sandbox.IsReady()) ||
        !TestTrue(TEXT("Outside PNG should copy"), Sandbox.CopyRedPng(OutsideTexture)) ||
        !TestTrue(TEXT("OBJ should be written"), Sandbox.WriteRootFile(TEXT("model.obj"), TriangleObj)) ||
        !TestTrue(TEXT("Junction should be created"), Sandbox.AddJunction(TEXT("linked"), Sandbox.GetOutsidePath())))
    {
        return false;
    }

    FRestrictedAssimpIOSystem IOSystem(FPaths::Combine(Sandbox.GetRootPath(), TEXT("model.obj")));
    if (!TestTrue(TEXT("IO system should initialize"), IOSystem.IsInitialized()))
    {
        return false;
    }

    AddExpectedError(TEXT("outside model root"), EAutomationExpectedErrorFlags::Contains, 1);
    TestNull(TEXT("Relative outside-root path should be denied"), OpenRestricted(IOSystem, TEXT("../outside/red.png")));
    TestTrue(TEXT("Relative denial should identify the canonical root escape"),
             IOSystem.GetLastDenialReason().Contains(TEXT("outside model root")));
    TestNull(TEXT("Absolute outside-root path should be denied"), OpenRestricted(IOSystem, OutsideTexture));
    TestTrue(TEXT("Absolute denial should identify the canonical root escape"),
             IOSystem.GetLastDenialReason().Contains(TEXT("outside model root")));
    TestNull(TEXT("Junction outside-root path should be denied"), OpenRestricted(IOSystem, TEXT("linked/red.png")));
    TestTrue(TEXT("Junction denial should identify the canonical root escape"),
             IOSystem.GetLastDenialReason().Contains(TEXT("outside model root")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestrictedAssimpIOOutsideMtlDenied,
                                 "RuntimeAssetImport.Security.Sandbox.OutsideMtlDenied",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedAssimpIOOutsideMtlDenied::RunTest(const FString &Parameters)
{
    FScopedAssetSandbox Sandbox;
    if (!TestTrue(TEXT("Sandbox should initialize"), Sandbox.IsReady()) ||
        !TestTrue(
            TEXT("OBJ should be written"),
            Sandbox.WriteRootFile(TEXT("model.obj"),
                                  FString(TriangleObj).Replace(TEXT("model.mtl"), TEXT("../outside/model.mtl")))) ||
        !TestTrue(TEXT("Outside MTL should be written"),
                  Sandbox.WriteOutsideFile(TEXT("model.mtl"), TEXT("newmtl TestMaterial\nKd 0.0 1.0 0.0\n"))))
    {
        return false;
    }

    AddExpectedError(TEXT("Restricted Assimp IO denied access"), EAutomationExpectedErrorFlags::Contains, 1);
    ELoadMeshFromAssetFileResult Result = ELoadMeshFromAssetFileResult::Failure;
    const FLoadedMeshData MeshData = LoadSandboxObj(Sandbox, Result);
    bool Passed = TestEqual(TEXT("Outside MTL should not prevent geometry import"), Result,
                            ELoadMeshFromAssetFileResult::Success);
    const FLoadedMaterialData *Material = FindSecurityFirstUsedMaterial(MeshData);
    if (Material != nullptr && Material->ColorStatus == EColorStatus::ColorIsSet)
    {
        Passed &= TestFalse(TEXT("Outside green MTL color must not be imported"),
                            FMath::IsNearlyEqual(Material->Color.G, 1.0f) && FMath::IsNearlyZero(Material->Color.R));
    }
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestrictedAssimpIOJunctionExternalBufferDenied,
                                 "RuntimeAssetImport.Security.Sandbox.JunctionExternalBufferDenied",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedAssimpIOJunctionExternalBufferDenied::RunTest(const FString &Parameters)
{
    FScopedAssetSandbox Sandbox;
    const FString SourceGltf = FPaths::Combine(GetSecurityTestAssetsDir(), TEXT("test_external_buffer.gltf"));
    const FString SourceBuffer = FPaths::Combine(GetSecurityTestAssetsDir(), TEXT("buffers/test_triangle.bin"));
    TArray<uint8> GltfBytes;
    TArray<uint8> BufferBytes;
    if (!TestTrue(TEXT("Sandbox should initialize"), Sandbox.IsReady()) ||
        !TestTrue(TEXT("glTF source should read"), FFileHelper::LoadFileToArray(GltfBytes, *SourceGltf)) ||
        !TestTrue(TEXT("Buffer source should read"), FFileHelper::LoadFileToArray(BufferBytes, *SourceBuffer)) ||
        !TestTrue(
            TEXT("glTF should be written"),
            FFileHelper::SaveArrayToFile(GltfBytes, *FPaths::Combine(Sandbox.GetRootPath(), TEXT("model.gltf")))) ||
        !TestTrue(TEXT("Outside buffer should be written"),
                  FFileHelper::SaveArrayToFile(
                      BufferBytes, *FPaths::Combine(Sandbox.GetOutsidePath(), TEXT("test_triangle.bin")))) ||
        !TestTrue(TEXT("Buffer junction should be created"),
                  Sandbox.AddJunction(TEXT("buffers"), Sandbox.GetOutsidePath())))
    {
        return false;
    }

    AddExpectedError(TEXT("Restricted Assimp IO denied access"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedError(TEXT("Assimp failed to load"), EAutomationExpectedErrorFlags::Contains, 1);
    ELoadMeshFromAssetFileResult Result = ELoadMeshFromAssetFileResult::Success;
    const FLoadedMeshData MeshData =
        UAssetLoader::LoadMeshFromAssetFile(FPaths::Combine(Sandbox.GetRootPath(), TEXT("model.gltf")), Result);
    bool Passed =
        TestEqual(TEXT("Junction external buffer should fail import"), Result, ELoadMeshFromAssetFileResult::Failure);
    Passed &= TestTrue(TEXT("Failed junction buffer import should be empty"), MeshData.NodeList.IsEmpty());
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestrictedAssimpIOAlternateDataStreamDenied,
                                 "RuntimeAssetImport.Security.Sandbox.AlternateDataStreamDenied",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedAssimpIOAlternateDataStreamDenied::RunTest(const FString &Parameters)
{
    FScopedAssetSandbox Sandbox;
    const FString TexturePath = FPaths::Combine(Sandbox.GetRootPath(), TEXT("textures/test_red.png"));
    TArray<uint8> RedPngBytes;
    if (!TestTrue(TEXT("Sandbox should initialize"), Sandbox.IsReady()) ||
        !TestTrue(TEXT("Red PNG should copy"), Sandbox.CopyRedPng(TexturePath)) ||
        !TestTrue(TEXT("Red PNG should read"), FFileHelper::LoadFileToArray(RedPngBytes, *TexturePath)) ||
        !TestTrue(TEXT("Alternate data stream should be created"),
                  CreateAlternateDataStream(TexturePath + TEXT(":secret"), RedPngBytes)) ||
        !TestTrue(TEXT("ADS OBJ should be written"),
                  PrepareObjWithTexture(Sandbox, TEXT("textures/test_red.png:secret"))))
    {
        return false;
    }

    AddExpectedError(TEXT("Restricted Assimp IO denied access"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedError(TEXT("is external and is not loaded"), EAutomationExpectedErrorFlags::Contains, 1);
    ELoadMeshFromAssetFileResult Result = ELoadMeshFromAssetFileResult::Failure;
    const FLoadedMeshData MeshData = LoadSandboxObj(Sandbox, Result);
    bool Passed =
        TestEqual(TEXT("ADS reference may still load geometry"), Result, ELoadMeshFromAssetFileResult::Success);
    Passed &= AssertNoExternalTexture(*this, TEXT("Alternate data stream"), MeshData);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestrictedAssimpIOUrlDenied, "RuntimeAssetImport.Security.Sandbox.UrlDenied",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedAssimpIOUrlDenied::RunTest(const FString &Parameters)
{
    FScopedAssetSandbox Sandbox;
    if (!TestTrue(TEXT("Sandbox should initialize"), Sandbox.IsReady()) ||
        !TestTrue(TEXT("URL OBJ should be written"),
                  PrepareObjWithTexture(Sandbox, TEXT("https://example.invalid/test.png"))))
    {
        return false;
    }

    AddExpectedError(TEXT("Restricted Assimp IO denied access"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedError(TEXT("is external and is not loaded"), EAutomationExpectedErrorFlags::Contains, 1);
    ELoadMeshFromAssetFileResult Result = ELoadMeshFromAssetFileResult::Failure;
    const FLoadedMeshData MeshData = LoadSandboxObj(Sandbox, Result);
    bool Passed =
        TestEqual(TEXT("URL reference may still load geometry"), Result, ELoadMeshFromAssetFileResult::Success);
    Passed &= AssertNoExternalTexture(*this, TEXT("URL reference"), MeshData);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestrictedAssimpIOUrlAndDevicePathsDenied,
                                 "RuntimeAssetImport.Security.Sandbox.UrlAndDevicePathsDenied",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedAssimpIOUrlAndDevicePathsDenied::RunTest(const FString &Parameters)
{
    FScopedAssetSandbox Sandbox;
    if (!TestTrue(TEXT("Sandbox should initialize"), Sandbox.IsReady()) ||
        !TestTrue(TEXT("OBJ should be written"), Sandbox.WriteRootFile(TEXT("model.obj"), TriangleObj)))
    {
        return false;
    }

    FRestrictedAssimpIOSystem IOSystem(FPaths::Combine(Sandbox.GetRootPath(), TEXT("model.obj")));
    TestTrue(TEXT("IO system should initialize"), IOSystem.IsInitialized());
    AddExpectedError(TEXT("URL and file URI schemes are not allowed"), EAutomationExpectedErrorFlags::Contains, 1);
    TestNull(TEXT("HTTPS URL should be denied"), OpenRestricted(IOSystem, TEXT("https://example.com/model.bin")));
    TestNull(TEXT("File URL should be denied"), OpenRestricted(IOSystem, TEXT("file:///C:/Windows/win.ini")));
    TestNull(TEXT("Win32 device path should be denied"), OpenRestricted(IOSystem, TEXT("\\\\.\\C:\\Windows")));
    TestNull(TEXT("NT path should be denied"), OpenRestricted(IOSystem, TEXT("\\??\\C:\\Windows\\win.ini")));
    TestNull(TEXT("Extended path should be denied"), OpenRestricted(IOSystem, TEXT("\\\\?\\C:\\Windows\\win.ini")));
    TestTrue(
        TEXT("The latest denial should identify an extended path"),
        IOSystem.GetLastDenialReason().Contains(TEXT("Device, NT, and extended-length caller paths are not allowed")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestrictedAssimpIOOversizedCompressedTextureDenied,
                                 "RuntimeAssetImport.Security.Limits.OversizedCompressedTextureDenied",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedAssimpIOOversizedCompressedTextureDenied::RunTest(const FString &Parameters)
{
    FScopedAssetSandbox Sandbox;
    const FString TexturePath = FPaths::Combine(Sandbox.GetRootPath(), TEXT("oversized.png"));
    if (!TestTrue(TEXT("Sandbox should initialize"), Sandbox.IsReady()) ||
        !TestTrue(TEXT("Oversized sparse PNG should be created"),
                  CreateSparseFile(TexturePath, RuntimeAssetImport::Limits::MaximumCompressedTextureBytes + 1)) ||
        !TestTrue(TEXT("Oversized texture OBJ should be written"),
                  PrepareObjWithTexture(Sandbox, TEXT("oversized.png"))))
    {
        return false;
    }

    AddExpectedError(TEXT("Restricted Assimp IO denied access"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedError(TEXT("is external and is not loaded"), EAutomationExpectedErrorFlags::Contains, 1);
    ELoadMeshFromAssetFileResult Result = ELoadMeshFromAssetFileResult::Failure;
    const FLoadedMeshData MeshData = LoadSandboxObj(Sandbox, Result);
    bool Passed =
        TestEqual(TEXT("Oversized texture may still load geometry"), Result, ELoadMeshFromAssetFileResult::Success);
    Passed &= AssertNoExternalTexture(*this, TEXT("Oversized texture"), MeshData);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestrictedAssimpIOOversizedAuxiliaryFileDenied,
                                 "RuntimeAssetImport.Security.Limits.OversizedAuxiliaryFileDenied",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedAssimpIOOversizedAuxiliaryFileDenied::RunTest(const FString &Parameters)
{
    FScopedAssetSandbox Sandbox;
    const FString MainPath = FPaths::Combine(Sandbox.GetRootPath(), TEXT("model.obj"));
    const FString AuxiliaryPath = FPaths::Combine(Sandbox.GetRootPath(), TEXT("oversized.bin"));
    if (!TestTrue(TEXT("Sandbox should initialize"), Sandbox.IsReady()) ||
        !TestTrue(TEXT("OBJ should be written"), Sandbox.WriteRootFile(TEXT("model.obj"), TriangleObj)) ||
        !TestTrue(TEXT("Oversized sparse auxiliary file should be created"),
                  CreateSparseFile(AuxiliaryPath, RuntimeAssetImport::Limits::MaximumAuxiliaryFileBytes + 1)))
    {
        return false;
    }

    FRestrictedAssimpIOSystem IOSystem(MainPath);
    TestTrue(TEXT("IO system should initialize"), IOSystem.IsInitialized());
    AddExpectedError(TEXT("the limit is 268435456 bytes"), EAutomationExpectedErrorFlags::Contains, 1);
    TestNull(TEXT("Oversized auxiliary file should be denied"), OpenRestricted(IOSystem, AuxiliaryPath));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestrictedAssimpIOUniqueFileBudgetDenied,
                                 "RuntimeAssetImport.Security.Limits.UniqueFileBudgetDenied",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedAssimpIOUniqueFileBudgetDenied::RunTest(const FString &Parameters)
{
    FScopedAssetSandbox Sandbox;
    const FString MainPath = FPaths::Combine(Sandbox.GetRootPath(), TEXT("model.obj"));
    if (!TestTrue(TEXT("Sandbox should initialize"), Sandbox.IsReady()) ||
        !TestTrue(TEXT("OBJ should be written"), Sandbox.WriteRootFile(TEXT("model.obj"), TriangleObj)))
    {
        return false;
    }

    for (int32 Index = 0; Index < RuntimeAssetImport::Limits::MaximumUniqueOpenedFiles; ++Index)
    {
        if (!SaveUtf8(FPaths::Combine(Sandbox.GetRootPath(), FString::Printf(TEXT("file-%03d.bin"), Index)), TEXT("x")))
        {
            AddError(TEXT("Could not prepare unique-file budget assets."));
            return false;
        }
    }

    FRestrictedAssimpIOSystem IOSystem(MainPath);
    TestTrue(TEXT("IO system should initialize"), IOSystem.IsInitialized());
    Assimp::IOStream *MainStream = OpenRestricted(IOSystem, MainPath);
    if (!TestNotNull(TEXT("The main model should occupy one opened-file slot"), MainStream))
    {
        return false;
    }
    IOSystem.Close(MainStream);
    for (int32 Index = 0; Index < RuntimeAssetImport::Limits::MaximumUniqueOpenedFiles - 1; ++Index)
    {
        Assimp::IOStream *Stream = OpenRestricted(IOSystem, FString::Printf(TEXT("file-%03d.bin"), Index));
        if (!TestNotNull(*FString::Printf(TEXT("Unique file %d should fit the budget"), Index), Stream))
        {
            return false;
        }
        IOSystem.Close(Stream);
        if (Index == 0)
        {
            Assimp::IOStream *RepeatedStream = OpenRestricted(IOSystem, TEXT("file-000.bin"));
            if (!TestNotNull(TEXT("Reopening a file should not consume another unique-file slot"), RepeatedStream))
            {
                return false;
            }
            IOSystem.Close(RepeatedStream);
        }
    }

    AddExpectedError(TEXT("Unique file limit exceeded"), EAutomationExpectedErrorFlags::Contains, 1);
    Assimp::IOStream *Denied = OpenRestricted(
        IOSystem, FString::Printf(TEXT("file-%03d.bin"), RuntimeAssetImport::Limits::MaximumUniqueOpenedFiles - 1));
    TestNull(TEXT("The first file beyond the unique-file budget should be denied"), Denied);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestrictedAssimpIOTotalByteBudgetDenied,
                                 "RuntimeAssetImport.Security.Limits.TotalByteBudgetDenied",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedAssimpIOTotalByteBudgetDenied::RunTest(const FString &Parameters)
{
    FScopedAssetSandbox Sandbox;
    const FString MainPath = FPaths::Combine(Sandbox.GetRootPath(), TEXT("model.obj"));
    if (!TestTrue(TEXT("Sandbox should initialize"), Sandbox.IsReady()) ||
        !TestTrue(TEXT("OBJ should be written"), Sandbox.WriteRootFile(TEXT("model.obj"), TriangleObj)))
    {
        return false;
    }

    for (int32 Index = 0; Index < 4; ++Index)
    {
        if (!TestTrue(
                *FString::Printf(TEXT("Sparse budget file %d should be created"), Index),
                CreateSparseFile(FPaths::Combine(Sandbox.GetRootPath(), FString::Printf(TEXT("budget-%d.bin"), Index)),
                                 RuntimeAssetImport::Limits::MaximumAuxiliaryFileBytes)))
        {
            return false;
        }
    }

    FRestrictedAssimpIOSystem IOSystem(MainPath);
    TestTrue(TEXT("IO system should initialize"), IOSystem.IsInitialized());
    Assimp::IOStream *MainStream = OpenRestricted(IOSystem, MainPath);
    if (!TestNotNull(TEXT("The main model should contribute to the opened-byte budget"), MainStream))
    {
        return false;
    }
    IOSystem.Close(MainStream);
    for (int32 Index = 0; Index < 3; ++Index)
    {
        Assimp::IOStream *Stream = OpenRestricted(IOSystem, FString::Printf(TEXT("budget-%d.bin"), Index));
        if (!TestNotNull(*FString::Printf(TEXT("Budget file %d should fit"), Index), Stream))
        {
            return false;
        }
        IOSystem.Close(Stream);
        if (Index == 0)
        {
            Assimp::IOStream *RepeatedStream = OpenRestricted(IOSystem, TEXT("budget-0.bin"));
            if (!TestNotNull(TEXT("Reopening a file should not consume the byte budget twice"), RepeatedStream))
            {
                return false;
            }
            IOSystem.Close(RepeatedStream);
        }
    }

    AddExpectedError(TEXT("Total unique opened byte budget exceeded"), EAutomationExpectedErrorFlags::Contains, 1);
    Assimp::IOStream *Denied = OpenRestricted(IOSystem, TEXT("budget-3.bin"));
    TestNull(TEXT("The first file beyond the total byte budget should be denied"), Denied);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestrictedAssimpIOExistsDoesNotConsumeUniqueFileBudget,
                                 "RuntimeAssetImport.Security.Limits.ExistsDoesNotConsumeUniqueFileBudget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedAssimpIOExistsDoesNotConsumeUniqueFileBudget::RunTest(const FString &Parameters)
{
    FScopedAssetSandbox Sandbox;
    const FString MainPath = FPaths::Combine(Sandbox.GetRootPath(), TEXT("model.obj"));
    if (!TestTrue(TEXT("Sandbox should initialize"), Sandbox.IsReady()) ||
        !TestTrue(TEXT("OBJ should be written"), Sandbox.WriteRootFile(TEXT("model.obj"), TriangleObj)))
    {
        return false;
    }

    for (int32 Index = 0; Index < RuntimeAssetImport::Limits::MaximumUniqueOpenedFiles; ++Index)
    {
        if (!SaveUtf8(FPaths::Combine(Sandbox.GetRootPath(), FString::Printf(TEXT("exists-%03d.bin"), Index)),
                      TEXT("x")))
        {
            AddError(TEXT("Could not prepare Exists unique-file assets."));
            return false;
        }
    }

    FRestrictedAssimpIOSystem IOSystem(MainPath);
    bool Passed = TestTrue(TEXT("IO system should initialize"), IOSystem.IsInitialized());
    for (int32 Index = 0; Index < RuntimeAssetImport::Limits::MaximumUniqueOpenedFiles; ++Index)
    {
        Passed &= TestTrue(*FString::Printf(TEXT("Exists should validate file %d without accounting it"), Index),
                           ExistsRestricted(IOSystem, FString::Printf(TEXT("exists-%03d.bin"), Index)));
    }

    Assimp::IOStream *MainStream = OpenRestricted(IOSystem, MainPath);
    if (!TestNotNull(TEXT("Main model should open after all Exists probes"), MainStream))
    {
        return false;
    }
    IOSystem.Close(MainStream);
    for (int32 Index = 0; Index < RuntimeAssetImport::Limits::MaximumUniqueOpenedFiles - 1; ++Index)
    {
        Assimp::IOStream *Stream = OpenRestricted(IOSystem, FString::Printf(TEXT("exists-%03d.bin"), Index));
        if (!TestNotNull(*FString::Printf(TEXT("Open %d should fit after Exists probes"), Index), Stream))
        {
            return false;
        }
        IOSystem.Close(Stream);
    }

    AddExpectedError(TEXT("Unique file limit exceeded"), EAutomationExpectedErrorFlags::Contains, 1);
    Assimp::IOStream *Denied = OpenRestricted(
        IOSystem, FString::Printf(TEXT("exists-%03d.bin"), RuntimeAssetImport::Limits::MaximumUniqueOpenedFiles - 1));
    Passed &= TestNull(TEXT("Only the first Open beyond the unique-file budget should be denied"), Denied);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestrictedAssimpIOExistsDoesNotConsumeByteBudget,
                                 "RuntimeAssetImport.Security.Limits.ExistsDoesNotConsumeByteBudget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedAssimpIOExistsDoesNotConsumeByteBudget::RunTest(const FString &Parameters)
{
    FScopedAssetSandbox Sandbox;
    const FString MainPath = FPaths::Combine(Sandbox.GetRootPath(), TEXT("model.obj"));
    if (!TestTrue(TEXT("Sandbox should initialize"), Sandbox.IsReady()) ||
        !TestTrue(TEXT("OBJ should be written"), Sandbox.WriteRootFile(TEXT("model.obj"), TriangleObj)))
    {
        return false;
    }

    for (int32 Index = 0; Index < 4; ++Index)
    {
        if (!TestTrue(*FString::Printf(TEXT("Exists budget file %d should be created"), Index),
                      CreateSparseFile(
                          FPaths::Combine(Sandbox.GetRootPath(), FString::Printf(TEXT("exists-budget-%d.bin"), Index)),
                          RuntimeAssetImport::Limits::MaximumAuxiliaryFileBytes)))
        {
            return false;
        }
    }

    FRestrictedAssimpIOSystem IOSystem(MainPath);
    bool Passed = TestTrue(TEXT("IO system should initialize"), IOSystem.IsInitialized());
    for (int32 Index = 0; Index < 4; ++Index)
    {
        Passed &= TestTrue(*FString::Printf(TEXT("Exists should not account bytes for file %d"), Index),
                           ExistsRestricted(IOSystem, FString::Printf(TEXT("exists-budget-%d.bin"), Index)));
    }

    Assimp::IOStream *MainStream = OpenRestricted(IOSystem, MainPath);
    if (!TestNotNull(TEXT("Main model should open after byte-budget Exists probes"), MainStream))
    {
        return false;
    }
    IOSystem.Close(MainStream);
    for (int32 Index = 0; Index < 3; ++Index)
    {
        Assimp::IOStream *Stream = OpenRestricted(IOSystem, FString::Printf(TEXT("exists-budget-%d.bin"), Index));
        if (!TestNotNull(*FString::Printf(TEXT("Byte-budget Open %d should fit"), Index), Stream))
        {
            return false;
        }
        IOSystem.Close(Stream);
    }

    AddExpectedError(TEXT("Total unique opened byte budget exceeded"), EAutomationExpectedErrorFlags::Contains, 1);
    Assimp::IOStream *Denied = OpenRestricted(IOSystem, TEXT("exists-budget-3.bin"));
    Passed &= TestNull(TEXT("Exists probes should not change the later Open success count"), Denied);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestrictedAssimpIOReopenGrowthConsumesDelta,
                                 "RuntimeAssetImport.Security.Limits.ReopenGrowthConsumesDelta",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedAssimpIOReopenGrowthConsumesDelta::RunTest(const FString &Parameters)
{
    FScopedAssetSandbox Sandbox;
    const FString MainPath = FPaths::Combine(Sandbox.GetRootPath(), TEXT("model.obj"));
    const FString GrowingPath = FPaths::Combine(Sandbox.GetRootPath(), TEXT("growing.bin"));
    if (!TestTrue(TEXT("Sandbox should initialize"), Sandbox.IsReady()) ||
        !TestTrue(TEXT("OBJ should be written"), Sandbox.WriteRootFile(TEXT("model.obj"), TriangleObj)) ||
        !TestTrue(TEXT("Initial growing file should be created"), CreateSparseFile(GrowingPath, 1)))
    {
        return false;
    }

    for (int32 Index = 0; Index < 3; ++Index)
    {
        if (!TestTrue(*FString::Printf(TEXT("Growth budget file %d should be created"), Index),
                      CreateSparseFile(
                          FPaths::Combine(Sandbox.GetRootPath(), FString::Printf(TEXT("growth-budget-%d.bin"), Index)),
                          RuntimeAssetImport::Limits::MaximumAuxiliaryFileBytes)))
        {
            return false;
        }
    }

    FRestrictedAssimpIOSystem IOSystem(MainPath);
    Assimp::IOStream *MainStream = OpenRestricted(IOSystem, MainPath);
    Assimp::IOStream *GrowingStream = OpenRestricted(IOSystem, GrowingPath);
    if (!TestNotNull(TEXT("Main model should open"), MainStream) ||
        !TestNotNull(TEXT("Initial growing file should open"), GrowingStream))
    {
        return false;
    }
    IOSystem.Close(MainStream);
    IOSystem.Close(GrowingStream);

    if (!TestTrue(TEXT("Growing file should be enlarged"),
                  CreateSparseFile(GrowingPath, RuntimeAssetImport::Limits::MaximumAuxiliaryFileBytes)))
    {
        return false;
    }
    GrowingStream = OpenRestricted(IOSystem, GrowingPath);
    if (!TestNotNull(TEXT("Enlarged file should reopen and account its delta"), GrowingStream))
    {
        return false;
    }
    IOSystem.Close(GrowingStream);

    for (int32 Index = 0; Index < 2; ++Index)
    {
        Assimp::IOStream *Stream = OpenRestricted(IOSystem, FString::Printf(TEXT("growth-budget-%d.bin"), Index));
        if (!TestNotNull(*FString::Printf(TEXT("Growth budget file %d should fit"), Index), Stream))
        {
            return false;
        }
        IOSystem.Close(Stream);
    }

    AddExpectedError(TEXT("Total unique opened byte budget exceeded"), EAutomationExpectedErrorFlags::Contains, 1);
    Assimp::IOStream *Denied = OpenRestricted(IOSystem, TEXT("growth-budget-2.bin"));
    TestNull(TEXT("Growth delta should consume budget on reopen"), Denied);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestrictedAssimpIOReopenShrinkDoesNotRefundBudget,
                                 "RuntimeAssetImport.Security.Limits.ReopenShrinkDoesNotRefundBudget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedAssimpIOReopenShrinkDoesNotRefundBudget::RunTest(const FString &Parameters)
{
    FScopedAssetSandbox Sandbox;
    const FString MainPath = FPaths::Combine(Sandbox.GetRootPath(), TEXT("model.obj"));
    const FString ShrinkingPath = FPaths::Combine(Sandbox.GetRootPath(), TEXT("shrinking.bin"));
    if (!TestTrue(TEXT("Sandbox should initialize"), Sandbox.IsReady()) ||
        !TestTrue(TEXT("OBJ should be written"), Sandbox.WriteRootFile(TEXT("model.obj"), TriangleObj)) ||
        !TestTrue(TEXT("Initial shrinking file should be created"),
                  CreateSparseFile(ShrinkingPath, RuntimeAssetImport::Limits::MaximumAuxiliaryFileBytes)))
    {
        return false;
    }

    for (int32 Index = 0; Index < 3; ++Index)
    {
        if (!TestTrue(*FString::Printf(TEXT("Shrink budget file %d should be created"), Index),
                      CreateSparseFile(
                          FPaths::Combine(Sandbox.GetRootPath(), FString::Printf(TEXT("shrink-budget-%d.bin"), Index)),
                          RuntimeAssetImport::Limits::MaximumAuxiliaryFileBytes)))
        {
            return false;
        }
    }

    FRestrictedAssimpIOSystem IOSystem(MainPath);
    Assimp::IOStream *MainStream = OpenRestricted(IOSystem, MainPath);
    Assimp::IOStream *ShrinkingStream = OpenRestricted(IOSystem, ShrinkingPath);
    if (!TestNotNull(TEXT("Main model should open"), MainStream) ||
        !TestNotNull(TEXT("Initial shrinking file should open"), ShrinkingStream))
    {
        return false;
    }
    IOSystem.Close(MainStream);
    IOSystem.Close(ShrinkingStream);

    if (!TestTrue(TEXT("Shrinking file should be reduced"), CreateSparseFile(ShrinkingPath, 1)))
    {
        return false;
    }
    ShrinkingStream = OpenRestricted(IOSystem, ShrinkingPath);
    if (!TestNotNull(TEXT("Shrunken file should reopen without refunding budget"), ShrinkingStream))
    {
        return false;
    }
    IOSystem.Close(ShrinkingStream);

    for (int32 Index = 0; Index < 2; ++Index)
    {
        Assimp::IOStream *Stream = OpenRestricted(IOSystem, FString::Printf(TEXT("shrink-budget-%d.bin"), Index));
        if (!TestNotNull(*FString::Printf(TEXT("Shrink budget file %d should fit"), Index), Stream))
        {
            return false;
        }
        IOSystem.Close(Stream);
    }

    AddExpectedError(TEXT("Total unique opened byte budget exceeded"), EAutomationExpectedErrorFlags::Contains, 1);
    Assimp::IOStream *Denied = OpenRestricted(IOSystem, TEXT("shrink-budget-2.bin"));
    TestNull(TEXT("Shrinking a reopened file should not refund its prior accounting"), Denied);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestrictedAssimpIOCurrentWorkingDirectoryDoesNotAffectResolution,
                                 "RuntimeAssetImport.Security.Sandbox.CurrentWorkingDirectoryDoesNotAffectResolution",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedAssimpIOCurrentWorkingDirectoryDoesNotAffectResolution::RunTest(const FString &Parameters)
{
    FScopedAssetSandbox Sandbox;
    const FString MainPath = FPaths::Combine(Sandbox.GetRootPath(), TEXT("model.obj"));
    const FString RootTexturePath = FPaths::Combine(Sandbox.GetRootPath(), TEXT("textures/red.png"));
    const FString DecoyDirectory = FPaths::Combine(Sandbox.GetRootPath(), TEXT("decoy"));
    const FString DecoyTexturePath = FPaths::Combine(DecoyDirectory, TEXT("textures/red.png"));
    const TArray<uint8> DecoyBytes = {0x64, 0x65, 0x63, 0x6f, 0x79};
    if (!TestTrue(TEXT("Sandbox should initialize"), Sandbox.IsReady()) ||
        !TestTrue(TEXT("OBJ should be written"), Sandbox.WriteRootFile(TEXT("model.obj"), TriangleObj)) ||
        !TestTrue(TEXT("Root texture should copy"), Sandbox.CopyRedPng(RootTexturePath)) ||
        !TestTrue(TEXT("Decoy directory should be created"),
                  IFileManager::Get().MakeDirectory(*FPaths::GetPath(DecoyTexturePath), true)) ||
        !TestTrue(TEXT("Decoy texture should be written"), FFileHelper::SaveArrayToFile(DecoyBytes, *DecoyTexturePath)))
    {
        return false;
    }

    TArray<uint8> ExpectedBytes;
    if (!TestTrue(TEXT("Expected root texture should be readable"),
                  FFileHelper::LoadFileToArray(ExpectedBytes, *RootTexturePath)))
    {
        return false;
    }

    FRestrictedAssimpIOSystem IOSystem(MainPath);
    FScopedProcessCurrentDirectory CurrentDirectory(DecoyDirectory);
    if (!TestTrue(TEXT("Process current directory should change for the regression test"),
                  CurrentDirectory.IsChanged()))
    {
        return false;
    }

    Assimp::IOStream *Stream = OpenRestricted(IOSystem, TEXT("textures/red.png"));
    if (!TestNotNull(TEXT("Relative texture should resolve from the model root"), Stream))
    {
        return false;
    }
    TArray<uint8> ActualBytes;
    const bool bRead = ReadRestrictedStream(*Stream, ActualBytes);
    IOSystem.Close(Stream);
    bool Passed = TestTrue(TEXT("Resolved texture should be readable"), bRead);
    Passed &= TestTrue(TEXT("Process CWD must not select the decoy texture"), ActualBytes == ExpectedBytes);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestrictedAssimpIOHardLinkEscapeDenied,
                                 "RuntimeAssetImport.Security.Sandbox.HardLinkEscapeDenied",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedAssimpIOHardLinkEscapeDenied::RunTest(const FString &Parameters)
{
    FScopedAssetSandbox Sandbox;
    const FString MainPath = FPaths::Combine(Sandbox.GetRootPath(), TEXT("model.obj"));
    const FString OutsideTexturePath = FPaths::Combine(Sandbox.GetOutsidePath(), TEXT("red.png"));
    const FString LinkedTexturePath = FPaths::Combine(Sandbox.GetRootPath(), TEXT("linked.png"));
    if (!TestTrue(TEXT("Sandbox should initialize"), Sandbox.IsReady()) ||
        !TestTrue(TEXT("Outside PNG should copy"), Sandbox.CopyRedPng(OutsideTexturePath)) ||
        !TestTrue(TEXT("Hard link should be created"), Sandbox.AddHardLink(TEXT("linked.png"), OutsideTexturePath)) ||
        !TestTrue(TEXT("Hard-link OBJ should be written"), PrepareObjWithTexture(Sandbox, TEXT("linked.png"))))
    {
        return false;
    }

    TArray<uint8> OriginalOutsideBytes;
    if (!TestTrue(TEXT("Outside original should be readable"),
                  FFileHelper::LoadFileToArray(OriginalOutsideBytes, *OutsideTexturePath)))
    {
        return false;
    }

    AddExpectedError(TEXT("Restricted Assimp IO denied access"), EAutomationExpectedErrorFlags::Contains, 2);
    FRestrictedAssimpIOSystem DirectIO(MainPath);
    Assimp::IOStream *DeniedStream = OpenRestricted(DirectIO, LinkedTexturePath);
    bool Passed = TestNull(TEXT("Auxiliary hard links should be denied directly"), DeniedStream);
    Passed &= TestTrue(TEXT("Denial should identify multiple hard links"),
                       DirectIO.GetLastDenialReason().Contains(TEXT("multiple hard links")));

    AddExpectedError(TEXT("is external and is not loaded"), EAutomationExpectedErrorFlags::Contains, 1);
    ELoadMeshFromAssetFileResult Result = ELoadMeshFromAssetFileResult::Failure;
    const FLoadedMeshData MeshData = LoadSandboxObj(Sandbox, Result);
    Passed &=
        TestEqual(TEXT("Hard-link denial may still load geometry"), Result, ELoadMeshFromAssetFileResult::Success);
    Passed &= AssertNoExternalTexture(*this, TEXT("Hard-link escape"), MeshData);

    TArray<uint8> FinalOutsideBytes;
    Passed &= TestTrue(TEXT("Outside original should remain readable"),
                       FFileHelper::LoadFileToArray(FinalOutsideBytes, *OutsideTexturePath));
    Passed &= TestTrue(TEXT("Outside original should remain unchanged"), FinalOutsideBytes == OriginalOutsideBytes);
    return Passed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRestrictedAssimpIOStreamReadOnlyBounds,
                                 "RuntimeAssetImport.Security.Stream.ReadOnlyBounds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedAssimpIOStreamReadOnlyBounds::RunTest(const FString &Parameters)
{
    FScopedAssetSandbox Sandbox;
    const FString MainPath = FPaths::Combine(Sandbox.GetRootPath(), TEXT("model.bin"));
    if (!TestTrue(TEXT("Sandbox should initialize"), Sandbox.IsReady()) ||
        !TestTrue(TEXT("Stream file should be written"), Sandbox.WriteRootFile(TEXT("model.bin"), TEXT("abcdef"))))
    {
        return false;
    }

    FRestrictedAssimpIOSystem IOSystem(MainPath);
    Assimp::IOStream *Stream = OpenRestricted(IOSystem, MainPath);
    if (!TestNotNull(TEXT("Restricted stream should open"), Stream))
    {
        return false;
    }

    ANSICHAR Bytes[8]{};
    bool Passed = TestEqual(TEXT("Two complete two-byte elements should read"), Stream->Read(Bytes, 2, 2),
                            static_cast<size_t>(2));
    Passed &= TestEqual(TEXT("Read bytes should match"), FString(UTF8_TO_TCHAR(Bytes)), FString(TEXT("abcd")));
    Passed &= TestEqual(TEXT("Tell should advance"), Stream->Tell(), static_cast<size_t>(4));
    Passed &= TestEqual(TEXT("Seek beyond end should fail"), Stream->Seek(7, aiOrigin_SET), AI_FAILURE);
    Passed &= TestEqual(TEXT("Seek from end should succeed"), Stream->Seek(2, aiOrigin_END), AI_SUCCESS);
    Passed &= TestEqual(TEXT("End-relative position"), Stream->Tell(), static_cast<size_t>(4));
    Passed &= TestEqual(TEXT("Writes should be denied"), Stream->Write(Bytes, 1, 1), static_cast<size_t>(0));
    IOSystem.Close(Stream);

    AddExpectedError(TEXT("Write-capable or invalid mode"), EAutomationExpectedErrorFlags::Contains, 1);
    FTCHARToUTF8 Utf8MainPath(*MainPath);
    TestNull(TEXT("Write-capable open mode should be denied"), IOSystem.Open(Utf8MainPath.Get(), "r+"));
    return Passed;
}
