// Copyright Epic Games, Inc. All Rights Reserved.
// Copyright (c) 2026 metyatech. All rights reserved.

#include "RuntimeAssetImport.h"

#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogRuntimeAssetImport);

#define LOCTEXT_NAMESPACE "FRuntimeAssetImportModule"

void FRuntimeAssetImportModule::StartupModule()
{
    static const TCHAR *AssimpDllName = TEXT("assimp-vc143-mt.dll");
    const FString BaseDirCandidate = FPaths::Combine(FPlatformProcess::BaseDir(), AssimpDllName);
    const TArray<FString> CandidatePaths = {BaseDirCandidate, FString(AssimpDllName),
                                            FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("RuntimeAssetImport"),
                                                            TEXT("Source/ThirdParty/assimp/Bin/Win64"), AssimpDllName),
                                            FPaths::Combine(FPaths::EnginePluginsDir(),
                                                            TEXT("Marketplace/RuntimeAssetImport"),
                                                            TEXT("Source/ThirdParty/assimp/Bin/Win64"), AssimpDllName)};
    for (const FString &CandidatePath : CandidatePaths)
    {
        AssimpDllHandle = FPlatformProcess::GetDllHandle(*CandidatePath);
        if (AssimpDllHandle != nullptr)
        {
            return;
        }
    }

    if (AssimpDllHandle == nullptr)
    {
        UE_LOG(LogRuntimeAssetImport, Error, TEXT("Failed to load '%s'. BaseDir candidate: '%s'."), AssimpDllName,
               *BaseDirCandidate);
    }
}

void FRuntimeAssetImportModule::ShutdownModule()
{
    if (AssimpDllHandle != nullptr)
    {
        FPlatformProcess::FreeDllHandle(AssimpDllHandle);
        AssimpDllHandle = nullptr;
    }
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRuntimeAssetImportModule, RuntimeAssetImport)
