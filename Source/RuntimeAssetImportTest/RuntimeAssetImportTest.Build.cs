using System.IO;
using UnrealBuildTool;

public class RuntimeAssetImportTest : ModuleRules
{
    public RuntimeAssetImportTest(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "../RuntimeAssetImport/Private"));
        PublicSystemLibraries.Add("bcrypt.lib");

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "RuntimeAssetImport", "Projects" });
        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "UnrealEd",
                "GeometryFramework",
                "ImageWrapper",
                "ProceduralMeshComponent",
                "assimp",
            }
        );
    }
}
