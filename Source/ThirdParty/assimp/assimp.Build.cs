// Copyright (c) 2026 metyatech. All rights reserved.

using System.IO;
using UnrealBuildTool;

public class assimp : ModuleRules
{
    public assimp(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        if (Target.Platform != UnrealTargetPlatform.Win64)
        {
            throw new BuildException("Runtime Asset Import bundles Assimp only for Windows Win64.");
        }

        string includePath = Path.Combine(ModuleDirectory, "Include");
        string configHeaderPath = Path.Combine(includePath, "assimp", "config.h");
        string revisionHeaderPath = Path.Combine(includePath, "assimp", "revision.h");
        string libPath = Path.Combine(ModuleDirectory, "Lib", "Win64", "assimp-vc143-mt.lib");
        string dllPath = Path.Combine(ModuleDirectory, "Bin", "Win64", "assimp-vc143-mt.dll");

        RequireDirectory(includePath, "Assimp include directory");
        RequireFile(configHeaderPath, "generated Assimp config.h");
        RequireFile(revisionHeaderPath, "generated Assimp revision.h");
        RequireFile(libPath, "Assimp import library");
        RequireFile(dllPath, "Assimp runtime DLL");

        PublicSystemIncludePaths.Add(includePath);
        PublicAdditionalLibraries.Add(libPath);
        PublicDelayLoadDLLs.Add("assimp-vc143-mt.dll");
        RuntimeDependencies.Add("$(TargetOutputDir)/assimp-vc143-mt.dll", dllPath);
    }

    private static void RequireDirectory(string path, string description)
    {
        if (!Directory.Exists(path))
        {
            throw new BuildException($"{description} is missing: {path}");
        }
    }

    private static void RequireFile(string path, string description)
    {
        if (!File.Exists(path))
        {
            throw new BuildException($"{description} is missing: {path}");
        }
    }
}
