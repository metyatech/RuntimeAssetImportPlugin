// Copyright Epic Games, Inc. All Rights Reserved.
// Copyright (c) 2026 metyatech. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRuntimeAssetImport, Log, All);

class FRuntimeAssetImportModule : public IModuleInterface
{
public:
    static FRuntimeAssetImportModule &Get();
    bool IsAssimpAvailable() const;

    /** IModuleInterface implementation */
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void *AssimpDllHandle = nullptr;
};
