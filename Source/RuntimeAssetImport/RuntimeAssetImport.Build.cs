// Copyright (c) 2026 metyatech. All rights reserved.

using UnrealBuildTool;

public class RuntimeAssetImport : ModuleRules
{
    public RuntimeAssetImport(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseRTTI = true;

        if (Target.Platform == UnrealTargetPlatform.Win64 && Target.Version.MajorVersion == 5 &&
            Target.Version.MinorVersion == 4)
        {
            PublicDefinitions.Add("__has_feature(x)=0");
        }

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
