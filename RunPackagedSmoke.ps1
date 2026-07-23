[CmdletBinding()]
param(
    [ValidatePattern('^5\.[0-9]+$')]
    [string]$EngineVersion = '5.4',

    [string]$WorkDirectory,

    [switch]$KeepWorkDirectory
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$CreatedDefaultWorkDirectory = [string]::IsNullOrWhiteSpace($WorkDirectory)
$WorkRoot = $null
$CompletedSuccessfully = $false

function ConvertTo-NativeArgument
{
    param([AllowEmptyString()][string]$Argument)

    if ($Argument.Length -gt 0 -and $Argument -notmatch '[\s"]')
    {
        return $Argument
    }

    $Builder = New-Object System.Text.StringBuilder
    [void]$Builder.Append('"')
    $BackslashCount = 0
    foreach ($Character in $Argument.ToCharArray())
    {
        if ($Character -eq '\')
        {
            $BackslashCount++
            continue
        }
        if ($Character -eq '"')
        {
            [void]$Builder.Append(('\' * (($BackslashCount * 2) + 1)))
            [void]$Builder.Append('"')
            $BackslashCount = 0
            continue
        }
        if ($BackslashCount -gt 0)
        {
            [void]$Builder.Append(('\' * $BackslashCount))
            $BackslashCount = 0
        }
        [void]$Builder.Append($Character)
    }
    if ($BackslashCount -gt 0)
    {
        [void]$Builder.Append(('\' * ($BackslashCount * 2)))
    }
    [void]$Builder.Append('"')
    return $Builder.ToString()
}

function Invoke-NativeCommand
{
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [string]$WorkingDirectory = $PSScriptRoot
    )

    $StartInfo = New-Object System.Diagnostics.ProcessStartInfo
    $StartInfo.FileName = $FilePath
    $StartInfo.WorkingDirectory = $WorkingDirectory
    $StartInfo.UseShellExecute = $false
    $StartInfo.CreateNoWindow = $true
    $StartInfo.RedirectStandardOutput = $true
    $StartInfo.RedirectStandardError = $true
    if ($null -ne $StartInfo.PSObject.Properties['ArgumentList'])
    {
        foreach ($Argument in $Arguments)
        {
            [void]$StartInfo.ArgumentList.Add($Argument)
        }
    }
    else
    {
        $QuotedArguments = New-Object System.Collections.Generic.List[string]
        foreach ($Argument in $Arguments)
        {
            $QuotedArguments.Add((ConvertTo-NativeArgument -Argument $Argument))
        }
        $StartInfo.Arguments = [string]::Join(' ', $QuotedArguments)
    }

    Write-Host ("Running: {0} {1}" -f $FilePath, ([string]::Join(' ', $Arguments))) -ForegroundColor Cyan
    $Process = New-Object System.Diagnostics.Process
    $Process.StartInfo = $StartInfo
    if (-not $Process.Start())
    {
        throw "Failed to start external command: $FilePath"
    }
    $StandardOutputTask = $Process.StandardOutput.ReadToEndAsync()
    $StandardErrorTask = $Process.StandardError.ReadToEndAsync()
    $Process.WaitForExit()
    $StandardOutput = $StandardOutputTask.GetAwaiter().GetResult()
    $StandardError = $StandardErrorTask.GetAwaiter().GetResult()
    $ExitCode = $Process.ExitCode
    $Process.Dispose()

    if (-not [string]::IsNullOrWhiteSpace($StandardOutput))
    {
        Write-Host $StandardOutput.TrimEnd()
    }
    if (-not [string]::IsNullOrWhiteSpace($StandardError))
    {
        Write-Host $StandardError.TrimEnd()
    }
    if ($ExitCode -ne 0)
    {
        throw "External command failed with exit code ${ExitCode}: $FilePath"
    }

    return [pscustomobject][ordered]@{
        ExitCode = $ExitCode
        StandardOutput = $StandardOutput
        StandardError = $StandardError
    }
}

function Invoke-BatchFile
{
    param(
        [Parameter(Mandatory = $true)][string]$BatchFile,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory
    )

    Assert-RequiredFile -Path $BatchFile
    $CommandArguments = New-Object System.Collections.Generic.List[string]
    $CommandArguments.Add('/d')
    $CommandArguments.Add('/c')
    $CommandArguments.Add('call')
    $CommandArguments.Add($BatchFile)
    foreach ($Argument in $Arguments)
    {
        $CommandArguments.Add($Argument)
    }
    return Invoke-NativeCommand -FilePath $env:ComSpec -Arguments $CommandArguments.ToArray() `
        -WorkingDirectory $WorkingDirectory
}

function Assert-RequiredFile
{
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not [System.IO.File]::Exists($Path))
    {
        throw "Required file does not exist: $Path"
    }
}

function Assert-RequiredDirectory
{
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not [System.IO.Directory]::Exists($Path))
    {
        throw "Required directory does not exist: $Path"
    }
}

function Get-RequiredCommandPath
{
    param([Parameter(Mandatory = $true)][string]$Name)

    $Commands = @(Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue)
    if ($Commands.Count -eq 0)
    {
        throw "Required command was not found on PATH: $Name"
    }
    return [string]$Commands[0].Source
}

function Remove-DirectorySafely
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$AllowedRoot
    )

    $FullPath = [System.IO.Path]::GetFullPath($Path)
    $FullAllowedRoot = [System.IO.Path]::GetFullPath($AllowedRoot).TrimEnd('\') + '\'
    if (-not $FullPath.StartsWith($FullAllowedRoot, [System.StringComparison]::OrdinalIgnoreCase))
    {
        throw "Refusing to delete directory outside the allowed root: $FullPath"
    }
    if (-not [System.IO.Directory]::Exists($FullPath))
    {
        return
    }
    foreach ($File in [System.IO.Directory]::EnumerateFiles($FullPath, '*', [System.IO.SearchOption]::AllDirectories))
    {
        [System.IO.File]::SetAttributes($File, [System.IO.FileAttributes]::Normal)
    }
    [System.IO.Directory]::Delete($FullPath, $true)
    if ([System.IO.Directory]::Exists($FullPath))
    {
        throw "Directory cleanup failed: $FullPath"
    }
}

function Copy-Directory
{
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    Assert-RequiredDirectory -Path $Source
    [void][System.IO.Directory]::CreateDirectory($Destination)
    $SourceRoot = [System.IO.Path]::GetFullPath($Source).TrimEnd('\') + '\'
    foreach ($SourceDirectory in [System.IO.Directory]::EnumerateDirectories(
            $Source, '*', [System.IO.SearchOption]::AllDirectories))
    {
        $RelativeDirectory = $SourceDirectory.Substring($SourceRoot.Length)
        [void][System.IO.Directory]::CreateDirectory((Join-Path $Destination $RelativeDirectory))
    }
    foreach ($SourceFile in [System.IO.Directory]::EnumerateFiles(
            $Source, '*', [System.IO.SearchOption]::AllDirectories))
    {
        $RelativeFile = $SourceFile.Substring($SourceRoot.Length)
        $DestinationFile = Join-Path $Destination $RelativeFile
        [void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($DestinationFile))
        [System.IO.File]::Copy($SourceFile, $DestinationFile, $true)
    }
}

function Add-SampleModuleDependencies
{
    param([Parameter(Mandatory = $true)][string]$BuildCsPath)

    Assert-RequiredFile -Path $BuildCsPath
    $BuildCsText = [System.IO.File]::ReadAllText($BuildCsPath)
    $DependencyPattern = 'PublicDependencyModuleNames\.AddRange\(new string\[\]\s*\{(?<Dependencies>[^}]*)\}\);'
    $DependencyMatch = [System.Text.RegularExpressions.Regex]::Match(
        $BuildCsText, $DependencyPattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)
    if (-not $DependencyMatch.Success)
    {
        throw "Could not find the sample PublicDependencyModuleNames declaration: $BuildCsPath"
    }
    $Dependencies = New-Object System.Collections.Generic.List[string]
    foreach ($QuotedDependency in [System.Text.RegularExpressions.Regex]::Matches(
            $DependencyMatch.Groups['Dependencies'].Value, '"([^"]+)"'))
    {
        $Dependencies.Add($QuotedDependency.Groups[1].Value)
    }
    foreach ($RequiredDependency in @('RuntimeAssetImport', 'GeometryFramework', 'ProceduralMeshComponent', 'Json'))
    {
        if (-not $Dependencies.Contains($RequiredDependency))
        {
            $Dependencies.Add($RequiredDependency)
        }
    }

    $DependencyLines = New-Object System.Collections.Generic.List[string]
    foreach ($Dependency in $Dependencies)
    {
        $DependencyLines.Add(('                "{0}",' -f $Dependency))
    }
    $Replacement = @(
        'PublicDependencyModuleNames.AddRange(',
        '            new string[]',
        '            {',
        ([string]::Join("`r`n", $DependencyLines)),
        '            }',
        '        );'
    ) -join "`r`n"
    $UpdatedBuildCsText = $BuildCsText.Substring(0, $DependencyMatch.Index) + $Replacement +
        $BuildCsText.Substring($DependencyMatch.Index + $DependencyMatch.Length)
    [System.IO.File]::WriteAllText($BuildCsPath, $UpdatedBuildCsText, $Utf8NoBom)
}

function Enable-ProjectPlugin
{
    param(
        [Parameter(Mandatory = $true)][object]$ProjectDescriptor,
        [Parameter(Mandatory = $true)][string]$PluginName
    )

    $Plugins = @($ProjectDescriptor.Plugins)
    $ExistingPlugin = @($Plugins | Where-Object { $_.Name -eq $PluginName })
    if ($ExistingPlugin.Count -gt 1)
    {
        throw "Project descriptor contains duplicate plugin entries: $PluginName"
    }
    if ($ExistingPlugin.Count -eq 1)
    {
        $ExistingPlugin[0].Enabled = $true
        return
    }
    $ProjectDescriptor.Plugins = @($Plugins + [pscustomobject][ordered]@{
            Name = $PluginName
            Enabled = $true
        })
}

try
{
    $GitPath = Get-RequiredCommandPath -Name 'git.exe'
    $PwshPath = Get-RequiredCommandPath -Name 'pwsh.exe'
    $PowerShellPath = Get-RequiredCommandPath -Name 'powershell.exe'

    $SourceSampleRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\RuntimeAssetImportSample'))
    Assert-RequiredDirectory -Path $SourceSampleRoot
    $SampleStatus = Invoke-NativeCommand -FilePath $GitPath -Arguments @('-C', $SourceSampleRoot, 'status', '--short')
    if (-not [string]::IsNullOrWhiteSpace($SampleStatus.StandardOutput))
    {
        throw "RuntimeAssetImportSample source repository must be clean: $SourceSampleRoot"
    }
    $SampleHeadResult = Invoke-NativeCommand -FilePath $GitPath -Arguments @('-C', $SourceSampleRoot, 'rev-parse', 'HEAD')
    $ExpectedSampleHead = $SampleHeadResult.StandardOutput.Trim()

    if ($CreatedDefaultWorkDirectory)
    {
        $WorkRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
            ("rai-smoke-{0}" -f [System.Guid]::NewGuid().ToString('N').Substring(0, 8))
    }
    else
    {
        $WorkRoot = [System.IO.Path]::GetFullPath($WorkDirectory)
    }
    if ([System.IO.Directory]::Exists($WorkRoot))
    {
        if ([System.IO.Directory]::EnumerateFileSystemEntries($WorkRoot).GetEnumerator().MoveNext())
        {
            throw "WorkDirectory must be absent or empty: $WorkRoot"
        }
    }
    else
    {
        [void][System.IO.Directory]::CreateDirectory($WorkRoot)
    }
    Write-Host "Packaged smoke work directory: $WorkRoot" -ForegroundColor Green

    $TempSampleRoot = Join-Path $WorkRoot 'S'
    [void](Invoke-NativeCommand -FilePath $GitPath `
            -Arguments @('clone', '--recursive', '--no-local', $SourceSampleRoot, $TempSampleRoot) `
            -WorkingDirectory $WorkRoot)
    $ClonedHeadResult = Invoke-NativeCommand -FilePath $GitPath -Arguments @('-C', $TempSampleRoot, 'rev-parse', 'HEAD')
    $ClonedHead = $ClonedHeadResult.StandardOutput.Trim()
    if ($ClonedHead -cne $ExpectedSampleHead)
    {
        throw "Cloned Sample HEAD mismatch. Expected $ExpectedSampleHead but found $ClonedHead."
    }

    $PackageOutput = Join-Path $WorkRoot 'P'
    [void](Invoke-NativeCommand -FilePath $PwshPath -Arguments @(
            '-NoProfile', '-File', (Join-Path $PSScriptRoot 'PackageForFab.ps1'),
            '-EngineVersion', $EngineVersion, '-OutputDirectory', $PackageOutput))
    $StagedPluginRoot = Join-Path $PackageOutput "PackedForFab\UE$EngineVersion\RuntimeAssetImport"
    Assert-RequiredDirectory -Path $StagedPluginRoot

    $TempPluginRoot = Join-Path $TempSampleRoot 'Plugins\RuntimeAssetImport'
    Remove-DirectorySafely -Path $TempPluginRoot -AllowedRoot $TempSampleRoot
    Copy-Directory -Source $StagedPluginRoot -Destination $TempPluginRoot
    if ([System.IO.Directory]::Exists((Join-Path $TempPluginRoot 'Source\RuntimeAssetImportTest')))
    {
        throw 'Customer-staged plugin unexpectedly contains RuntimeAssetImportTest.'
    }
    $StagedDescriptor = Get-Content -Raw -LiteralPath (Join-Path $TempPluginRoot 'RuntimeAssetImport.uplugin') |
        ConvertFrom-Json
    if ($StagedDescriptor.Modules.Count -ne 1 -or $StagedDescriptor.Modules[0].Name -ne 'RuntimeAssetImport' -or
        $StagedDescriptor.Modules[0].Type -ne 'Runtime')
    {
        throw 'Customer-staged plugin descriptor must contain only the RuntimeAssetImport Runtime module.'
    }

    $SampleModuleRoot = Join-Path $TempSampleRoot 'Source\RuntimeAssetImportSample'
    foreach ($SmokeSourceFile in @(
            'RuntimeAssetImportSmokeGameInstance.h',
            'RuntimeAssetImportSmokeGameInstance.cpp'))
    {
        $SourceFile = Join-Path $PSScriptRoot "Tests\PackagedSmoke\$SmokeSourceFile"
        Assert-RequiredFile -Path $SourceFile
        [System.IO.File]::Copy($SourceFile, (Join-Path $SampleModuleRoot $SmokeSourceFile), $true)
    }
    Add-SampleModuleDependencies -BuildCsPath (Join-Path $SampleModuleRoot 'RuntimeAssetImportSample.Build.cs')

    $GameTargetPath = Join-Path $TempSampleRoot 'Source\RuntimeAssetImportSample.Target.cs'
    Assert-RequiredFile -Path $GameTargetPath
    $GameTargetText = [System.IO.File]::ReadAllText($GameTargetPath)
    if ($GameTargetText -notmatch '\bbOverrideBuildEnvironment\s*=')
    {
        $TargetTypeLine = '        Type = TargetType.Game;'
        if ($GameTargetText.IndexOf($TargetTypeLine, [System.StringComparison]::Ordinal) -lt 0)
        {
            throw "Could not find the Game target type declaration: $GameTargetPath"
        }
        $GameTargetText = $GameTargetText.Replace(
            $TargetTypeLine, $TargetTypeLine +
                "`r`n        bOverrideBuildEnvironment = true;")
        [System.IO.File]::WriteAllText($GameTargetPath, $GameTargetText, $Utf8NoBom)
    }

    $ProjectPath = Join-Path $TempSampleRoot 'RuntimeAssetImportSample.uproject'
    $ProjectDescriptor = Get-Content -Raw -LiteralPath $ProjectPath | ConvertFrom-Json
    Enable-ProjectPlugin -ProjectDescriptor $ProjectDescriptor -PluginName 'PythonScriptPlugin'
    Enable-ProjectPlugin -ProjectDescriptor $ProjectDescriptor -PluginName 'EditorScriptingUtilities'
    [System.IO.File]::WriteAllText(
        $ProjectPath, (($ProjectDescriptor | ConvertTo-Json -Depth 20) + "`n"), $Utf8NoBom)

    $DefaultGamePath = Join-Path $TempSampleRoot 'Config\DefaultGame.ini'
    Assert-RequiredFile -Path $DefaultGamePath
    $DefaultGameText = [System.IO.File]::ReadAllText($DefaultGamePath)
    $ProjectIdMatch = [System.Text.RegularExpressions.Regex]::Match(
        $DefaultGameText, '(?m)^ProjectID=([0-9A-Fa-f-]+)\s*$')
    if (-not $ProjectIdMatch.Success)
    {
        throw "Could not find ProjectID in temp Sample config: $DefaultGamePath"
    }
    $NormalizedProjectId = $ProjectIdMatch.Groups[1].Value.Replace('-', '')
    $DefaultGameText = $DefaultGameText.Substring(0, $ProjectIdMatch.Groups[1].Index) + $NormalizedProjectId +
        $DefaultGameText.Substring($ProjectIdMatch.Groups[1].Index + $ProjectIdMatch.Groups[1].Length)
    $DefaultGameText = $DefaultGameText.TrimEnd("`r", "`n") + @'

[/Script/UnrealEd.ProjectPackagingSettings]
+DirectoriesToAlwaysStageAsNonUFS=(Path="SmokeAssets")
'@ + "`n"
    [System.IO.File]::WriteAllText($DefaultGamePath, $DefaultGameText, $Utf8NoBom)

    $DefaultEnginePath = Join-Path $TempSampleRoot 'Config\DefaultEngine.ini'
    Assert-RequiredFile -Path $DefaultEnginePath
    $DefaultEngineText = [System.IO.File]::ReadAllText($DefaultEnginePath).TrimEnd("`r", "`n")
    $SmokeConfiguration = @'

[/Script/EngineSettings.GameMapsSettings]
GameDefaultMap=/Game/Smoke/SmokeMap
GameInstanceClass=/Script/RuntimeAssetImportSample.RuntimeAssetImportSmokeGameInstance

[/Script/UnrealEd.ProjectPackagingSettings]
+DirectoriesToAlwaysStageAsNonUFS=(Path="SmokeAssets")
+DirectoriesToAlwaysCook=(Path="/RuntimeAssetImport")
'@
    [System.IO.File]::WriteAllText($DefaultEnginePath, $DefaultEngineText + $SmokeConfiguration + "`n", $Utf8NoBom)

    $SmokeAssetDestination = Join-Path $TempSampleRoot 'Content\SmokeAssets'
    [void][System.IO.Directory]::CreateDirectory($SmokeAssetDestination)
    foreach ($SmokeAssetFile in @(
            'test_triangle.fbx',
            'test_triangle.obj',
            'test_triangle.mtl',
            'test_triangle.dae',
            'test_scene.gltf',
            'test_triangle.glb',
            'test_external_texture.obj',
            'test_external_texture.mtl',
            'textures\test_red.png',
            'test_embedded_texture.gltf',
            'test_external_buffer.gltf',
            'buffers\test_triangle.bin'))
    {
        $SourceAsset = Join-Path $PSScriptRoot "Source\RuntimeAssetImportTest\TestAssets\$SmokeAssetFile"
        Assert-RequiredFile -Path $SourceAsset
        $DestinationAsset = Join-Path $SmokeAssetDestination $SmokeAssetFile
        [void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($DestinationAsset))
        [System.IO.File]::Copy($SourceAsset, $DestinationAsset, $true)
    }
    $ExpectedRedPngHash = '49e1dad481e94dfab7c9573a9a81d56aa2ca629fe15a3f7a910aa4f47601c00d'
    $ActualRedPngHash = (Get-FileHash -LiteralPath (Join-Path $SmokeAssetDestination 'textures\test_red.png') `
            -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($ActualRedPngHash -cne $ExpectedRedPngHash)
    {
        throw "Staged red PNG SHA-256 mismatch: $ActualRedPngHash"
    }

    $EngineResolverPath = Join-Path $TempSampleRoot 'UnrealBuildRunTestScript\Get-UEInstallPath.ps1'
    $EngineResult = Invoke-NativeCommand -FilePath $PowerShellPath -Arguments @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $EngineResolverPath, '-Version', $EngineVersion)
    $EngineRoot = $EngineResult.StandardOutput.Trim()
    Assert-RequiredDirectory -Path $EngineRoot

    $BuildBatch = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
    [void](Invoke-BatchFile -BatchFile $BuildBatch -Arguments @(
            'RuntimeAssetImportSampleEditor', 'Win64', 'Development',
            ("-Project=$ProjectPath"), '-WaitMutex', '-NoHotReload') -WorkingDirectory $TempSampleRoot)

    $CreateMapScript = Join-Path $PSScriptRoot 'Tests\PackagedSmoke\CreateSmokeMap.py'
    Assert-RequiredFile -Path $CreateMapScript
    $UnrealEditorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    Assert-RequiredFile -Path $UnrealEditorCmd
    [void](Invoke-NativeCommand -FilePath $UnrealEditorCmd -Arguments @(
            $ProjectPath, '-run=pythonscript', ("-script=$CreateMapScript"),
            '-unattended', '-nop4', '-nosplash', '-nosound', '-nullrhi') -WorkingDirectory $TempSampleRoot)
    Assert-RequiredFile -Path (Join-Path $TempSampleRoot 'Content\Smoke\SmokeMap.umap')

    $ArchiveDirectory = Join-Path $WorkRoot 'A'
    $RunUatBatch = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
    [void](Invoke-BatchFile -BatchFile $RunUatBatch -Arguments @(
            'BuildCookRun', ("-project=$ProjectPath"), '-noP4', '-platform=Win64', '-clientconfig=Shipping',
            '-build', '-cook', '-stage', '-pak', '-archive', ("-archivedirectory=$ArchiveDirectory"),
            '-unattended', '-utf8output') -WorkingDirectory $TempSampleRoot)

    $PackagedExecutables = [System.IO.Directory]::GetFiles(
        $ArchiveDirectory, 'RuntimeAssetImportSample.exe', [System.IO.SearchOption]::AllDirectories)
    if ($PackagedExecutables.Length -ne 1)
    {
        throw "Expected exactly one packaged RuntimeAssetImportSample.exe, found $($PackagedExecutables.Length)."
    }
    $PackagedExecutable = $PackagedExecutables[0]
    $RuntimeUserDirectory = Join-Path $WorkRoot 'U'
    [void][System.IO.Directory]::CreateDirectory($RuntimeUserDirectory)
    $RuntimeLogPath = Join-Path $WorkRoot 'smoke.log'

    $StartInfo = New-Object System.Diagnostics.ProcessStartInfo
    $StartInfo.FileName = $PackagedExecutable
    $StartInfo.WorkingDirectory = [System.IO.Path]::GetDirectoryName($PackagedExecutable)
    $StartInfo.UseShellExecute = $false
    $StartInfo.CreateNoWindow = $true
    $StartInfo.RedirectStandardOutput = $true
    $StartInfo.RedirectStandardError = $true
    $RuntimeArguments = @(
        '-unattended', '-nosplash', '-nosound', '-RenderOffscreen',
        ("-abslog=$RuntimeLogPath"), ("-UserDir=$RuntimeUserDirectory"))
    if ($null -ne $StartInfo.PSObject.Properties['ArgumentList'])
    {
        foreach ($RuntimeArgument in $RuntimeArguments)
        {
            [void]$StartInfo.ArgumentList.Add($RuntimeArgument)
        }
    }
    else
    {
        $StartInfo.Arguments = [string]::Join(' ', @($RuntimeArguments | ForEach-Object {
                    ConvertTo-NativeArgument -Argument $_
                }))
    }

    Write-Host "Running packaged smoke executable: $PackagedExecutable" -ForegroundColor Cyan
    $RuntimeProcess = New-Object System.Diagnostics.Process
    $RuntimeProcess.StartInfo = $StartInfo
    if (-not $RuntimeProcess.Start())
    {
        throw "Failed to start packaged executable: $PackagedExecutable"
    }
    $RuntimeOutputTask = $RuntimeProcess.StandardOutput.ReadToEndAsync()
    $RuntimeErrorTask = $RuntimeProcess.StandardError.ReadToEndAsync()
    if (-not $RuntimeProcess.WaitForExit(120000))
    {
        $RuntimeProcess.Kill()
        $RuntimeProcess.WaitForExit()
        throw 'Packaged smoke executable did not exit within 120 seconds.'
    }
    $RuntimeOutput = $RuntimeOutputTask.GetAwaiter().GetResult()
    $RuntimeError = $RuntimeErrorTask.GetAwaiter().GetResult()
    $RuntimeExitCode = $RuntimeProcess.ExitCode
    $RuntimeProcess.Dispose()
    if ($RuntimeExitCode -ne 0)
    {
        throw "Packaged smoke executable exited with code $RuntimeExitCode.`n$RuntimeOutput`n$RuntimeError"
    }
    $RuntimeTranscript = [string]::Join("`n", @($RuntimeOutput, $RuntimeError))
    if ([System.IO.File]::Exists($RuntimeLogPath))
    {
        if (-not [string]::IsNullOrWhiteSpace($RuntimeTranscript))
        {
            [System.IO.File]::AppendAllText($RuntimeLogPath, "`n$RuntimeTranscript", $Utf8NoBom)
        }
    }
    else
    {
        [System.IO.File]::WriteAllText($RuntimeLogPath, $RuntimeTranscript, $Utf8NoBom)
    }

    $SmokeResultFiles = [System.IO.Directory]::GetFiles(
        $WorkRoot, 'RuntimeAssetImportSmoke.json', [System.IO.SearchOption]::AllDirectories)
    if ($SmokeResultFiles.Length -ne 1)
    {
        throw "Expected exactly one RuntimeAssetImportSmoke.json, found $($SmokeResultFiles.Length)."
    }
    $SmokeResultPath = $SmokeResultFiles[0]
    $SmokeResult = Get-Content -Raw -LiteralPath $SmokeResultPath | ConvertFrom-Json
    if ($SmokeResult.OverallSuccess -ne $true)
    {
        throw "Packaged smoke reported OverallSuccess=false: $SmokeResultPath"
    }
    $ExpectedFormats = @(
        'FBX',
        'OBJ',
        'DAE',
        'glTF',
        'GLB',
        'ExternalTexture',
        'EmbeddedTextureFile',
        'EmbeddedTextureMemory',
        'ExternalBuffer')
    $ActualFormats = @($SmokeResult.Formats | ForEach-Object { $_.Format } | Sort-Object)
    if ([string]::Join("`n", $ActualFormats) -cne [string]::Join("`n", @($ExpectedFormats | Sort-Object)))
    {
        throw "Packaged smoke format set mismatch: $($ActualFormats -join ', ')"
    }
    foreach ($FormatResult in $SmokeResult.Formats)
    {
        foreach ($RequiredBooleanField in @(
                'ImportSuccess',
                'ComponentRegistered',
                'MaterialSlot0Valid',
                'BoundsNonZero',
                'CollisionEnabled',
                'CollisionData',
                'CollisionHit',
                'AttachedToOwnerRoot',
                'FollowedOwnerTransform',
                'ColorStatusValid',
                'ImportedColorValid',
                'TextureBytesValid',
                'MaterialScalarValid',
                'MaterialVectorValid',
                'MaterialTextureValid',
                'MemoryExternalAccessDenied'))
        {
            if ($FormatResult.$RequiredBooleanField -ne $true)
            {
                throw "$($FormatResult.Format) smoke field is not true: $RequiredBooleanField"
            }
        }
        if ([int]$FormatResult.TriangleCount -le 0 -or [int]$FormatResult.MaterialCount -le 0)
        {
            throw "$($FormatResult.Format) smoke geometry or material count is not positive."
        }
    }

    $PackagedDlls = [System.IO.Directory]::GetFiles(
        $ArchiveDirectory, 'assimp-vc143-mt.dll', [System.IO.SearchOption]::AllDirectories)
    if ($PackagedDlls.Length -lt 1)
    {
        throw 'The packaged output does not contain assimp-vc143-mt.dll.'
    }
    Assert-RequiredFile -Path $RuntimeLogPath
    $RuntimeLogText = [System.IO.File]::ReadAllText($RuntimeLogPath)
    foreach ($ForbiddenLogPattern in @(
            "Failed to load 'assimp-vc143-mt.dll'",
            'missing import',
            'fatal error',
            'unhandled exception'))
    {
        if ($RuntimeLogText.IndexOf($ForbiddenLogPattern, [System.StringComparison]::OrdinalIgnoreCase) -ge 0)
        {
            throw "Packaged runtime log contains forbidden text: $ForbiddenLogPattern"
        }
    }

    Write-Host "Packaged smoke JSON: $SmokeResultPath" -ForegroundColor Green
    Write-Host ("Packaged Assimp DLL: {0}" -f $PackagedDlls[0]) -ForegroundColor Green
    Write-Host 'Packaged Shipping smoke passed for the five baseline formats, external and embedded textures, external buffers, material values, and memory I/O denial.' -ForegroundColor Green
    $CompletedSuccessfully = $true
}
finally
{
    if ($CompletedSuccessfully -and -not $KeepWorkDirectory -and $null -ne $WorkRoot -and
        [System.IO.Directory]::Exists($WorkRoot))
    {
        $CleanupRoot = if ($CreatedDefaultWorkDirectory) {
            [System.IO.Path]::GetTempPath()
        }
        else {
            [System.IO.Path]::GetDirectoryName([System.IO.Path]::GetFullPath($WorkRoot))
        }
        Remove-DirectorySafely -Path $WorkRoot -AllowedRoot $CleanupRoot
        Write-Host "Removed packaged smoke work directory: $WorkRoot"
    }
    elseif ($null -ne $WorkRoot -and [System.IO.Directory]::Exists($WorkRoot))
    {
        Write-Host "Packaged smoke work directory retained: $WorkRoot" -ForegroundColor Yellow
    }
}
