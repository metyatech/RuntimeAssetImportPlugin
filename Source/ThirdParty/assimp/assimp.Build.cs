// Copyright (c) 2026 metyatech. All rights reserved.

using System.IO;
using UnrealBuildTool;

public class assimp : ModuleRules
{
    public assimp(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;
        bEnableUndefinedIdentifierWarnings = false;

        // Add the include directory (prebuilt headers)
        PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "Include"));

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            var libPath = Path.Combine(ModuleDirectory, "Lib", "Win64", "assimp-vc143-mt.lib");
            var dllPath = Path.Combine(ModuleDirectory, "Bin", "Win64", "assimp-vc143-mt.dll");

            // Add the import library
            PublicAdditionalLibraries.Add(libPath);

            // Delay-load the DLL, so we can load it from the right place first
            PublicDelayLoadDLLs.Add(dllPath);

            // Ensure that the DLL is staged along with the executable
            RuntimeDependencies.Add("$(BinaryOutputDir)", dllPath);
        }
    }
}
