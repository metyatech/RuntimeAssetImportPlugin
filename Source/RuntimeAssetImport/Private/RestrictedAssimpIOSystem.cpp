// Copyright (c) 2026 metyatech. All rights reserved.

#include "RestrictedAssimpIOSystem.h"

#include "AssetImportLimits.h"
#include "LogAssetLoader.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

#include <limits>

namespace
{
    constexpr DWORD SharedReadFlags = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

    FString NormalizeFinalHandlePath(FString Path)
    {
        Path.ReplaceInline(TEXT("/"), TEXT("\\"));
        if (Path.StartsWith(TEXT("\\\\?\\UNC\\"), ESearchCase::IgnoreCase))
        {
            Path = TEXT("\\\\") + Path.Mid(8);
        }
        else if (Path.StartsWith(TEXT("\\\\?\\"), ESearchCase::IgnoreCase))
        {
            Path.RightChopInline(4, EAllowShrinking::No);
        }
        return Path;
    }

    bool GetFinalPathForHandle(const HANDLE Handle, FString &OutPath, FString &OutError)
    {
        DWORD Capacity = 512;
        TArray<WCHAR> Buffer;
        for (;;)
        {
            Buffer.SetNumUninitialized(static_cast<int32>(Capacity));
            const DWORD Length =
                GetFinalPathNameByHandleW(Handle, Buffer.GetData(), Capacity, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            if (Length == 0)
            {
                OutError =
                    FString::Printf(TEXT("GetFinalPathNameByHandleW failed with Windows error %lu."), GetLastError());
                return false;
            }
            if (Length < Capacity)
            {
                OutPath = NormalizeFinalHandlePath(FString(static_cast<int32>(Length), Buffer.GetData()));
                return true;
            }
            if (Length >= static_cast<DWORD>(MAX_int32 - 1))
            {
                OutError = TEXT("The final path is too long to represent.");
                return false;
            }
            Capacity = Length + 1;
        }
    }

    bool GetHandleFileInfo(const HANDLE Handle, bool &OutIsDirectory, uint64 &OutFileSize, FString &OutError)
    {
        BY_HANDLE_FILE_INFORMATION Information{};
        if (!GetFileInformationByHandle(Handle, &Information))
        {
            OutError =
                FString::Printf(TEXT("GetFileInformationByHandle failed with Windows error %lu."), GetLastError());
            return false;
        }

        OutIsDirectory = (Information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        OutFileSize = (static_cast<uint64>(Information.nFileSizeHigh) << 32) | Information.nFileSizeLow;
        return true;
    }

    bool IsPathWithinRoot(const FString &CandidatePath, const FString &RootPath)
    {
        if (CandidatePath.Equals(RootPath, ESearchCase::IgnoreCase))
        {
            return true;
        }

        FString RootWithBoundary = RootPath;
        if (!RootWithBoundary.EndsWith(TEXT("\\")))
        {
            RootWithBoundary.AppendChar(TEXT('\\'));
        }
        return CandidatePath.StartsWith(RootWithBoundary, ESearchCase::IgnoreCase);
    }

    bool IsImageExtension(const FString &Path)
    {
        FString Extension = FPaths::GetExtension(Path, true).ToLower();
        return Extension == TEXT(".png") || Extension == TEXT(".jpg") || Extension == TEXT(".jpeg") ||
               Extension == TEXT(".bmp") || Extension == TEXT(".tga") || Extension == TEXT(".dds") ||
               Extension == TEXT(".webp");
    }

    FString FromAssimpPath(const char *Path) { return Path != nullptr ? FString(UTF8_TO_TCHAR(Path)) : FString(); }
} // namespace

FRestrictedAssimpIOStream::FRestrictedAssimpIOStream(const HANDLE InFileHandle, const uint64 InFileSize)
    : FileHandle(InFileHandle), Size(InFileSize)
{
}

FRestrictedAssimpIOStream::~FRestrictedAssimpIOStream()
{
    if (FileHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(FileHandle);
        FileHandle = INVALID_HANDLE_VALUE;
    }
}

size_t FRestrictedAssimpIOStream::Read(void *Buffer, const size_t ElementSize, const size_t ElementCount)
{
    if (FileHandle == INVALID_HANDLE_VALUE || Buffer == nullptr || ElementSize == 0 || ElementCount == 0 ||
        ElementCount > std::numeric_limits<size_t>::max() / ElementSize)
    {
        return 0;
    }

    const uint64 RemainingBytes = Size - Position;
    const uint64 CompleteElementCount =
        FMath::Min<uint64>(static_cast<uint64>(ElementCount), RemainingBytes / static_cast<uint64>(ElementSize));
    const uint64 RequestedBytes = CompleteElementCount * static_cast<uint64>(ElementSize);
    uint64 TotalBytesRead = 0;
    uint8 *Destination = static_cast<uint8 *>(Buffer);
    while (TotalBytesRead < RequestedBytes)
    {
        const DWORD ChunkSize = static_cast<DWORD>(FMath::Min<uint64>(RequestedBytes - TotalBytesRead, MAXDWORD));
        DWORD ChunkBytesRead = 0;
        if (!ReadFile(FileHandle, Destination + TotalBytesRead, ChunkSize, &ChunkBytesRead, nullptr) ||
            ChunkBytesRead == 0)
        {
            break;
        }
        TotalBytesRead += ChunkBytesRead;
        Position += ChunkBytesRead;
    }
    return static_cast<size_t>(TotalBytesRead / static_cast<uint64>(ElementSize));
}

size_t FRestrictedAssimpIOStream::Write(const void *Buffer, const size_t ElementSize, const size_t ElementCount)
{
    return 0;
}

aiReturn FRestrictedAssimpIOStream::Seek(const size_t Offset, const aiOrigin Origin)
{
    uint64 NewPosition = 0;
    switch (Origin)
    {
    case aiOrigin_SET:
        NewPosition = static_cast<uint64>(Offset);
        break;
    case aiOrigin_CUR:
        if (static_cast<uint64>(Offset) > Size - Position)
        {
            return AI_FAILURE;
        }
        NewPosition = Position + static_cast<uint64>(Offset);
        break;
    case aiOrigin_END:
        if (static_cast<uint64>(Offset) > Size)
        {
            return AI_FAILURE;
        }
        NewPosition = Size - static_cast<uint64>(Offset);
        break;
    default:
        return AI_FAILURE;
    }

    if (NewPosition > Size || NewPosition > static_cast<uint64>(MAX_int64))
    {
        return AI_FAILURE;
    }

    LARGE_INTEGER Distance{};
    Distance.QuadPart = static_cast<LONGLONG>(NewPosition);
    if (!SetFilePointerEx(FileHandle, Distance, nullptr, FILE_BEGIN))
    {
        return AI_FAILURE;
    }
    Position = NewPosition;
    return AI_SUCCESS;
}

size_t FRestrictedAssimpIOStream::Tell() const { return static_cast<size_t>(Position); }

size_t FRestrictedAssimpIOStream::FileSize() const { return static_cast<size_t>(Size); }

void FRestrictedAssimpIOStream::Flush() {}

FRestrictedAssimpIOSystem::FRestrictedAssimpIOSystem(const FString &MainModelPath)
{
    FString ValidationReason;
    if (!ValidateCallerPath(MainModelPath, ValidationReason))
    {
        RecordDenial(MainModelPath, ValidationReason);
        return;
    }

    RequestedMainModelAbsolutePath = FPaths::ConvertRelativePathToFull(MainModelPath);
    RequestedMainModelAbsolutePath.ReplaceInline(TEXT("/"), TEXT("\\"));
    RequestedModelRootAbsolutePath = FPaths::GetPath(RequestedMainModelAbsolutePath);
    if (RequestedModelRootAbsolutePath.IsEmpty())
    {
        RecordDenial(MainModelPath, TEXT("The model parent directory could not be determined."));
        return;
    }

    HANDLE RootHandle = CreateFileW(*RequestedModelRootAbsolutePath, FILE_READ_ATTRIBUTES, SharedReadFlags, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (RootHandle == INVALID_HANDLE_VALUE)
    {
        RecordDenial(
            MainModelPath,
            FString::Printf(TEXT("The model directory could not be opened (Windows error %lu)."), GetLastError()));
        return;
    }

    FString Error;
    bool RootIsDirectory = false;
    uint64 IgnoredRootSize = 0;
    const bool bRootValid = GetFinalPathForHandle(RootHandle, FinalModelRootPath, Error) &&
                            GetHandleFileInfo(RootHandle, RootIsDirectory, IgnoredRootSize, Error) && RootIsDirectory;
    CloseHandle(RootHandle);
    if (!bRootValid)
    {
        RecordDenial(MainModelPath, Error.IsEmpty() ? TEXT("The model root is not a directory.") : Error);
        return;
    }

    HANDLE MainHandle = CreateFileW(*RequestedMainModelAbsolutePath, GENERIC_READ, SharedReadFlags, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (MainHandle == INVALID_HANDLE_VALUE)
    {
        RecordDenial(MainModelPath,
                     FString::Printf(TEXT("The main model could not be opened (Windows error %lu)."), GetLastError()));
        return;
    }

    bool MainIsDirectory = false;
    uint64 MainFileSize = 0;
    const bool bMainInfoValid = GetFinalPathForHandle(MainHandle, FinalMainModelPath, Error) &&
                                GetHandleFileInfo(MainHandle, MainIsDirectory, MainFileSize, Error);
    CloseHandle(MainHandle);
    if (!bMainInfoValid)
    {
        RecordDenial(MainModelPath, Error);
        return;
    }
    if (MainIsDirectory)
    {
        RecordDenial(MainModelPath, TEXT("The main model path identifies a directory."));
        return;
    }
    if (!IsPathWithinRoot(FinalMainModelPath, FinalModelRootPath))
    {
        RecordDenial(MainModelPath, TEXT("The main model resolves outside its model directory."));
        return;
    }
    if (MainFileSize > RuntimeAssetImport::Limits::MaximumMainModelFileBytes)
    {
        RecordDenial(MainModelPath,
                     FString::Printf(TEXT("The main model is %llu bytes; the limit is %llu bytes."), MainFileSize,
                                     RuntimeAssetImport::Limits::MaximumMainModelFileBytes));
        return;
    }
    if (MainFileSize > static_cast<uint64>(std::numeric_limits<size_t>::max()))
    {
        RecordDenial(MainModelPath, TEXT("The main model size cannot be represented by size_t."));
        return;
    }

    UniqueFinalPaths.Add(FinalMainModelPath.ToLower());
    TotalUniqueOpenedBytes = MainFileSize;
    bInitialized = true;
}

bool FRestrictedAssimpIOSystem::IsInitialized() const { return bInitialized; }

FString FRestrictedAssimpIOSystem::GetLastDenialReason() const
{
    FScopeLock Lock(&StateMutex);
    return LastDenialReason;
}

bool FRestrictedAssimpIOSystem::Exists(const char *FilePath) const
{
    HANDLE Handle = INVALID_HANDLE_VALUE;
    uint64 FileSize = 0;
    FString FinalPath;
    const bool bResolved = ResolveFile(FromAssimpPath(FilePath), true, Handle, FileSize, FinalPath);
    if (Handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(Handle);
    }
    return bResolved;
}

char FRestrictedAssimpIOSystem::getOsSeparator() const { return '\\'; }

Assimp::IOStream *FRestrictedAssimpIOSystem::Open(const char *FilePath, const char *Mode)
{
    const FString CallerPath = FromAssimpPath(FilePath);
    const FString RequestedMode = FromAssimpPath(Mode);
    if (RequestedMode.IsEmpty() || !RequestedMode.StartsWith(TEXT("r"), ESearchCase::CaseSensitive) ||
        RequestedMode.Contains(TEXT("+")) || RequestedMode.Contains(TEXT("w"), ESearchCase::IgnoreCase) ||
        RequestedMode.Contains(TEXT("a"), ESearchCase::IgnoreCase))
    {
        RecordDenial(CallerPath,
                     FString::Printf(TEXT("Write-capable or invalid mode '%s' is not allowed."), *RequestedMode));
        return nullptr;
    }

    HANDLE Handle = INVALID_HANDLE_VALUE;
    uint64 FileSize = 0;
    FString FinalPath;
    if (!ResolveFile(CallerPath, true, Handle, FileSize, FinalPath))
    {
        return nullptr;
    }
    return new FRestrictedAssimpIOStream(Handle, FileSize);
}

void FRestrictedAssimpIOSystem::Close(Assimp::IOStream *File) { delete File; }

bool FRestrictedAssimpIOSystem::ComparePaths(const char *FirstPath, const char *SecondPath) const
{
    HANDLE FirstHandle = INVALID_HANDLE_VALUE;
    HANDLE SecondHandle = INVALID_HANDLE_VALUE;
    uint64 FirstSize = 0;
    uint64 SecondSize = 0;
    FString FirstFinalPath;
    FString SecondFinalPath;
    const bool bFirstResolved = ResolveFile(FromAssimpPath(FirstPath), false, FirstHandle, FirstSize, FirstFinalPath);
    const bool bSecondResolved =
        ResolveFile(FromAssimpPath(SecondPath), false, SecondHandle, SecondSize, SecondFinalPath);
    if (FirstHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(FirstHandle);
    }
    if (SecondHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(SecondHandle);
    }
    return bFirstResolved && bSecondResolved && FirstFinalPath.Equals(SecondFinalPath, ESearchCase::IgnoreCase);
}

bool FRestrictedAssimpIOSystem::PushDirectory(const std::string &Path)
{
    FString FinalPath;
    if (!ResolveDirectory(FString(UTF8_TO_TCHAR(Path.c_str())), FinalPath))
    {
        return false;
    }
    DirectoryStack.emplace_back(TCHAR_TO_UTF8(*FinalPath));
    return true;
}

const std::string &FRestrictedAssimpIOSystem::CurrentDirectory() const
{
    static const std::string Empty;
    return DirectoryStack.empty() ? Empty : DirectoryStack.back();
}

size_t FRestrictedAssimpIOSystem::StackSize() const { return DirectoryStack.size(); }

bool FRestrictedAssimpIOSystem::PopDirectory()
{
    if (DirectoryStack.empty())
    {
        return false;
    }
    DirectoryStack.pop_back();
    return true;
}

bool FRestrictedAssimpIOSystem::CreateDirectory(const std::string &Path) { return false; }

bool FRestrictedAssimpIOSystem::ChangeDirectory(const std::string &Path) { return false; }

bool FRestrictedAssimpIOSystem::DeleteFile(const std::string &Path) { return false; }

bool FRestrictedAssimpIOSystem::ResolveFile(const FString &CallerPath, const bool TrackBudget, HANDLE &OutHandle,
                                            uint64 &OutFileSize, FString &OutFinalPath) const
{
    OutHandle = INVALID_HANDLE_VALUE;
    OutFileSize = 0;
    OutFinalPath.Reset();
    if (!bInitialized)
    {
        RecordDenial(CallerPath, TEXT("The restricted IO system is not initialized."));
        return false;
    }

    FString AbsolutePath;
    if (!MakeAbsoluteCandidatePath(CallerPath, AbsolutePath))
    {
        return false;
    }

    HANDLE Handle = CreateFileW(*AbsolutePath, GENERIC_READ, SharedReadFlags, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        RecordDenial(CallerPath,
                     FString::Printf(TEXT("The file could not be opened (Windows error %lu)."), GetLastError()));
        return false;
    }

    FString Error;
    bool bIsDirectory = false;
    uint64 FileSize = 0;
    FString FinalPath;
    if (!GetFinalPathForHandle(Handle, FinalPath, Error) || !GetHandleFileInfo(Handle, bIsDirectory, FileSize, Error))
    {
        CloseHandle(Handle);
        RecordDenial(CallerPath, Error);
        return false;
    }
    if (bIsDirectory)
    {
        CloseHandle(Handle);
        RecordDenial(CallerPath, TEXT("Directories cannot be opened as files."));
        return false;
    }
    if (!IsPathWithinRoot(FinalPath, FinalModelRootPath))
    {
        CloseHandle(Handle);
        RecordDenial(CallerPath, FString::Printf(TEXT("The final path '%s' is outside model root '%s'."), *FinalPath,
                                                 *FinalModelRootPath));
        return false;
    }

    const bool bIsMainModel = FinalPath.Equals(FinalMainModelPath, ESearchCase::IgnoreCase);
    const uint64 PerFileLimit = bIsMainModel ? RuntimeAssetImport::Limits::MaximumMainModelFileBytes
                                             : RuntimeAssetImport::Limits::MaximumAuxiliaryFileBytes;
    if (FileSize > PerFileLimit)
    {
        CloseHandle(Handle);
        RecordDenial(CallerPath,
                     FString::Printf(TEXT("The file is %llu bytes; the limit is %llu bytes."), FileSize, PerFileLimit));
        return false;
    }
    if (!bIsMainModel && IsImageExtension(FinalPath) &&
        FileSize > RuntimeAssetImport::Limits::MaximumCompressedTextureBytes)
    {
        CloseHandle(Handle);
        RecordDenial(CallerPath, FString::Printf(TEXT("The compressed texture is %llu bytes; the limit is %llu bytes."),
                                                 FileSize, RuntimeAssetImport::Limits::MaximumCompressedTextureBytes));
        return false;
    }
    if (FileSize > static_cast<uint64>(std::numeric_limits<size_t>::max()))
    {
        CloseHandle(Handle);
        RecordDenial(CallerPath, TEXT("The file size cannot be represented by size_t."));
        return false;
    }
    if (TrackBudget && !TrackUniqueFile(FinalPath, FileSize))
    {
        CloseHandle(Handle);
        return false;
    }

    OutHandle = Handle;
    OutFileSize = FileSize;
    OutFinalPath = MoveTemp(FinalPath);
    return true;
}

bool FRestrictedAssimpIOSystem::ResolveDirectory(const FString &CallerPath, FString &OutFinalPath) const
{
    FString AbsolutePath;
    if (!MakeAbsoluteCandidatePath(CallerPath, AbsolutePath))
    {
        return false;
    }

    HANDLE Handle = CreateFileW(*AbsolutePath, FILE_READ_ATTRIBUTES, SharedReadFlags, nullptr, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        RecordDenial(CallerPath,
                     FString::Printf(TEXT("The directory could not be opened (Windows error %lu)."), GetLastError()));
        return false;
    }

    FString Error;
    bool bIsDirectory = false;
    uint64 IgnoredSize = 0;
    const bool bResolved = GetFinalPathForHandle(Handle, OutFinalPath, Error) &&
                           GetHandleFileInfo(Handle, bIsDirectory, IgnoredSize, Error);
    CloseHandle(Handle);
    if (!bResolved)
    {
        RecordDenial(CallerPath, Error);
        return false;
    }
    if (!bIsDirectory)
    {
        RecordDenial(CallerPath, TEXT("The requested directory is not a directory."));
        return false;
    }
    if (!IsPathWithinRoot(OutFinalPath, FinalModelRootPath))
    {
        RecordDenial(CallerPath, TEXT("The directory resolves outside the model root."));
        return false;
    }
    return true;
}

bool FRestrictedAssimpIOSystem::MakeAbsoluteCandidatePath(const FString &CallerPath, FString &OutAbsolutePath) const
{
    FString ValidationReason;
    if (!ValidateCallerPath(CallerPath, ValidationReason))
    {
        RecordDenial(CallerPath, ValidationReason);
        return false;
    }

    FString NormalizedPath = CallerPath;
    NormalizedPath.ReplaceInline(TEXT("/"), TEXT("\\"));
    if (FPaths::IsRelative(NormalizedPath))
    {
        FString ProcessAbsolutePath = FPaths::ConvertRelativePathToFull(NormalizedPath);
        ProcessAbsolutePath.ReplaceInline(TEXT("/"), TEXT("\\"));
        if (IsPathWithinRoot(ProcessAbsolutePath, RequestedModelRootAbsolutePath))
        {
            OutAbsolutePath = MoveTemp(ProcessAbsolutePath);
            return true;
        }

        const std::string &Current = CurrentDirectory();
        const FString BaseDirectory = Current.empty() ? FinalModelRootPath : FString(UTF8_TO_TCHAR(Current.c_str()));
        NormalizedPath = FPaths::Combine(BaseDirectory, NormalizedPath);
    }
    OutAbsolutePath = FPaths::ConvertRelativePathToFull(NormalizedPath);
    OutAbsolutePath.ReplaceInline(TEXT("/"), TEXT("\\"));
    return true;
}

bool FRestrictedAssimpIOSystem::TrackUniqueFile(const FString &FinalPath, const uint64 FileSize) const
{
    FScopeLock Lock(&StateMutex);
    const FString Key = FinalPath.ToLower();
    if (UniqueFinalPaths.Contains(Key))
    {
        return true;
    }
    if (UniqueFinalPaths.Num() >= RuntimeAssetImport::Limits::MaximumUniqueOpenedFiles)
    {
        LastDenialReason =
            FString::Printf(TEXT("Unique file limit exceeded: %d files; limit is %d."), UniqueFinalPaths.Num() + 1,
                            RuntimeAssetImport::Limits::MaximumUniqueOpenedFiles);
        if (!bLoggedDenial)
        {
            UE_LOG(LogAssetLoader, Warning, TEXT("Restricted Assimp IO denied access: %s"), *LastDenialReason);
            bLoggedDenial = true;
        }
        return false;
    }
    if (FileSize > RuntimeAssetImport::Limits::MaximumTotalUniqueOpenedBytes - TotalUniqueOpenedBytes)
    {
        LastDenialReason = FString::Printf(
            TEXT("Total unique opened byte budget exceeded: current=%llu, requested=%llu, limit=%llu."),
            TotalUniqueOpenedBytes, FileSize, RuntimeAssetImport::Limits::MaximumTotalUniqueOpenedBytes);
        if (!bLoggedDenial)
        {
            UE_LOG(LogAssetLoader, Warning, TEXT("Restricted Assimp IO denied access: %s"), *LastDenialReason);
            bLoggedDenial = true;
        }
        return false;
    }

    UniqueFinalPaths.Add(Key);
    TotalUniqueOpenedBytes += FileSize;
    return true;
}

bool FRestrictedAssimpIOSystem::ValidateCallerPath(const FString &CallerPath, FString &OutReason) const
{
    if (CallerPath.IsEmpty())
    {
        OutReason = TEXT("Empty paths are not allowed.");
        return false;
    }

    FString NormalizedPath = CallerPath;
    NormalizedPath.ReplaceInline(TEXT("/"), TEXT("\\"));
    if (NormalizedPath.StartsWith(TEXT("\\\\.\\")) || NormalizedPath.StartsWith(TEXT("\\??\\")) ||
        NormalizedPath.StartsWith(TEXT("\\\\?\\")))
    {
        OutReason = TEXT("Device, NT, and extended-length caller paths are not allowed.");
        return false;
    }

    const int32 ColonIndex = NormalizedPath.Find(TEXT(":"));
    if (ColonIndex != INDEX_NONE)
    {
        const bool bDriveLetter = ColonIndex == 1 && FChar::IsAlpha(NormalizedPath[0]) && NormalizedPath.Len() > 2 &&
                                  NormalizedPath[2] == TEXT('\\');
        if (!bDriveLetter)
        {
            const FString SchemeCandidate = NormalizedPath.Left(ColonIndex);
            bool bLooksLikeScheme = !SchemeCandidate.IsEmpty() && FChar::IsAlpha(SchemeCandidate[0]);
            for (int32 Index = 1; bLooksLikeScheme && Index < SchemeCandidate.Len(); ++Index)
            {
                const TCHAR Character = SchemeCandidate[Index];
                bLooksLikeScheme = FChar::IsAlnum(Character) || Character == TEXT('+') || Character == TEXT('-') ||
                                   Character == TEXT('.');
            }
            const bool bIsUrlScheme = bLooksLikeScheme && !SchemeCandidate.Contains(TEXT("."));
            OutReason = bIsUrlScheme ? TEXT("URL and file URI schemes are not allowed.")
                                     : TEXT("Alternate data streams are not allowed.");
            return false;
        }
        if (NormalizedPath.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ColonIndex + 1) !=
            INDEX_NONE)
        {
            OutReason = TEXT("Alternate data streams are not allowed.");
            return false;
        }
    }
    return true;
}

void FRestrictedAssimpIOSystem::RecordDenial(const FString &CallerPath, const FString &Reason) const
{
    const FString Message = FString::Printf(TEXT("Path '%s' was denied: %s"), *CallerPath, *Reason);
    bool bShouldLog = false;
    {
        FScopeLock Lock(&StateMutex);
        LastDenialReason = Message;
        bShouldLog = !bLoggedDenial;
        bLoggedDenial = true;
    }
    if (bShouldLog)
    {
        UE_LOG(LogAssetLoader, Warning, TEXT("Restricted Assimp IO denied access: %s"), *Message);
    }
}

FString FDenyExternalAssimpIOSystem::GetLastDenialReason() const
{
    FScopeLock Lock(&StateMutex);
    return LastDenialReason;
}

bool FDenyExternalAssimpIOSystem::Exists(const char *FilePath) const
{
    RecordDenial(FromAssimpPath(FilePath), TEXT("Exists"));
    return false;
}

char FDenyExternalAssimpIOSystem::getOsSeparator() const { return '\\'; }

Assimp::IOStream *FDenyExternalAssimpIOSystem::Open(const char *FilePath, const char *Mode)
{
    RecordDenial(FromAssimpPath(FilePath), TEXT("Open"));
    return nullptr;
}

void FDenyExternalAssimpIOSystem::Close(Assimp::IOStream *File) { delete File; }

bool FDenyExternalAssimpIOSystem::ComparePaths(const char *FirstPath, const char *SecondPath) const
{
    RecordDenial(FString::Printf(TEXT("%s | %s"), *FromAssimpPath(FirstPath), *FromAssimpPath(SecondPath)),
                 TEXT("ComparePaths"));
    return false;
}

bool FDenyExternalAssimpIOSystem::PushDirectory(const std::string &Path)
{
    RecordDenial(FString(UTF8_TO_TCHAR(Path.c_str())), TEXT("PushDirectory"));
    return false;
}

const std::string &FDenyExternalAssimpIOSystem::CurrentDirectory() const
{
    static const std::string Empty;
    return Empty;
}

size_t FDenyExternalAssimpIOSystem::StackSize() const { return 0; }

bool FDenyExternalAssimpIOSystem::PopDirectory() { return false; }

bool FDenyExternalAssimpIOSystem::CreateDirectory(const std::string &Path) { return false; }

bool FDenyExternalAssimpIOSystem::ChangeDirectory(const std::string &Path) { return false; }

bool FDenyExternalAssimpIOSystem::DeleteFile(const std::string &Path) { return false; }

void FDenyExternalAssimpIOSystem::RecordDenial(const FString &Path, const FString &Operation) const
{
    const FString Message =
        FString::Printf(TEXT("Memory import denied external filesystem %s for '%s'."), *Operation, *Path);
    bool bShouldLog = false;
    {
        FScopeLock Lock(&StateMutex);
        LastDenialReason = Message;
        bShouldLog = !bLoggedDenial;
        bLoggedDenial = true;
    }
    if (bShouldLog)
    {
        UE_LOG(LogAssetLoader, Warning, TEXT("%s"), *Message);
    }
}
