// Copyright (c) 2026 metyatech. All rights reserved.

using UnrealBuildTool;

public class RuntimeAssetImport : ModuleRules
{
    public RuntimeAssetImport(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseRTTI = true;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "ProceduralMeshComponent",
                "GeometryFramework",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "assimp",
                "GeometryCore",
                "MeshConversion",
            }
        );
    }
}
