using UnrealBuildTool;

public class RuntimeAssetImportTest : ModuleRules
{
    public RuntimeAssetImportTest(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "RuntimeAssetImport", "Projects" });
        PrivateDependencyModuleNames.AddRange(new string[] { "CoreUObject", "Engine" });
    }
}
