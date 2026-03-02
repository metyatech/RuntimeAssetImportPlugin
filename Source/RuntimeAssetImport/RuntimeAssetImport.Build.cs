// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class RuntimeAssetImport : ModuleRules
{
    public RuntimeAssetImport(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(
            new string[] {
				// ... add other public include paths required here ...
            }
            );


        PrivateIncludePaths.AddRange(
            new string[] {
				// ... add other private include paths required here ...
			}
            );


        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "ProceduralMeshComponent",
                "assimp",
                "GeometryFramework",
                "GeometryCore",
                "MeshConversion",
            }
            );

        bEnableUndefinedIdentifierWarnings = false;
        bUseRTTI = true;


        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
				// ... add private dependencies that you statically link with here ...	
			}
            );


        DynamicallyLoadedModuleNames.AddRange(
            new string[]
            {
				// ... add any modules that your module loads dynamically here ...
			}
            );
    }
}
