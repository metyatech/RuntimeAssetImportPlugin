// Copyright Epic Games, Inc. All Rights Reserved.
// Copyright (c) 2026 metyatech. All rights reserved.

#include "RuntimeAssetImport.h"

#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogRuntimeAssetImport);

#define LOCTEXT_NAMESPACE "FRuntimeAssetImportModule"

void FRuntimeAssetImportModule::StartupModule()
{
    static const TCHAR *AssimpDllName = TEXT("assimp-vc143-mt.dll");
    const FString BaseDirCandidate = FPaths::Combine(FPlatformProcess::BaseDir(), AssimpDllName);
    TArray<FString> CandidatePaths = {BaseDirCandidate, FString(AssimpDllName)};
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("RuntimeAssetImport"));
    if (Plugin.IsValid())
    {
        CandidatePaths.Add(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Source/ThirdParty/assimp/Bin/Win64"),
                                           AssimpDllName));
    }
    else
    {
        UE_LOG(LogRuntimeAssetImport, Warning,
               TEXT("Plugin 'RuntimeAssetImport' was not found through IPluginManager; the plugin-relative Assimp "
                    "candidate will not be tried."));
    }

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
        UE_LOG(LogRuntimeAssetImport, Error, TEXT("Failed to load '%s'. Tried candidates: %s"), AssimpDllName,
               *FString::Join(CandidatePaths, TEXT("; ")));
    }
}

FRuntimeAssetImportModule &FRuntimeAssetImportModule::Get()
{
    return FModuleManager::LoadModuleChecked<FRuntimeAssetImportModule>(TEXT("RuntimeAssetImport"));
}

bool FRuntimeAssetImportModule::IsAssimpAvailable() const
{
    return AssimpDllHandle != nullptr;
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
