// Copyright (c) 2026 metyatech. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Windows/WindowsHWrapper.h"

#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>

#include <string>
#include <vector>

class RUNTIMEASSETIMPORT_API FRestrictedAssimpIOStream final : public Assimp::IOStream
{
public:
    FRestrictedAssimpIOStream(HANDLE InFileHandle, uint64 InFileSize);
    ~FRestrictedAssimpIOStream() override;

    size_t Read(void *Buffer, size_t ElementSize, size_t ElementCount) override;
    size_t Write(const void *Buffer, size_t ElementSize, size_t ElementCount) override;
    aiReturn Seek(size_t Offset, aiOrigin Origin) override;
    size_t Tell() const override;
    size_t FileSize() const override;
    void Flush() override;

private:
    HANDLE FileHandle = INVALID_HANDLE_VALUE;
    uint64 Size = 0;
    uint64 Position = 0;
};

class RUNTIMEASSETIMPORT_API FRestrictedAssimpIOSystem final : public Assimp::IOSystem
{
public:
    explicit FRestrictedAssimpIOSystem(const FString &MainModelPath);
    ~FRestrictedAssimpIOSystem() override = default;

    bool IsInitialized() const;
    FString GetLastDenialReason() const;

    bool Exists(const char *FilePath) const override;
    char getOsSeparator() const override;
    Assimp::IOStream *Open(const char *FilePath, const char *Mode = "rb") override;
    void Close(Assimp::IOStream *File) override;
    bool ComparePaths(const char *FirstPath, const char *SecondPath) const override;
    bool PushDirectory(const std::string &Path) override;
    const std::string &CurrentDirectory() const override;
    size_t StackSize() const override;
    bool PopDirectory() override;
    bool CreateDirectory(const std::string &Path) override;
    bool ChangeDirectory(const std::string &Path) override;
    bool DeleteFile(const std::string &Path) override;

private:
    bool ResolveFile(const FString &CallerPath, bool TrackBudget, HANDLE &OutHandle, uint64 &OutFileSize,
                     FString &OutFinalPath) const;
    bool ResolveDirectory(const FString &CallerPath, FString &OutFinalPath) const;
    bool MakeAbsoluteCandidatePath(const FString &CallerPath, FString &OutAbsolutePath) const;
    bool TrackUniqueFile(const FString &FinalPath, uint64 FileSize) const;
    bool ValidateCallerPath(const FString &CallerPath, FString &OutReason) const;
    void RecordDenial(const FString &CallerPath, const FString &Reason) const;

    bool bInitialized = false;
    FString RequestedMainModelAbsolutePath;
    FString RequestedModelRootAbsolutePath;
    FString FinalModelRootPath;
    FString FinalMainModelPath;
    std::vector<std::string> DirectoryStack;

    mutable FCriticalSection StateMutex;
    mutable FString LastDenialReason;
    mutable bool bLoggedDenial = false;
    mutable TSet<FString> UniqueFinalPaths;
    mutable uint64 TotalUniqueOpenedBytes = 0;
};

class RUNTIMEASSETIMPORT_API FDenyExternalAssimpIOSystem final : public Assimp::IOSystem
{
public:
    FDenyExternalAssimpIOSystem() = default;
    ~FDenyExternalAssimpIOSystem() override = default;

    FString GetLastDenialReason() const;

    bool Exists(const char *FilePath) const override;
    char getOsSeparator() const override;
    Assimp::IOStream *Open(const char *FilePath, const char *Mode = "rb") override;
    void Close(Assimp::IOStream *File) override;
    bool ComparePaths(const char *FirstPath, const char *SecondPath) const override;
    bool PushDirectory(const std::string &Path) override;
    const std::string &CurrentDirectory() const override;
    size_t StackSize() const override;
    bool PopDirectory() override;
    bool CreateDirectory(const std::string &Path) override;
    bool ChangeDirectory(const std::string &Path) override;
    bool DeleteFile(const std::string &Path) override;

private:
    void RecordDenial(const FString &Path, const FString &Operation) const;

    mutable FCriticalSection StateMutex;
    mutable FString LastDenialReason;
    mutable bool bLoggedDenial = false;
};
