param(
    [ValidatePattern('^5\.[0-9]+$')]
    [string]$EngineVersion = '5.4',

    [string]$OutputDirectory = $PSScriptRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function ConvertTo-NativeArgument {
    param([AllowEmptyString()][string]$Argument)

    if ($Argument.Length -gt 0 -and $Argument -notmatch '[\s"]') {
        return $Argument
    }

    $builder = New-Object System.Text.StringBuilder
    [void]$builder.Append('"')
    $backslashCount = 0
    foreach ($character in $Argument.ToCharArray()) {
        if ($character -eq '\') {
            $backslashCount++
            continue
        }
        if ($character -eq '"') {
            [void]$builder.Append(('\' * (($backslashCount * 2) + 1)))
            [void]$builder.Append('"')
            $backslashCount = 0
            continue
        }
        if ($backslashCount -gt 0) {
            [void]$builder.Append(('\' * $backslashCount))
            $backslashCount = 0
        }
        [void]$builder.Append($character)
    }
    if ($backslashCount -gt 0) {
        [void]$builder.Append(('\' * ($backslashCount * 2)))
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Remove-DirectorySafely {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$AllowedRoot
    )

    $resolvedPath = [System.IO.Path]::GetFullPath($Path)
    $resolvedRoot = [System.IO.Path]::GetFullPath($AllowedRoot).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
    $requiredPrefix = $resolvedRoot + [System.IO.Path]::DirectorySeparatorChar
    if (-not $resolvedPath.StartsWith($requiredPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove path outside output directory: $resolvedPath"
    }
    if (-not [System.IO.Directory]::Exists($resolvedPath)) {
        return
    }

    foreach ($file in [System.IO.Directory]::EnumerateFiles(
            $resolvedPath, '*', [System.IO.SearchOption]::AllDirectories)) {
        [System.IO.File]::SetAttributes($file, [System.IO.FileAttributes]::Normal)
    }
    [System.IO.Directory]::Delete($resolvedPath, $true)
    if ([System.IO.Directory]::Exists($resolvedPath)) {
        throw "Failed to remove directory: $resolvedPath"
    }
}

function Require-File {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not [System.IO.File]::Exists($Path)) {
        throw "Required file is missing: $Path"
    }
}

function Require-Directory {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not [System.IO.Directory]::Exists($Path)) {
        throw "Required directory is missing: $Path"
    }
}

function Copy-DirectoryWithRobocopy {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    Require-Directory -Path $Source
    [System.IO.Directory]::CreateDirectory($Destination) | Out-Null

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = 'robocopy.exe'
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $arguments = @($Source, $Destination, '/E', '/COPY:DAT', '/DCOPY:DAT', '/R:2', '/W:1',
        '/NFL', '/NDL', '/NJH', '/NJS', '/NP')
    if ($null -ne $startInfo.PSObject.Properties['ArgumentList']) {
        foreach ($argument in $arguments) {
            [void]$startInfo.ArgumentList.Add($argument)
        }
    }
    else {
        $quotedArguments = @($arguments | ForEach-Object { ConvertTo-NativeArgument -Argument $_ })
        $startInfo.Arguments = [string]::Join(' ', $quotedArguments)
    }
    $process = [System.Diagnostics.Process]::Start($startInfo)
    $process.WaitForExit()
    $exitCode = $process.ExitCode
    $process.Dispose()
    if ($exitCode -lt 0 -or $exitCode -gt 7) {
        throw "robocopy failed with exit code ${exitCode}: $Source"
    }
}

function Copy-RequiredFile {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    Require-File -Path $Source
    $destinationParent = [System.IO.Path]::GetDirectoryName($Destination)
    [System.IO.Directory]::CreateDirectory($destinationParent) | Out-Null
    [System.IO.File]::Copy($Source, $Destination, $true)
}

$repositoryRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$resolvedOutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($resolvedOutputDirectory) | Out-Null

$versionName = '1.0.0'
$engineFolderName = "UE$EngineVersion"
$stagingRoot = [System.IO.Path]::Combine($resolvedOutputDirectory, 'PackedForFab', $engineFolderName)
$stagedPluginRoot = [System.IO.Path]::Combine($stagingRoot, 'RuntimeAssetImport')
$zipPath = [System.IO.Path]::Combine(
    $resolvedOutputDirectory, "RuntimeAssetImport_${versionName}_${engineFolderName}_Win64.zip")

Remove-DirectorySafely -Path $stagingRoot -AllowedRoot $resolvedOutputDirectory
if ([System.IO.File]::Exists($zipPath)) {
    [System.IO.File]::SetAttributes($zipPath, [System.IO.FileAttributes]::Normal)
    [System.IO.File]::Delete($zipPath)
}
[System.IO.Directory]::CreateDirectory($stagedPluginRoot) | Out-Null

$directoryMappings = [ordered]@{
    'Config' = 'Config'
    'Content' = 'Content'
    'Resources' = 'Resources'
    'Source/RuntimeAssetImport' = 'Source/RuntimeAssetImport'
    'Source/ThirdParty/assimp' = 'Source/ThirdParty/assimp'
}
foreach ($mapping in $directoryMappings.GetEnumerator()) {
    $sourceDirectory = [System.IO.Path]::Combine($repositoryRoot, $mapping.Key)
    $destinationDirectory = [System.IO.Path]::Combine($stagedPluginRoot, $mapping.Value)
    Copy-DirectoryWithRobocopy -Source $sourceDirectory -Destination $destinationDirectory
}

$fileMappings = [ordered]@{
    'RuntimeAssetImport.uplugin' = 'RuntimeAssetImport.uplugin'
    'README.md' = 'README.md'
    'LICENSE' = 'LICENSE'
    'THIRD_PARTY_NOTICES.md' = 'THIRD_PARTY_NOTICES.md'
}
foreach ($mapping in $fileMappings.GetEnumerator()) {
    $sourceFile = [System.IO.Path]::Combine($repositoryRoot, $mapping.Key)
    $destinationFile = [System.IO.Path]::Combine($stagedPluginRoot, $mapping.Value)
    Copy-RequiredFile -Source $sourceFile -Destination $destinationFile
}

$stagedDescriptorPath = [System.IO.Path]::Combine($stagedPluginRoot, 'RuntimeAssetImport.uplugin')
$descriptor = Get-Content -LiteralPath $stagedDescriptorPath -Raw | ConvertFrom-Json
$descriptor.EngineVersion = "$EngineVersion.0"
$descriptor.Installed = $true
$descriptor.Modules = @($descriptor.Modules | Where-Object { $_.Name -eq 'RuntimeAssetImport' })
if ($descriptor.Modules.Count -ne 1 -or $descriptor.Modules[0].Type -ne 'Runtime') {
    throw 'The staged descriptor must contain exactly one RuntimeAssetImport Runtime module.'
}
$descriptorJson = $descriptor | ConvertTo-Json -Depth 20
$utf8WithoutBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($stagedDescriptorPath, $descriptorJson + [Environment]::NewLine, $utf8WithoutBom)

Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $stagingRoot,
    $zipPath,
    [System.IO.Compression.CompressionLevel]::Optimal,
    $false)

$requiredEntries = @(
    'RuntimeAssetImport/RuntimeAssetImport.uplugin',
    'RuntimeAssetImport/Config/FilterPlugin.ini',
    'RuntimeAssetImport/Content/AssetImporterMeshMaterial.uasset',
    'RuntimeAssetImport/Resources/Icon128.png',
    'RuntimeAssetImport/Source/RuntimeAssetImport/RuntimeAssetImport.Build.cs',
    'RuntimeAssetImport/Source/RuntimeAssetImport/Private/RestrictedAssimpIOSystem.h',
    'RuntimeAssetImport/Source/RuntimeAssetImport/Private/RestrictedAssimpIOSystem.cpp',
    'RuntimeAssetImport/Source/RuntimeAssetImport/Private/AssetImportLimits.h',
    'RuntimeAssetImport/Source/ThirdParty/assimp/assimp.Build.cs',
    'RuntimeAssetImport/Source/ThirdParty/assimp/BUILD-INFO.json',
    'RuntimeAssetImport/Source/ThirdParty/assimp/LICENSE',
    'RuntimeAssetImport/Source/ThirdParty/assimp/LICENSES/contrib/Open3DGC/LICENSE.txt',
    'RuntimeAssetImport/Source/ThirdParty/assimp/LICENSES/contrib/stb/LICENSE.txt',
    'RuntimeAssetImport/Source/ThirdParty/assimp/LICENSES/contrib/unzip/MiniZip64_info.txt',
    'RuntimeAssetImport/Source/ThirdParty/assimp/Bin/Win64/assimp-vc143-mt.dll',
    'RuntimeAssetImport/Source/ThirdParty/assimp/Lib/Win64/assimp-vc143-mt.lib',
    'RuntimeAssetImport/README.md',
    'RuntimeAssetImport/LICENSE',
    'RuntimeAssetImport/THIRD_PARTY_NOTICES.md'
)

$archive = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    $entries = @($archive.Entries | ForEach-Object { $_.FullName.Replace('\\', '/') })
    $topLevelDirectories = @($entries | ForEach-Object { ($_ -split '/')[0] } | Sort-Object -Unique)
    if ($topLevelDirectories.Count -ne 1 -or $topLevelDirectories[0] -ne 'RuntimeAssetImport') {
        throw "ZIP must contain exactly one top-level RuntimeAssetImport directory. Found: $($topLevelDirectories -join ', ')"
    }

    foreach ($requiredEntry in $requiredEntries) {
        if ($entries -notcontains $requiredEntry) {
            throw "Required ZIP entry is missing: $requiredEntry"
        }
    }

    $expectedLicenseEntries = @(
        'RuntimeAssetImport/Source/ThirdParty/assimp/LICENSES/assimp/LICENSE',
        'RuntimeAssetImport/Source/ThirdParty/assimp/LICENSES/contrib/Open3DGC/LICENSE.txt',
        'RuntimeAssetImport/Source/ThirdParty/assimp/LICENSES/contrib/clipper/License.txt',
        'RuntimeAssetImport/Source/ThirdParty/assimp/LICENSES/contrib/earcut-hpp/LICENSE',
        'RuntimeAssetImport/Source/ThirdParty/assimp/LICENSES/contrib/openddlparser/LICENSE',
        'RuntimeAssetImport/Source/ThirdParty/assimp/LICENSES/contrib/poly2tri/LICENSE',
        'RuntimeAssetImport/Source/ThirdParty/assimp/LICENSES/contrib/pugixml/LICENSE.md',
        'RuntimeAssetImport/Source/ThirdParty/assimp/LICENSES/contrib/rapidjson/license.txt',
        'RuntimeAssetImport/Source/ThirdParty/assimp/LICENSES/contrib/stb/LICENSE.txt',
        'RuntimeAssetImport/Source/ThirdParty/assimp/LICENSES/contrib/unzip/MiniZip64_info.txt',
        'RuntimeAssetImport/Source/ThirdParty/assimp/LICENSES/contrib/utf8cpp/doc/LICENSE',
        'RuntimeAssetImport/Source/ThirdParty/assimp/LICENSES/contrib/zlib/LICENSE'
    ) | Sort-Object
    $actualLicenseEntries = @($entries | Where-Object {
            $_.StartsWith('RuntimeAssetImport/Source/ThirdParty/assimp/LICENSES/',
                [System.StringComparison]::Ordinal) -and -not $_.EndsWith('/')
        } | Sort-Object)
    if ([string]::Join("`n", $actualLicenseEntries) -cne [string]::Join("`n", $expectedLicenseEntries)) {
        throw "ZIP curated license allowlist mismatch. Expected: $($expectedLicenseEntries -join ', '). Actual: $($actualLicenseEntries -join ', ')."
    }

    $forbiddenPatterns = @(
        '^RuntimeAssetImport/Source/RuntimeAssetImportTest/',
        '^RuntimeAssetImport/BuildAssimpForPlugin\.ps1$',
        '^RuntimeAssetImport/MarketplaceListing\.md$',
        '^RuntimeAssetImport/FabSubmissionChecklist\.md$',
        '^RuntimeAssetImport/LICENSE-MIT-LEGACY$',
        '^RuntimeAssetImport/agent-rules',
        '^RuntimeAssetImport/AGENTS\.md$',
        '^RuntimeAssetImport/\.github/',
        '^RuntimeAssetImport/Binaries/',
        '^RuntimeAssetImport/Intermediate/',
        '^RuntimeAssetImport/Saved/',
        '^RuntimeAssetImport/Build/',
        '/Source/ThirdParty/assimp/LICENSES/contrib/draco/',
        '/Source/ThirdParty/assimp/LICENSES/contrib/googletest/',
        '/Source/ThirdParty/assimp/LICENSES/packaging/',
        '/Source/ThirdParty/assimp/LICENSES/port/',
        '/Source/ThirdParty/assimp/LICENSES/test/',
        '/Source/ThirdParty/assimp/LICENSES/contrib/zlib/contrib/dotzlib/',
        '\.pdb$',
        '(^|/)assimp\.exe$',
        '(^|/)PackedForFab/',
        'RuntimeAssetImport_.*_UE.*_Win64\.zip$'
    )
    foreach ($entry in $entries) {
        foreach ($pattern in $forbiddenPatterns) {
            if ($entry -match $pattern) {
                throw "Forbidden ZIP entry is present: $entry"
            }
        }
    }
}
finally {
    $archive.Dispose()
}

$validatedDescriptor = Get-Content -LiteralPath $stagedDescriptorPath -Raw | ConvertFrom-Json
if ($validatedDescriptor.EngineVersion -ne "$EngineVersion.0") {
    throw "Staged EngineVersion is invalid: $($validatedDescriptor.EngineVersion)"
}
if ($validatedDescriptor.Installed -ne $true) {
    throw 'Staged Installed must be true.'
}
if ($validatedDescriptor.Modules.Count -ne 1 -or
    $validatedDescriptor.Modules[0].Name -ne 'RuntimeAssetImport' -or
    $validatedDescriptor.Modules[0].Type -ne 'Runtime') {
    throw 'Staged descriptor validation failed: expected one RuntimeAssetImport Runtime module.'
}

$zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Output "Fab staging directory: $stagedPluginRoot"
Write-Output "Fab ZIP: $zipPath"
Write-Output "Fab ZIP SHA-256: $zipHash"
