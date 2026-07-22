[CmdletBinding()]
param(
    [string]$WorkDirectory,
    [switch]$KeepWorkDirectory
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$SourceRepository = 'https://github.com/assimp/assimp.git'
$SourceTag = 'v6.0.5'
$ExpectedCommit = '392a658f9c271be965271f45e7521a1b80ea4392'
$CMakeGenerator = 'Visual Studio 17 2022'
$Architecture = 'x64'
$Toolset = 'v143'
$Configuration = 'Release'
$AssimpDllName = 'assimp-vc143-mt.dll'
$AssimpLibName = 'assimp-vc143-mt.lib'

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

    $Result = [pscustomobject][ordered]@{
        ExitCode = $ExitCode
        StandardOutput = $StandardOutput
        StandardError = $StandardError
    }

    if ($ExitCode -ne 0)
    {
        $CombinedOutput = @($StandardError, $StandardOutput) -join "`n"
        $FirstError = ($CombinedOutput -split "`r?`n" | Where-Object { $_ -match '(?i)error|fatal' } | Select-Object -First 1)
        if ([string]::IsNullOrWhiteSpace($FirstError))
        {
            $FirstError = 'No explicit error line was emitted.'
        }
        throw "External command failed with exit code ${ExitCode}: $FilePath`nFirst error: $FirstError"
    }

    return $Result
}

function Get-RequiredCommandPath
{
    param([Parameter(Mandatory = $true)][string]$Name)

    $Command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue
    if ($null -eq $Command)
    {
        throw "Required command was not found on PATH: $Name"
    }
    return $Command.Source
}

function Normalize-GeneratedDaeMetadata
{
    param([Parameter(Mandatory = $true)][string]$Path)

    $Text = [System.IO.File]::ReadAllText($Path)
    $NormalizedText = [System.Text.RegularExpressions.Regex]::Replace(
        $Text, '<(created|modified)>[^<]+</\1>', '<$1>1970-01-01T00:00:00</$1>')
    if ($NormalizedText -eq $Text)
    {
        throw "Generated DAE did not contain the expected creation metadata: $Path"
    }
    [System.IO.File]::WriteAllText($Path, $NormalizedText, $Utf8NoBom)
}

function Normalize-GeneratedFbxMetadata
{
    param([Parameter(Mandatory = $true)][string]$Path)

    $Bytes = [System.IO.File]::ReadAllBytes($Path)
    $FixedTimestamp = [ordered]@{
        Year = 1970
        Month = 1
        Day = 1
        Hour = 0
        Minute = 0
        Second = 0
        Millisecond = 0
    }

    foreach ($Entry in $FixedTimestamp.GetEnumerator())
    {
        $NameBytes = [System.Text.Encoding]::ASCII.GetBytes($Entry.Key)
        $Matches = New-Object System.Collections.Generic.List[int]
        for ($Offset = 0; $Offset -le $Bytes.Length - $NameBytes.Length; $Offset++)
        {
            $MatchesName = $true
            for ($Index = 0; $Index -lt $NameBytes.Length; $Index++)
            {
                if ($Bytes[$Offset + $Index] -ne $NameBytes[$Index])
                {
                    $MatchesName = $false
                    break
                }
            }
            if ($MatchesName)
            {
                $Matches.Add($Offset)
            }
        }

        if ($Matches.Count -ne 1)
        {
            throw "Generated FBX must contain exactly one $($Entry.Key) header field: $Path"
        }

        $ValueTypeOffset = $Matches[0] + $NameBytes.Length
        if ($ValueTypeOffset + 4 -ge $Bytes.Length -or $Bytes[$ValueTypeOffset] -ne [byte][char]'I')
        {
            throw "Generated FBX $($Entry.Key) header field is not an int32 property: $Path"
        }

        $ValueBytes = [System.BitConverter]::GetBytes([int32]$Entry.Value)
        [System.Array]::Copy($ValueBytes, 0, $Bytes, $ValueTypeOffset + 1, $ValueBytes.Length)
    }

    [System.IO.File]::WriteAllBytes($Path, $Bytes)
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
    foreach ($Directory in [System.IO.Directory]::EnumerateDirectories(
            $FullPath, '*', [System.IO.SearchOption]::AllDirectories))
    {
        [System.IO.File]::SetAttributes($Directory, [System.IO.FileAttributes]::Directory)
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

    if (-not [System.IO.Directory]::Exists($Source))
    {
        throw "Required source directory does not exist: $Source"
    }

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

function Move-DirectoryWithRetry
{
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string]$AllowedRoot
    )

    $FullSource = [System.IO.Path]::GetFullPath($Source)
    $FullDestination = [System.IO.Path]::GetFullPath($Destination)
    $FullAllowedRoot = [System.IO.Path]::GetFullPath($AllowedRoot).TrimEnd('\') + '\'
    foreach ($Candidate in @($FullSource, $FullDestination))
    {
        if (-not $Candidate.StartsWith($FullAllowedRoot, [System.StringComparison]::OrdinalIgnoreCase))
        {
            throw "Refusing to move a directory outside the allowed root: $Candidate"
        }
    }
    if (-not [System.IO.Directory]::Exists($FullSource))
    {
        throw "Required move source directory does not exist: $FullSource"
    }
    if ([System.IO.Directory]::Exists($FullDestination))
    {
        throw "Move destination already exists: $FullDestination"
    }

    $MaximumAttempts = 5
    for ($Attempt = 1; $Attempt -le $MaximumAttempts; $Attempt++)
    {
        try
        {
            [System.IO.Directory]::Move($FullSource, $FullDestination)
            if ([System.IO.Directory]::Exists($FullSource) -or
                -not [System.IO.Directory]::Exists($FullDestination))
            {
                throw "Directory move did not reach the expected final state: $FullDestination"
            }
            return
        }
        catch [System.UnauthorizedAccessException]
        {
            if ($Attempt -eq $MaximumAttempts)
            {
                throw
            }
        }
        catch [System.IO.IOException]
        {
            if ($Attempt -eq $MaximumAttempts)
            {
                throw
            }
        }

        [System.GC]::Collect()
        [System.GC]::WaitForPendingFinalizers()
        [System.Threading.Thread]::Sleep(250)
    }
}

function Get-Sha256
{
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-RelativePath
{
    param(
        [Parameter(Mandatory = $true)][string]$BasePath,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $BaseUri = New-Object System.Uri(([System.IO.Path]::GetFullPath($BasePath).TrimEnd('\') + '\'))
    $PathUri = New-Object System.Uri([System.IO.Path]::GetFullPath($Path))
    return [System.Uri]::UnescapeDataString($BaseUri.MakeRelativeUri($PathUri).ToString()).Replace('/', '\')
}

function Get-HeadersManifestHash
{
    param([Parameter(Mandatory = $true)][string]$HeadersRoot)

    $HeaderFiles = [System.IO.Directory]::GetFiles($HeadersRoot, '*', [System.IO.SearchOption]::AllDirectories)
    [System.Array]::Sort($HeaderFiles, [System.StringComparer]::Ordinal)
    $Lines = New-Object System.Collections.Generic.List[string]
    foreach ($HeaderFile in $HeaderFiles)
    {
        $RelativePath = (Get-RelativePath -BasePath $HeadersRoot -Path $HeaderFile).Replace('\', '/')
        $Lines.Add(("{0}  {1}" -f (Get-Sha256 -Path $HeaderFile), $RelativePath))
    }
    $ManifestText = [string]::Join("`n", $Lines) + "`n"
    $Hasher = [System.Security.Cryptography.SHA256]::Create()
    try
    {
        $ManifestBytes = $Utf8NoBom.GetBytes($ManifestText)
        return ([System.BitConverter]::ToString($Hasher.ComputeHash($ManifestBytes))).Replace('-', '').ToLowerInvariant()
    }
    finally
    {
        $Hasher.Dispose()
    }
}

function Get-DependencyNames
{
    param([Parameter(Mandatory = $true)][string]$DumpbinOutput)

    $Dependencies = New-Object System.Collections.Generic.List[string]
    $InDependencySection = $false
    foreach ($Line in ($DumpbinOutput -split "`r?`n"))
    {
        if ($Line -match 'Image has the following dependencies:')
        {
            $InDependencySection = $true
            continue
        }
        if (-not $InDependencySection)
        {
            continue
        }
        if ($Line -match '^\s+([A-Za-z0-9_.-]+\.dll)\s*$')
        {
            $Dependencies.Add($Matches[1])
            continue
        }
        if ($Dependencies.Count -gt 0 -and [string]::IsNullOrWhiteSpace($Line))
        {
            break
        }
    }
    return $Dependencies.ToArray()
}

function Assert-RequiredFile
{
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not [System.IO.File]::Exists($Path))
    {
        throw "Required file does not exist: $Path"
    }
}

function Get-SortedUniqueStrings
{
    param([Parameter(Mandatory = $true)][string[]]$Values)

    $SortedValues = [string[]]@($Values)
    [System.Array]::Sort($SortedValues, [System.StringComparer]::Ordinal)
    $UniqueValues = New-Object System.Collections.Generic.List[string]
    foreach ($Value in $SortedValues)
    {
        if ($UniqueValues.Count -eq 0 -or $UniqueValues[$UniqueValues.Count - 1] -cne $Value)
        {
            $UniqueValues.Add($Value)
        }
    }
    return $UniqueValues.ToArray()
}

function Assert-ExactStringSet
{
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string[]]$Actual,
        [Parameter(Mandatory = $true)][string[]]$Expected
    )

    $ActualSorted = @(Get-SortedUniqueStrings -Values $Actual)
    $ExpectedSorted = @(Get-SortedUniqueStrings -Values $Expected)
    if ([string]::Join("`n", $ActualSorted) -cne [string]::Join("`n", $ExpectedSorted))
    {
        throw "$Label did not match the required set. Expected: $($ExpectedSorted -join ', '). Actual: $($ActualSorted -join ', ')."
    }
}

function Get-EnabledFormats
{
    param(
        [Parameter(Mandatory = $true)][string]$ConfigureOutput,
        [Parameter(Mandatory = $true)][ValidateSet('importer', 'exporter')][string]$FormatType
    )

    $Pattern = "(?im)^\s*--\s*Enabled $FormatType formats:\s*(.*?)\s*$"
    $Match = [System.Text.RegularExpressions.Regex]::Match($ConfigureOutput, $Pattern)
    if (-not $Match.Success)
    {
        throw "CMake output did not contain the Enabled $FormatType formats line."
    }
    return @($Match.Groups[1].Value -split '\s+' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        ForEach-Object { $_.ToUpperInvariant() })
}

function Get-AssimpExportIds
{
    param([Parameter(Mandatory = $true)][string]$ListExportOutput)

    $ExportIds = New-Object System.Collections.Generic.List[string]
    foreach ($Line in ($ListExportOutput -split "`r?`n"))
    {
        if ($Line -match '^\s*([a-z][a-z0-9]*)\b')
        {
            $ExportIds.Add($Matches[1])
        }
    }
    return @(Get-SortedUniqueStrings -Values $ExportIds.ToArray())
}

function Write-ExtractedLicense
{
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    $NormalizedText = [System.Text.RegularExpressions.Regex]::Replace($Text, "`r`n|`r", "`n")
    if (-not $NormalizedText.EndsWith("`n", [System.StringComparison]::Ordinal))
    {
        $NormalizedText += "`n"
    }
    [void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($Destination))
    [System.IO.File]::WriteAllText($Destination, $NormalizedText, $Utf8NoBom)
}

function Disable-AssimpDumpCommandInGeneratedProject
{
    param(
        [Parameter(Mandatory = $true)][string]$SourceDirectory,
        [Parameter(Mandatory = $true)][string]$BuildDirectory
    )

    $ProjectPath = Join-Path $BuildDirectory 'tools\assimp_cmd\assimp_cmd.vcxproj'
    Assert-RequiredFile -Path $ProjectPath
    $WriteDumpSource = Join-Path $SourceDirectory 'tools\assimp_cmd\WriteDump.cpp'
    $ProjectText = [System.IO.File]::ReadAllText($ProjectPath)
    $Needle = "    <ClCompile Include=`"$WriteDumpSource`" />"
    if ($ProjectText.IndexOf($Needle, [System.StringComparison]::Ordinal) -lt 0 -or
        $ProjectText.IndexOf($Needle, [System.StringComparison]::Ordinal) -ne
        $ProjectText.LastIndexOf($Needle, [System.StringComparison]::Ordinal))
    {
        throw 'Generated assimp_cmd project did not contain exactly one expected WriteDump.cpp item.'
    }
    $Replacement = @(
        "    <ClCompile Include=`"$WriteDumpSource`">",
        '      <PreprocessorDefinitions>ASSIMP_BUILD_NO_EXPORT;%(PreprocessorDefinitions)</PreprocessorDefinitions>',
        '    </ClCompile>'
    ) -join "`r`n"
    $UpdatedProjectText = $ProjectText.Replace($Needle, $Replacement)
    [System.IO.File]::WriteAllText($ProjectPath, $UpdatedProjectText, $Utf8NoBom)
}

try
{
    $GitPath = Get-RequiredCommandPath -Name 'git.exe'
    $CMakePath = Get-RequiredCommandPath -Name 'cmake.exe'

    $CMakeVersionResult = Invoke-NativeCommand -FilePath $CMakePath -Arguments @('--version')
    if ($CMakeVersionResult.StandardOutput -notmatch 'cmake version\s+(\d+\.\d+\.\d+)')
    {
        throw 'Could not parse the installed CMake version.'
    }
    $CMakeVersion = $Matches[1]
    if ([version]$CMakeVersion -lt [version]'3.22.0')
    {
        throw "CMake 3.22 or newer is required; found $CMakeVersion."
    }

    $VsWherePath = Join-Path ([System.Environment]::GetFolderPath('ProgramFilesX86')) `
        'Microsoft Visual Studio\Installer\vswhere.exe'
    Assert-RequiredFile -Path $VsWherePath
    $VsWhereArguments = @(
        '-latest', '-products', '*', '-version', '[17.0,18.0)',
        '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
    )
    $VsWhereResult = Invoke-NativeCommand -FilePath $VsWherePath -Arguments $VsWhereArguments
    $VisualStudioPath = $VsWhereResult.StandardOutput.Trim()
    if ([string]::IsNullOrWhiteSpace($VisualStudioPath) -or -not [System.IO.Directory]::Exists($VisualStudioPath))
    {
        throw 'Visual Studio 2022 with the Microsoft.VisualStudio.Component.VC.Tools.x86.x64 component was not found.'
    }

    $MsvcRoot = Join-Path $VisualStudioPath 'VC\Tools\MSVC'
    if (-not [System.IO.Directory]::Exists($MsvcRoot))
    {
        throw "The Visual Studio v143 tools directory does not exist: $MsvcRoot"
    }
    $MsvcDirectories = [System.IO.Directory]::GetDirectories($MsvcRoot)
    [System.Array]::Sort($MsvcDirectories, [System.StringComparer]::OrdinalIgnoreCase)
    if ($MsvcDirectories.Length -eq 0)
    {
        throw "No MSVC toolset was found under: $MsvcRoot"
    }
    $MsvcDirectory = $MsvcDirectories[$MsvcDirectories.Length - 1]
    $DumpbinPath = Join-Path $MsvcDirectory 'bin\Hostx64\x64\dumpbin.exe'
    $CompilerPath = Join-Path $MsvcDirectory 'bin\Hostx64\x64\cl.exe'
    Assert-RequiredFile -Path $DumpbinPath
    Assert-RequiredFile -Path $CompilerPath

    if ($CreatedDefaultWorkDirectory)
    {
        $WorkRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
            ("RuntimeAssetImport-Assimp-v6.0.5-{0}" -f [System.Guid]::NewGuid().ToString('N'))
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

    Write-Host "Assimp work directory: $WorkRoot" -ForegroundColor Green

    $SourceDirectory = Join-Path $WorkRoot 'source'
    $BuildDirectory = Join-Path $WorkRoot 'build'
    $InstallDirectory = Join-Path $WorkRoot 'install'
    $PayloadDirectory = Join-Path $WorkRoot 'payload'
    $GeneratedAssetDirectory = Join-Path $WorkRoot 'generated-assets'

    $CloneArguments = @('clone', '--branch', $SourceTag, '--depth', '1', $SourceRepository, $SourceDirectory)
    [void](Invoke-NativeCommand -FilePath $GitPath -Arguments $CloneArguments -WorkingDirectory $WorkRoot)

    $HeadResult = Invoke-NativeCommand -FilePath $GitPath -Arguments @('-C', $SourceDirectory, 'rev-parse', 'HEAD')
    $SourceCommit = $HeadResult.StandardOutput.Trim()
    if ($SourceCommit -cne $ExpectedCommit)
    {
        throw "Unexpected Assimp source commit. Expected $ExpectedCommit but found $SourceCommit."
    }
    $StatusResult = Invoke-NativeCommand -FilePath $GitPath -Arguments @('-C', $SourceDirectory, 'status', '--short')
    if (-not [string]::IsNullOrWhiteSpace($StatusResult.StandardOutput))
    {
        throw 'The cloned Assimp source tree is not clean.'
    }

    $CMakeOptionRecords = @(
        [ordered]@{ Name = 'BUILD_SHARED_LIBS'; Value = 'ON' },
        [ordered]@{ Name = 'ASSIMP_BUILD_ASSIMP_TOOLS'; Value = 'ON' },
        [ordered]@{ Name = 'ASSIMP_BUILD_ASSIMP_VIEW'; Value = 'OFF' },
        [ordered]@{ Name = 'ASSIMP_BUILD_TESTS'; Value = 'OFF' },
        [ordered]@{ Name = 'ASSIMP_BUILD_SAMPLES'; Value = 'OFF' },
        [ordered]@{ Name = 'ASSIMP_BUILD_DOCS'; Value = 'OFF' },
        [ordered]@{ Name = 'ASSIMP_BUILD_ZLIB'; Value = 'ON' },
        [ordered]@{ Name = 'ASSIMP_INSTALL'; Value = 'ON' },
        [ordered]@{ Name = 'ASSIMP_INSTALL_PDB'; Value = 'OFF' },
        [ordered]@{ Name = 'ASSIMP_WARNINGS_AS_ERRORS'; Value = 'ON' },
        [ordered]@{ Name = 'ASSIMP_HUNTER_ENABLED'; Value = 'OFF' },
        [ordered]@{ Name = 'ASSIMP_NO_EXPORT'; Value = 'OFF' },
        [ordered]@{ Name = 'ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT'; Value = 'OFF' },
        [ordered]@{ Name = 'ASSIMP_BUILD_COLLADA_IMPORTER'; Value = 'ON' },
        [ordered]@{ Name = 'ASSIMP_BUILD_FBX_IMPORTER'; Value = 'ON' },
        [ordered]@{ Name = 'ASSIMP_BUILD_OBJ_IMPORTER'; Value = 'ON' },
        [ordered]@{ Name = 'ASSIMP_BUILD_GLTF_IMPORTER'; Value = 'ON' },
        [ordered]@{ Name = 'ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT'; Value = 'OFF' },
        [ordered]@{ Name = 'ASSIMP_BUILD_COLLADA_EXPORTER'; Value = 'ON' },
        [ordered]@{ Name = 'ASSIMP_BUILD_FBX_EXPORTER'; Value = 'ON' },
        [ordered]@{ Name = 'ASSIMP_BUILD_GLTF_EXPORTER'; Value = 'ON' },
        [ordered]@{ Name = 'ASSIMP_BUILD_VRML_IMPORTER'; Value = 'OFF' },
        [ordered]@{ Name = 'ASSIMP_BUILD_3MF_IMPORTER'; Value = 'OFF' },
        [ordered]@{ Name = 'ASSIMP_BUILD_3MF_EXPORTER'; Value = 'OFF' },
        [ordered]@{ Name = 'ASSIMP_BUILD_DRACO'; Value = 'OFF' },
        [ordered]@{ Name = 'ASSIMP_BUILD_USD_IMPORTER'; Value = 'OFF' },
        [ordered]@{ Name = 'ASSIMP_BUILD_NONFREE_C4D_IMPORTER'; Value = 'OFF' },
        [ordered]@{ Name = 'USE_STATIC_CRT'; Value = 'OFF' },
        [ordered]@{ Name = 'ASSIMP_IGNORE_GIT_HASH'; Value = 'OFF' },
        [ordered]@{ Name = 'CMAKE_C_FLAGS'; Value = '/DWIN32 /D_WINDOWS /W3 /experimental:deterministic /pathmap:$WORK_DIRECTORY/source=C:\assimp-source' },
        [ordered]@{ Name = 'CMAKE_CXX_FLAGS'; Value = '/DWIN32 /D_WINDOWS /W3 /GR /EHsc /experimental:deterministic /pathmap:$WORK_DIRECTORY/source=C:\assimp-source' },
        [ordered]@{ Name = 'CMAKE_SHARED_LINKER_FLAGS'; Value = '/Brepro' },
        [ordered]@{ Name = 'CMAKE_EXE_LINKER_FLAGS'; Value = '/Brepro' },
        [ordered]@{ Name = 'CMAKE_INSTALL_PREFIX'; Value = '$WORK_DIRECTORY/install' }
    )

    $ConfigureArguments = New-Object System.Collections.Generic.List[string]
    $ConfigureArguments.Add('-S')
    $ConfigureArguments.Add($SourceDirectory)
    $ConfigureArguments.Add('-B')
    $ConfigureArguments.Add($BuildDirectory)
    $ConfigureArguments.Add('-G')
    $ConfigureArguments.Add($CMakeGenerator)
    $ConfigureArguments.Add('-A')
    $ConfigureArguments.Add($Architecture)
    $ConfigureArguments.Add('-T')
    $ConfigureArguments.Add($Toolset)
    foreach ($OptionRecord in $CMakeOptionRecords)
    {
        $OptionValue = $OptionRecord.Value
        if ($OptionRecord.Name -eq 'CMAKE_INSTALL_PREFIX')
        {
            $OptionValue = $InstallDirectory
        }
        elseif ($OptionRecord.Name -in @('CMAKE_C_FLAGS', 'CMAKE_CXX_FLAGS'))
        {
            $OptionValue = $OptionValue.Replace('$WORK_DIRECTORY/source', $SourceDirectory)
        }
        $ConfigureArguments.Add(("-D{0}={1}" -f $OptionRecord.Name, $OptionValue))
    }

    $ConfigureResult = Invoke-NativeCommand -FilePath $CMakePath -Arguments $ConfigureArguments.ToArray()
    $ConfigureOutput = $ConfigureResult.StandardOutput + "`n" + $ConfigureResult.StandardError
    $ParsedEnabledImporters = @(Get-EnabledFormats -ConfigureOutput $ConfigureOutput -FormatType 'importer')
    $ParsedEnabledExporters = @(Get-EnabledFormats -ConfigureOutput $ConfigureOutput -FormatType 'exporter')
    $EnabledImporters = @('COLLADA', 'FBX', 'GLTF', 'OBJ')
    $EnabledExporters = @('COLLADA', 'FBX', 'GLTF')
    Assert-ExactStringSet -Label 'Enabled importer formats' -Actual $ParsedEnabledImporters `
        -Expected $EnabledImporters
    Assert-ExactStringSet -Label 'Enabled exporter formats' -Actual $ParsedEnabledExporters `
        -Expected $EnabledExporters

    $DependencySourceRoot = Join-Path $BuildDirectory '_deps'
    if ([System.IO.Directory]::Exists($DependencySourceRoot))
    {
        $FetchedSourceDirectories = [System.IO.Directory]::GetDirectories(
            $DependencySourceRoot, '*-src', [System.IO.SearchOption]::TopDirectoryOnly)
        if ($FetchedSourceDirectories.Length -gt 0)
        {
            throw "CMake fetched external source repositories: $($FetchedSourceDirectories -join ', ')"
        }
    }
    foreach ($ForbiddenFetchDirectory in @(
            (Join-Path $BuildDirectory '_deps\meshlab_repo-src'),
            (Join-Path $BuildDirectory '_deps\tinyusdz_repo-src')))
    {
        if ([System.IO.Directory]::Exists($ForbiddenFetchDirectory))
        {
            throw "Forbidden external repository was fetched: $ForbiddenFetchDirectory"
        }
    }

    Disable-AssimpDumpCommandInGeneratedProject -SourceDirectory $SourceDirectory -BuildDirectory $BuildDirectory

    $CompilerConfigurationFiles = [System.IO.Directory]::GetFiles(
        $BuildDirectory, 'CMakeCXXCompiler.cmake', [System.IO.SearchOption]::AllDirectories)
    if ($CompilerConfigurationFiles.Length -ne 1)
    {
        throw "Expected one CMakeCXXCompiler.cmake file, found $($CompilerConfigurationFiles.Length)."
    }
    $CompilerConfiguration = [System.IO.File]::ReadAllText($CompilerConfigurationFiles[0])
    if ($CompilerConfiguration -notmatch 'set\(CMAKE_CXX_COMPILER_VERSION\s+"([^"]+)"\)')
    {
        throw 'Could not determine the MSVC compiler version from CMake configuration.'
    }
    $MsvcCompilerVersion = $Matches[1]

    $BuildArguments = @(
        '--build', $BuildDirectory, '--config', $Configuration,
        '--target', 'assimp', 'assimp_cmd', '--parallel'
    )
    [void](Invoke-NativeCommand -FilePath $CMakePath -Arguments $BuildArguments)
    [void](Invoke-NativeCommand -FilePath $CMakePath -Arguments @(
            '--install', $BuildDirectory, '--config', $Configuration))

    $PostBuildStatusResult = Invoke-NativeCommand -FilePath $GitPath `
        -Arguments @('-C', $SourceDirectory, 'status', '--short')
    if (-not [string]::IsNullOrWhiteSpace($PostBuildStatusResult.StandardOutput))
    {
        throw 'The Assimp source tree was modified during the build.'
    }

    $InstalledDll = Join-Path $InstallDirectory "bin\$AssimpDllName"
    $InstalledLib = Join-Path $InstallDirectory "lib\$AssimpLibName"
    $AssimpExe = Join-Path $InstallDirectory 'bin\assimp.exe'
    $InstalledHeaders = Join-Path $InstallDirectory 'include\assimp'
    $InstalledConfigHeader = Join-Path $InstalledHeaders 'config.h'
    $GeneratedRevisionHeader = Join-Path $BuildDirectory 'include\assimp\revision.h'
    $SourceLicense = Join-Path $SourceDirectory 'LICENSE'

    foreach ($RequiredFile in @(
            $InstalledDll, $InstalledLib, $AssimpExe, $InstalledConfigHeader,
            $GeneratedRevisionHeader, $SourceLicense))
    {
        Assert-RequiredFile -Path $RequiredFile
    }

    $RevisionText = [System.IO.File]::ReadAllText($GeneratedRevisionHeader)
    foreach ($ExpectedRevisionPattern in @(
            '#define\s+VER_MAJOR\s+6\b',
            '#define\s+VER_MINOR\s+0\b',
            '#define\s+VER_PATCH\s+5\b',
            '#define\s+GitVersion\s+0x392a658f\b'))
    {
        if ($RevisionText -notmatch $ExpectedRevisionPattern)
        {
            throw "Generated revision.h failed version validation: $ExpectedRevisionPattern"
        }
    }

    $VersionResult = Invoke-NativeCommand -FilePath $AssimpExe -Arguments @('version') `
        -WorkingDirectory (Split-Path -Parent $AssimpExe)
    $VersionOutput = $VersionResult.StandardOutput + $VersionResult.StandardError
    foreach ($ExpectedVersionPattern in @('Version\s+6\.0', '-shared', '392a658f'))
    {
        if ($VersionOutput -notmatch $ExpectedVersionPattern)
        {
            throw "assimp.exe version output is missing required value: $ExpectedVersionPattern"
        }
    }

    $ListExportResult = Invoke-NativeCommand -FilePath $AssimpExe -Arguments @('listexport') `
        -WorkingDirectory (Split-Path -Parent $AssimpExe)
    $ExportIds = @(Get-AssimpExportIds -ListExportOutput $ListExportResult.StandardOutput)
    Assert-ExactStringSet -Label 'assimp.exe listexport IDs' -Actual $ExportIds `
        -Expected @('collada', 'fbx', 'fbxa', 'gltf', 'glb', 'gltf2', 'glb2')

    $ListExtensionResult = Invoke-NativeCommand -FilePath $AssimpExe -Arguments @('listext') `
        -WorkingDirectory (Split-Path -Parent $AssimpExe)
    foreach ($Extension in @('fbx', 'obj', 'gltf', 'glb', 'dae'))
    {
        if ($ListExtensionResult.StandardOutput -notmatch ("(?i)\*\.{0}\b" -f [regex]::Escape($Extension)))
        {
            throw "assimp.exe listext does not contain required extension: *.$Extension"
        }
    }

    foreach ($PeFile in @($InstalledDll, $InstalledLib, $AssimpExe))
    {
        $HeaderResult = Invoke-NativeCommand -FilePath $DumpbinPath -Arguments @('/headers', $PeFile)
        if ($HeaderResult.StandardOutput -notmatch '(?i)8664 machine \(x64\)')
        {
            throw "PE architecture is not x64: $PeFile"
        }
    }

    $DependentsResult = Invoke-NativeCommand -FilePath $DumpbinPath -Arguments @('/dependents', $InstalledDll)
    $DependencyNames = Get-DependencyNames -DumpbinOutput $DependentsResult.StandardOutput
    if ($DependencyNames.Length -eq 0)
    {
        throw 'dumpbin did not report any DLL dependencies for the Assimp DLL.'
    }
    foreach ($DependencyName in $DependencyNames)
    {
        if ($DependencyName -match '^(?i:zlib|minizip|draco).*\.dll$')
        {
            throw "Assimp DLL unexpectedly depends on an external third-party DLL: $DependencyName"
        }
    }

    [void][System.IO.Directory]::CreateDirectory($GeneratedAssetDirectory)
    $SourceObj = Join-Path $PSScriptRoot 'Source\RuntimeAssetImportTest\TestAssets\test_triangle.obj'
    $SourceMtl = Join-Path $PSScriptRoot 'Source\RuntimeAssetImportTest\TestAssets\test_triangle.mtl'
    Assert-RequiredFile -Path $SourceObj
    Assert-RequiredFile -Path $SourceMtl
    $TempObj = Join-Path $GeneratedAssetDirectory 'test_triangle.obj'
    $TempMtl = Join-Path $GeneratedAssetDirectory 'test_triangle.mtl'
    [System.IO.File]::Copy($SourceObj, $TempObj, $true)
    [System.IO.File]::Copy($SourceMtl, $TempMtl, $true)

    $GeneratedAssetSpecifications = @(
        [ordered]@{ FileName = 'test_triangle.fbx'; FormatId = 'fbx' },
        [ordered]@{ FileName = 'test_triangle.dae'; FormatId = 'collada' },
        [ordered]@{ FileName = 'test_triangle.glb'; FormatId = 'glb2' }
    )
    $GeneratedAssetRecords = New-Object System.Collections.Generic.List[object]
    $GeneratedCommandRecords = New-Object System.Collections.Generic.List[string]
    foreach ($GeneratedAssetSpecification in $GeneratedAssetSpecifications)
    {
        $OutputAsset = Join-Path $GeneratedAssetDirectory $GeneratedAssetSpecification.FileName
        $ExportArguments = @('export', $TempObj, $OutputAsset, ("-f{0}" -f $GeneratedAssetSpecification.FormatId))
        [void](Invoke-NativeCommand -FilePath $AssimpExe -Arguments $ExportArguments `
                -WorkingDirectory $GeneratedAssetDirectory)
        Assert-RequiredFile -Path $OutputAsset
        if ($GeneratedAssetSpecification.FormatId -eq 'fbx')
        {
            Normalize-GeneratedFbxMetadata -Path $OutputAsset
        }
        elseif ($GeneratedAssetSpecification.FormatId -eq 'collada')
        {
            Normalize-GeneratedDaeMetadata -Path $OutputAsset
        }
        $OutputAssetInfo = New-Object System.IO.FileInfo($OutputAsset)
        if ($OutputAssetInfo.Length -le 0)
        {
            throw "Generated test asset is empty: $OutputAsset"
        }
        [void](Invoke-NativeCommand -FilePath $AssimpExe -Arguments @('info', $OutputAsset) `
                -WorkingDirectory $GeneratedAssetDirectory)

        $GeneratedCommandRecords.Add(("assimp.exe export test_triangle.obj {0} -f{1}" -f `
                    $GeneratedAssetSpecification.FileName, $GeneratedAssetSpecification.FormatId))
        $GeneratedAssetRecords.Add([ordered]@{
                FileName = $GeneratedAssetSpecification.FileName
                Size = $OutputAssetInfo.Length
                Sha256 = Get-Sha256 -Path $OutputAsset
            })
    }

    $PayloadAssimpRoot = Join-Path $PayloadDirectory 'assimp'
    $PayloadBin = Join-Path $PayloadAssimpRoot 'Bin\Win64'
    $PayloadLib = Join-Path $PayloadAssimpRoot 'Lib\Win64'
    $PayloadHeaders = Join-Path $PayloadAssimpRoot 'Include\assimp'
    $PayloadLicenses = Join-Path $PayloadAssimpRoot 'LICENSES'
    foreach ($PayloadPath in @($PayloadBin, $PayloadLib, $PayloadHeaders, $PayloadLicenses))
    {
        [void][System.IO.Directory]::CreateDirectory($PayloadPath)
    }

    [System.IO.File]::Copy($InstalledDll, (Join-Path $PayloadBin $AssimpDllName), $true)
    [System.IO.File]::Copy($InstalledLib, (Join-Path $PayloadLib $AssimpLibName), $true)
    Copy-Directory -Source $InstalledHeaders -Destination $PayloadHeaders
    $NormalizedRevisionText = [System.Text.RegularExpressions.Regex]::Replace(
        [System.IO.File]::ReadAllText($GeneratedRevisionHeader), '(?m)[ \t]+$', '')
    $NormalizedRevisionText = [System.Text.RegularExpressions.Regex]::Replace(
        $NormalizedRevisionText, "`r`n|`r", "`n")
    [System.IO.File]::WriteAllText(
        (Join-Path $PayloadHeaders 'revision.h'), $NormalizedRevisionText, $Utf8NoBom)
    [System.IO.File]::Copy($SourceLicense, (Join-Path $PayloadAssimpRoot 'LICENSE'), $true)

    $LicenseCopyRecords = @(
        [ordered]@{ Source = 'LICENSE'; Destination = 'assimp/LICENSE' },
        [ordered]@{ Source = 'contrib/clipper/License.txt'; Destination = 'contrib/clipper/License.txt' },
        [ordered]@{ Source = 'contrib/earcut-hpp/LICENSE'; Destination = 'contrib/earcut-hpp/LICENSE' },
        [ordered]@{ Source = 'contrib/openddlparser/LICENSE'; Destination = 'contrib/openddlparser/LICENSE' },
        [ordered]@{ Source = 'contrib/poly2tri/LICENSE'; Destination = 'contrib/poly2tri/LICENSE' },
        [ordered]@{ Source = 'contrib/pugixml/LICENSE.md'; Destination = 'contrib/pugixml/LICENSE.md' },
        [ordered]@{ Source = 'contrib/rapidjson/license.txt'; Destination = 'contrib/rapidjson/license.txt' },
        [ordered]@{ Source = 'contrib/unzip/MiniZip64_info.txt'; Destination = 'contrib/unzip/MiniZip64_info.txt' },
        [ordered]@{ Source = 'contrib/utf8cpp/doc/LICENSE'; Destination = 'contrib/utf8cpp/doc/LICENSE' },
        [ordered]@{ Source = 'contrib/zlib/LICENSE'; Destination = 'contrib/zlib/LICENSE' }
    )
    foreach ($LicenseCopyRecord in $LicenseCopyRecords)
    {
        $LicenseSource = Join-Path $SourceDirectory $LicenseCopyRecord.Source
        $LicenseDestination = Join-Path $PayloadLicenses $LicenseCopyRecord.Destination
        Assert-RequiredFile -Path $LicenseSource
        [void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($LicenseDestination))
        [System.IO.File]::Copy($LicenseSource, $LicenseDestination, $true)
    }

    $Open3DgcSource = Join-Path $SourceDirectory 'contrib/Open3DGC/o3dgcCommon.h'
    Assert-RequiredFile -Path $Open3DgcSource
    $Open3DgcSourceText = [System.IO.File]::ReadAllText($Open3DgcSource)
    $Open3DgcStart = $Open3DgcSourceText.IndexOf('/*', [System.StringComparison]::Ordinal)
    if ($Open3DgcStart -lt 0 -or -not [string]::IsNullOrWhiteSpace($Open3DgcSourceText.Substring(0, $Open3DgcStart)))
    {
        throw 'Open3DGC source does not begin with the expected license comment.'
    }
    $Open3DgcEnd = $Open3DgcSourceText.IndexOf('*/', $Open3DgcStart, [System.StringComparison]::Ordinal)
    if ($Open3DgcEnd -lt 0)
    {
        throw 'Open3DGC source license comment is not terminated.'
    }
    $Open3DgcLicenseText = $Open3DgcSourceText.Substring($Open3DgcStart, $Open3DgcEnd - $Open3DgcStart + 2)
    foreach ($RequiredText in @('Copyright (c) 2013 Khaled Mammou', 'Permission is hereby granted'))
    {
        if (-not $Open3DgcLicenseText.Contains($RequiredText))
        {
            throw "Open3DGC license extraction is missing required text: $RequiredText"
        }
    }
    Write-ExtractedLicense -Text $Open3DgcLicenseText `
        -Destination (Join-Path $PayloadLicenses 'contrib/Open3DGC/LICENSE.txt')

    $StbSource = Join-Path $SourceDirectory 'contrib/stb/stb_image.h'
    Assert-RequiredFile -Path $StbSource
    $StbSourceText = [System.IO.File]::ReadAllText($StbSource)
    $StbLicenseMarker = 'This software is available under 2 licenses'
    $StbStart = $StbSourceText.IndexOf($StbLicenseMarker, [System.StringComparison]::Ordinal)
    if ($StbStart -lt 0)
    {
        throw 'stb source does not contain the dual-license marker.'
    }
    $StbEnd = $StbSourceText.IndexOf('*/', $StbStart, [System.StringComparison]::Ordinal)
    if ($StbEnd -lt 0)
    {
        throw 'stb dual-license block is not terminated.'
    }
    $StbLicenseText = $StbSourceText.Substring($StbStart, $StbEnd - $StbStart + 2)
    foreach ($RequiredText in @(
            'ALTERNATIVE A - MIT License',
            'Copyright (c) 2017 Sean Barrett',
            'ALTERNATIVE B - Public Domain'))
    {
        if (-not $StbLicenseText.Contains($RequiredText))
        {
            throw "stb license extraction is missing required text: $RequiredText"
        }
    }
    Write-ExtractedLicense -Text $StbLicenseText `
        -Destination (Join-Path $PayloadLicenses 'contrib/stb/LICENSE.txt')

    $ExpectedThirdPartyLicenseFiles = @(
        'LICENSES/assimp/LICENSE',
        'LICENSES/contrib/Open3DGC/LICENSE.txt',
        'LICENSES/contrib/clipper/License.txt',
        'LICENSES/contrib/earcut-hpp/LICENSE',
        'LICENSES/contrib/openddlparser/LICENSE',
        'LICENSES/contrib/poly2tri/LICENSE',
        'LICENSES/contrib/pugixml/LICENSE.md',
        'LICENSES/contrib/rapidjson/license.txt',
        'LICENSES/contrib/stb/LICENSE.txt',
        'LICENSES/contrib/unzip/MiniZip64_info.txt',
        'LICENSES/contrib/utf8cpp/doc/LICENSE',
        'LICENSES/contrib/zlib/LICENSE'
    )
    $ThirdPartyLicenseFiles = New-Object System.Collections.Generic.List[string]
    foreach ($LicenseFile in [System.IO.Directory]::GetFiles(
            $PayloadLicenses, '*', [System.IO.SearchOption]::AllDirectories))
    {
        $RelativeLicensePath = (Get-RelativePath -BasePath $PayloadAssimpRoot -Path $LicenseFile).Replace('\', '/')
        $ThirdPartyLicenseFiles.Add($RelativeLicensePath)
    }
    $ThirdPartyLicenseFileArray = @(Get-SortedUniqueStrings -Values $ThirdPartyLicenseFiles.ToArray())
    Assert-ExactStringSet -Label 'Curated third-party license files' -Actual $ThirdPartyLicenseFileArray `
        -Expected $ExpectedThirdPartyLicenseFiles

    $ThirdPartyNoticesPath = Join-Path $PSScriptRoot 'THIRD_PARTY_NOTICES.md'
    Assert-RequiredFile -Path $ThirdPartyNoticesPath
    $ThirdPartyNoticesText = [System.IO.File]::ReadAllText($ThirdPartyNoticesPath)
    $NoticeInventoryMatch = [System.Text.RegularExpressions.Regex]::Match(
        $ThirdPartyNoticesText,
        '(?s)<!-- BEGIN ASSIMP LICENSE FILES -->(.*?)<!-- END ASSIMP LICENSE FILES -->')
    if (-not $NoticeInventoryMatch.Success)
    {
        throw 'THIRD_PARTY_NOTICES.md does not contain the curated license inventory markers.'
    }
    $NoticeLicenseFiles = New-Object System.Collections.Generic.List[string]
    foreach ($NoticeLicenseMatch in [System.Text.RegularExpressions.Regex]::Matches(
            $NoticeInventoryMatch.Groups[1].Value,
            '`Source/ThirdParty/assimp/(LICENSES/[^`]+)`'))
    {
        $NoticeLicenseFiles.Add($NoticeLicenseMatch.Groups[1].Value)
    }
    Assert-ExactStringSet -Label 'THIRD_PARTY_NOTICES.md license files' -Actual $NoticeLicenseFiles.ToArray() `
        -Expected $ThirdPartyLicenseFileArray

    $PayloadDll = Join-Path $PayloadBin $AssimpDllName
    $PayloadLibFile = Join-Path $PayloadLib $AssimpLibName
    $DllInfo = New-Object System.IO.FileInfo($PayloadDll)
    $LibInfo = New-Object System.IO.FileInfo($PayloadLibFile)
    $AssimpExeInfo = New-Object System.IO.FileInfo($AssimpExe)
    $HeadersManifestSha256 = Get-HeadersManifestHash -HeadersRoot $PayloadHeaders

    $BuildInfo = [ordered]@{
        Dependency = 'Assimp'
        Version = '6.0.5'
        SourceRepository = $SourceRepository
        SourceTag = $SourceTag
        SourceCommit = $ExpectedCommit
        CMakeGenerator = $CMakeGenerator
        Architecture = $Architecture
        Toolset = $Toolset
        Configuration = $Configuration
        CMakeVersion = $CMakeVersion
        MsvcCompilerVersion = $MsvcCompilerVersion
        CMakeOptions = $CMakeOptionRecords
        EnabledImporters = $EnabledImporters
        EnabledExporters = $EnabledExporters
        ThirdPartyLicenseFiles = $ThirdPartyLicenseFileArray
        AssimpCommandBuildWorkaround =
            'The generated assimp_cmd project compiles only WriteDump.cpp with ASSIMP_BUILD_NO_EXPORT because Assimp 6.0.5 otherwise links disabled ASSBIN and ASSXML dump writers. The Assimp source tree is unchanged.'
        Dll = [ordered]@{
            FileName = $AssimpDllName
            Size = $DllInfo.Length
            Sha256 = Get-Sha256 -Path $PayloadDll
        }
        ImportLibrary = [ordered]@{
            FileName = $AssimpLibName
            Size = $LibInfo.Length
            Sha256 = Get-Sha256 -Path $PayloadLibFile
        }
        EphemeralAssimpExe = [ordered]@{
            FileName = 'assimp.exe'
            Size = $AssimpExeInfo.Length
            Sha256 = Get-Sha256 -Path $AssimpExe
        }
        HeadersManifestSha256 = $HeadersManifestSha256
        DllDependencies = @($DependencyNames)
        TestAssetGenerationCommands = $GeneratedCommandRecords.ToArray()
        GeneratedTestAssets = $GeneratedAssetRecords.ToArray()
    }
    $BuildInfoJson = $BuildInfo | ConvertTo-Json -Depth 10
    $BuildInfoJson = ($BuildInfoJson -replace "`r?`n", "`n") + "`n"
    [System.IO.File]::WriteAllText((Join-Path $PayloadAssimpRoot 'BUILD-INFO.json'), $BuildInfoJson, $Utf8NoBom)

    $GeneratedAssetsDocumentation = New-Object System.Collections.Generic.List[string]
    $GeneratedAssetsDocumentation.Add('# Generated Test Assets')
    $GeneratedAssetsDocumentation.Add('')
    $GeneratedAssetsDocumentation.Add('Source asset: `test_triangle.obj` with `test_triangle.mtl`.')
    $GeneratedAssetsDocumentation.Add('')
    $GeneratedAssetsDocumentation.Add('Generator: Assimp 6.0.5 built from official tag `v6.0.5` at commit')
    $GeneratedAssetsDocumentation.Add(('`{0}`.' -f $ExpectedCommit))
    $GeneratedAssetsDocumentation.Add('')
    $GeneratedAssetsDocumentation.Add(
        'For reproducible builds, exporter-created FBX and DAE timestamps are normalized to 1970-01-01 after export and before readback validation.')
    $GeneratedAssetsDocumentation.Add('')
    $GeneratedAssetsDocumentation.Add('## Commands')
    $GeneratedAssetsDocumentation.Add('')
    foreach ($GeneratedCommandRecord in $GeneratedCommandRecords)
    {
        $GeneratedAssetsDocumentation.Add(('- `{0}`' -f $GeneratedCommandRecord))
    }
    $GeneratedAssetsDocumentation.Add('')
    $GeneratedAssetsDocumentation.Add('## SHA-256')
    $GeneratedAssetsDocumentation.Add('')
    foreach ($GeneratedAssetRecord in $GeneratedAssetRecords)
    {
        $GeneratedAssetsDocumentation.Add(('- `{0}`: `{1}`' -f $GeneratedAssetRecord.FileName,
                $GeneratedAssetRecord.Sha256))
    }
    $GeneratedAssetsText = [string]::Join("`n", $GeneratedAssetsDocumentation) + "`n"
    [System.IO.File]::WriteAllText(
        (Join-Path $GeneratedAssetDirectory 'GENERATED-ASSETS.md'), $GeneratedAssetsText, $Utf8NoBom)

    foreach ($PayloadRequiredFile in @(
            $PayloadDll, $PayloadLibFile, (Join-Path $PayloadHeaders 'config.h'),
            (Join-Path $PayloadHeaders 'revision.h'), (Join-Path $PayloadAssimpRoot 'LICENSE'),
            (Join-Path $PayloadAssimpRoot 'BUILD-INFO.json')))
    {
        Assert-RequiredFile -Path $PayloadRequiredFile
    }

    $ThirdPartyRoot = Join-Path $PSScriptRoot 'Source\ThirdParty'
    $AuthoritativeAssimpRoot = Join-Path $ThirdPartyRoot 'assimp'
    $CutoverId = [System.Guid]::NewGuid().ToString('N')
    $ReplacementAssimpRoot = Join-Path $ThirdPartyRoot ".assimp-replacement-$CutoverId"
    $BackupAssimpRoot = Join-Path $ThirdPartyRoot ".assimp-backup-$CutoverId"
    $CutoverComplete = $false
    $AuthoritativeMoved = $false

    try
    {
        Copy-Directory -Source $AuthoritativeAssimpRoot -Destination $ReplacementAssimpRoot
        foreach ($ReplacementPath in @(
                (Join-Path $ReplacementAssimpRoot 'Bin'),
                (Join-Path $ReplacementAssimpRoot 'Lib'),
                (Join-Path $ReplacementAssimpRoot 'Include'),
                (Join-Path $ReplacementAssimpRoot 'LICENSES')))
        {
            if ([System.IO.Directory]::Exists($ReplacementPath))
            {
                Remove-DirectorySafely -Path $ReplacementPath -AllowedRoot $ReplacementAssimpRoot
            }
        }
        foreach ($ReplacementFile in @(
                (Join-Path $ReplacementAssimpRoot 'LICENSE'),
                (Join-Path $ReplacementAssimpRoot 'BUILD-INFO.json')))
        {
            if ([System.IO.File]::Exists($ReplacementFile))
            {
                [System.IO.File]::SetAttributes($ReplacementFile, [System.IO.FileAttributes]::Normal)
                [System.IO.File]::Delete($ReplacementFile)
            }
        }
        Copy-Directory -Source $PayloadAssimpRoot -Destination $ReplacementAssimpRoot

        Move-DirectoryWithRetry -Source $AuthoritativeAssimpRoot -Destination $BackupAssimpRoot `
            -AllowedRoot $ThirdPartyRoot
        $AuthoritativeMoved = $true
        Move-DirectoryWithRetry -Source $ReplacementAssimpRoot -Destination $AuthoritativeAssimpRoot `
            -AllowedRoot $ThirdPartyRoot

        $TestAssetsRoot = Join-Path $PSScriptRoot 'Source\RuntimeAssetImportTest\TestAssets'
        foreach ($GeneratedAssetRecord in $GeneratedAssetRecords)
        {
            [System.IO.File]::Copy(
                (Join-Path $GeneratedAssetDirectory $GeneratedAssetRecord.FileName),
                (Join-Path $TestAssetsRoot $GeneratedAssetRecord.FileName),
                $true)
        }
        [System.IO.File]::Copy(
            (Join-Path $GeneratedAssetDirectory 'GENERATED-ASSETS.md'),
            (Join-Path $TestAssetsRoot 'GENERATED-ASSETS.md'),
            $true)

        $CutoverComplete = $true
    }
    catch
    {
        if ($AuthoritativeMoved)
        {
            if ([System.IO.Directory]::Exists($AuthoritativeAssimpRoot))
            {
                Remove-DirectorySafely -Path $AuthoritativeAssimpRoot -AllowedRoot $ThirdPartyRoot
            }
            if ([System.IO.Directory]::Exists($BackupAssimpRoot))
            {
                Move-DirectoryWithRetry -Source $BackupAssimpRoot -Destination $AuthoritativeAssimpRoot `
                    -AllowedRoot $ThirdPartyRoot
            }
        }
        throw
    }
    finally
    {
        if ($CutoverComplete -and [System.IO.Directory]::Exists($BackupAssimpRoot))
        {
            Remove-DirectorySafely -Path $BackupAssimpRoot -AllowedRoot $ThirdPartyRoot
        }
        if ([System.IO.Directory]::Exists($ReplacementAssimpRoot))
        {
            Remove-DirectorySafely -Path $ReplacementAssimpRoot -AllowedRoot $ThirdPartyRoot
        }
    }

    Write-Host 'Assimp 6.0.5 artifacts and generated test assets were updated successfully.' -ForegroundColor Green
    Write-Host ("DLL SHA-256: {0}" -f $BuildInfo.Dll.Sha256)
    Write-Host ("LIB SHA-256: {0}" -f $BuildInfo.ImportLibrary.Sha256)
    Write-Host ("assimp.exe SHA-256: {0}" -f $BuildInfo.EphemeralAssimpExe.Sha256)
    Write-Host ("Headers manifest SHA-256: {0}" -f $HeadersManifestSha256)
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
        Write-Host "Removed Assimp work directory: $WorkRoot"
    }
    elseif (-not $CompletedSuccessfully -and $null -ne $WorkRoot -and
        [System.IO.Directory]::Exists($WorkRoot))
    {
        Write-Host "Assimp work directory retained after failure: $WorkRoot" -ForegroundColor Yellow
    }
    elseif ($CompletedSuccessfully -and $KeepWorkDirectory)
    {
        Write-Host "Assimp work directory retained: $WorkRoot" -ForegroundColor Yellow
    }
}
