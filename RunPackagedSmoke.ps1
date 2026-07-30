[CmdletBinding()]
param(
    [ValidatePattern('^5\.[0-9]+$')]
    [string]$EngineVersion = '5.8',

    [string]$WorkDirectory,

    [string]$ReleaseToolsRoot,

    [string]$EngineRoot,

    [string]$SampleRoot,

    [string]$PackageZipPath,

    [switch]$KeepWorkDirectory
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$CreatedDefaultWorkDirectory = [string]::IsNullOrWhiteSpace($WorkDirectory)
$WorkRoot = $null
$CompletedSuccessfully = $false
$ExpectedSampleHead = 'b4c78058993e68a62fc9c16b673aec65ee668573'
$ExpectedToolVersion = '0.2.0'

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

function Test-IsSameOrDescendantPath
{
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Candidate
    )

    $ResolvedRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $ResolvedCandidate = [System.IO.Path]::GetFullPath($Candidate).TrimEnd('\', '/')
    return $ResolvedCandidate.Equals($ResolvedRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
        $ResolvedCandidate.StartsWith($ResolvedRoot + [System.IO.Path]::DirectorySeparatorChar,
            [System.StringComparison]::OrdinalIgnoreCase)
}

function Invoke-GitValue
{
    param(
        [Parameter(Mandatory = $true)][string]$GitPath,
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $Result = Invoke-NativeCommand -FilePath $GitPath -Arguments (@('-C', $RepositoryRoot) + $Arguments) `
        -WorkingDirectory $RepositoryRoot
    return $Result.StandardOutput.Trim()
}

function Assert-SampleRepository
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string]$GitPath
    )

    if (-not [System.IO.Directory]::Exists($RepositoryRoot))
    {
        throw "RuntimeAssetImportSample repository was not found: $RepositoryRoot"
    }
    $ResolvedRoot = [System.IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\', '/')
    $GitRoot = Invoke-GitValue -GitPath $GitPath -RepositoryRoot $ResolvedRoot `
        -Arguments @('rev-parse', '--show-toplevel')
    $NormalizedGitRoot = [System.IO.Path]::GetFullPath($GitRoot).TrimEnd('\', '/')
    if (-not $NormalizedGitRoot.Equals($ResolvedRoot, [System.StringComparison]::OrdinalIgnoreCase))
    {
        throw "SampleRoot must be the exact Git repository root. Git reported '$GitRoot' for '$ResolvedRoot'."
    }
    $Status = Invoke-GitValue -GitPath $GitPath -RepositoryRoot $ResolvedRoot `
        -Arguments @('status', '--porcelain', '--untracked-files=all')
    if (-not [string]::IsNullOrWhiteSpace($Status))
    {
        throw "RuntimeAssetImportSample source repository must be clean: $ResolvedRoot"
    }
    $Branch = Invoke-GitValue -GitPath $GitPath -RepositoryRoot $ResolvedRoot `
        -Arguments @('branch', '--show-current')
    if ($Branch -cne 'main')
    {
        throw "RuntimeAssetImportSample source repository must be on branch main, found '$Branch'."
    }
    $Head = Invoke-GitValue -GitPath $GitPath -RepositoryRoot $ResolvedRoot `
        -Arguments @('rev-parse', 'HEAD')
    if ($Head -cne $ExpectedSampleHead)
    {
        throw "RuntimeAssetImportSample HEAD mismatch. Expected $ExpectedSampleHead but found $Head."
    }
    $OriginMain = Invoke-GitValue -GitPath $GitPath -RepositoryRoot $ResolvedRoot `
        -Arguments @('rev-parse', 'origin/main')
    if ($OriginMain -cne $ExpectedSampleHead)
    {
        throw "RuntimeAssetImportSample origin/main mismatch. Expected $ExpectedSampleHead but found $OriginMain."
    }
    return $ResolvedRoot
}

function Resolve-ReleaseArtifactSet
{
    param(
        [string]$RequestedZipPath,
        [Parameter(Mandatory = $true)][string]$OutputDirectory,
        [Parameter(Mandatory = $true)][string]$RequestedEngineVersion
    )

    $ZipPath = $null
    if (-not [string]::IsNullOrWhiteSpace($RequestedZipPath))
    {
        $ZipPath = [System.IO.Path]::GetFullPath($RequestedZipPath)
        Assert-RequiredFile -Path $ZipPath
    }
    else
    {
        Assert-RequiredDirectory -Path $OutputDirectory
        $ZipFiles = @([System.IO.Directory]::GetFiles($OutputDirectory, '*.zip',
                [System.IO.SearchOption]::TopDirectoryOnly))
        $ZipPattern = '^RuntimeAssetImport_.+_UE' + [regex]::Escape($RequestedEngineVersion) + '_Win64\.zip$'
        $MatchingZipFiles = @($ZipFiles | Where-Object {
                [System.Text.RegularExpressions.Regex]::IsMatch(
                    [System.IO.Path]::GetFileName($_), $ZipPattern)
            })
        if ($ZipFiles.Count -ne 1 -or $MatchingZipFiles.Count -ne 1)
        {
            throw "Expected exactly one RuntimeAssetImport UE$RequestedEngineVersion ZIP in $OutputDirectory."
        }
        $ZipPath = [System.IO.Path]::GetFullPath($MatchingZipFiles[0])
    }

    if (([System.IO.FileInfo]::new($ZipPath)).Length -le 0)
    {
        throw "Release ZIP is empty: $ZipPath"
    }
    $ZipName = [System.IO.Path]::GetFileName($ZipPath)
    $ZipPattern = '^RuntimeAssetImport_.+_UE' + [regex]::Escape($RequestedEngineVersion) + '_Win64\.zip$'
    if (-not [System.Text.RegularExpressions.Regex]::IsMatch($ZipName, $ZipPattern))
    {
        throw "Release ZIP filename does not match the requested engine version: $ZipName"
    }

    $ChecksumPath = "$ZipPath.sha256"
    $ReportPath = "$ZipPath.report.json"
    $LogPath = "$ZipPath.log"
    Assert-RequiredFile -Path $ChecksumPath
    Assert-RequiredFile -Path $ReportPath
    Assert-RequiredFile -Path $LogPath

    $ChecksumText = [System.IO.File]::ReadAllText($ChecksumPath).TrimEnd("`r", "`n")
    $ChecksumMatch = [System.Text.RegularExpressions.Regex]::Match(
        $ChecksumText, '^(?<Hash>[0-9a-f]{64})  (?<Name>[^`r`n]+)$')
    if (-not $ChecksumMatch.Success -or $ChecksumMatch.Groups['Name'].Value -cne $ZipName)
    {
        throw "Checksum sidecar must contain '<lowercase SHA-256><two spaces><ZIP filename>': $ChecksumPath"
    }
    $ActualHash = (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($ChecksumMatch.Groups['Hash'].Value -cne $ActualHash)
    {
        throw "Release ZIP checksum mismatch. Sidecar: $($ChecksumMatch.Groups['Hash'].Value); actual: $ActualHash"
    }

    $Report = Get-Content -Raw -LiteralPath $ReportPath | ConvertFrom-Json
    if ($Report.status -cne 'PASS') { throw "Release report status is not PASS: $ReportPath" }
    if ([string]$Report.toolVersion -cne $ExpectedToolVersion) {
        throw "Release report toolVersion mismatch: $($Report.toolVersion)"
    }
    if ([string]$Report.pluginName -cne 'RuntimeAssetImport') {
        throw "Release report pluginName mismatch: $($Report.pluginName)"
    }
    if ([string]$Report.engineVersion -cne $RequestedEngineVersion) {
        throw "Release report engineVersion mismatch: $($Report.engineVersion)"
    }
    if ([string]$Report.repositoryHead -cne $ProductHead) {
        throw "Release report repositoryHead mismatch. Expected $ProductHead but found $($Report.repositoryHead)."
    }
    if ([string]$Report.zipSha256 -cne $ActualHash) {
        throw "Release report zipSha256 mismatch: $($Report.zipSha256)"
    }
    if ([System.IO.Path]::GetFileName([string]$Report.outputZip) -cne $ZipName) {
        throw "Release report outputZip does not name the inspected ZIP: $($Report.outputZip)"
    }
    if ($Report.build.exitCode -ne 0) { throw 'Release report build.exitCode is not 0.' }
    if ($Report.build.timedOut -ne $false) { throw 'Release report build.timedOut is not false.' }
    $Gates = @($Report.gates)
    if ($Gates.Count -ne 12) { throw "Release report gate count must be 12, found $($Gates.Count)." }
    foreach ($Gate in $Gates) {
        if ([string]$Gate.status -cne 'PASS') {
            throw "Release report gate is not PASS: $($Gate.name)"
        }
    }
    $LogText = [System.IO.File]::ReadAllText($LogPath)
    if ($LogText.IndexOf('Release pipeline completed successfully.', [System.StringComparison]::Ordinal) -lt 0) {
        throw "Release log does not contain the successful completion marker: $LogPath"
    }
    if ($LogText.IndexOf('GATE FAIL', [System.StringComparison]::Ordinal) -ge 0) {
        throw "Release log contains GATE FAIL: $LogPath"
    }

    return [pscustomobject][ordered]@{
        ZipPath      = $ZipPath
        ZipName      = $ZipName
        ChecksumPath = $ChecksumPath
        ReportPath   = $ReportPath
        LogPath      = $LogPath
        Report       = $Report
        Sha256       = $ActualHash
    }
}

function Expand-ReleaseZipSafely
{
    param(
        [Parameter(Mandatory = $true)][string]$ZipPath,
        [Parameter(Mandatory = $true)][string]$DestinationRoot
    )

    if ([System.IO.Directory]::Exists($DestinationRoot))
    {
        throw "ZIP extraction destination already exists: $DestinationRoot"
    }
    [void][System.IO.Directory]::CreateDirectory($DestinationRoot)
    $ResolvedRoot = [System.IO.Path]::GetFullPath($DestinationRoot).TrimEnd('\', '/')
    $SeenEntries = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    $PathTypes = [System.Collections.Generic.Dictionary[string, string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    $TopLevels = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    $Archive = [System.IO.Compression.ZipFile]::OpenRead($ZipPath)
    try
    {
        foreach ($Entry in $Archive.Entries)
        {
            $EntryName = $Entry.FullName
            if ([string]::IsNullOrWhiteSpace($EntryName) -or $EntryName.Contains('\'))
            {
                throw "ZIP entry has an empty or backslash path: '$EntryName'"
            }
            $IsDirectory = $EntryName.EndsWith('/')
            $NormalizedEntry = $EntryName.TrimEnd('/')
            if ([string]::IsNullOrWhiteSpace($NormalizedEntry) -or
                $NormalizedEntry.StartsWith('/') -or $NormalizedEntry.StartsWith('\') -or
                $NormalizedEntry -match '^[A-Za-z]:' -or
                [System.IO.Path]::IsPathFullyQualified($NormalizedEntry))
            {
                throw "ZIP entry has an absolute or drive-qualified path: $EntryName"
            }
            $Segments = $NormalizedEntry.Split([char]'/')
            if (@($Segments | Where-Object { [string]::IsNullOrEmpty($_) -or $_ -eq '.' -or $_ -eq '..' }).Count -gt 0)
            {
                throw "ZIP entry has an unsafe path segment: $EntryName"
            }
            if ($Segments.Count -lt 2 -and -not $IsDirectory)
            {
                throw "ZIP file entry must be below the RuntimeAssetImport top-level directory: $EntryName"
            }
            [void]$TopLevels.Add($Segments[0])
            if ($Segments[0] -cne 'RuntimeAssetImport')
            {
                throw "ZIP entry is outside the RuntimeAssetImport top-level directory: $EntryName"
            }
            if (-not $SeenEntries.Add($NormalizedEntry))
            {
                throw "Case-insensitive duplicate ZIP entry: $EntryName"
            }

            $EntryType = if ($IsDirectory) { 'directory' } else { 'file' }
            if ($PathTypes.ContainsKey($NormalizedEntry))
            {
                if ($PathTypes[$NormalizedEntry] -ne $EntryType)
                {
                    throw "ZIP file/directory collision: $EntryName"
                }
                throw "Case-insensitive duplicate ZIP entry: $EntryName"
            }
            for ($Index = 1; $Index -lt $Segments.Count; $Index++)
            {
                $Parent = [string]::Join('/', $Segments[0..($Index - 1)])
                if ($PathTypes.ContainsKey($Parent) -and $PathTypes[$Parent] -eq 'file')
                {
                    throw "ZIP file/directory collision: $EntryName"
                }
                if (-not $PathTypes.ContainsKey($Parent))
                {
                    $PathTypes[$Parent] = 'directory'
                }
            }
            if ($EntryType -eq 'file')
            {
                foreach ($ExistingPath in @($PathTypes.Keys))
                {
                    if ($ExistingPath.StartsWith($NormalizedEntry + '/',
                            [System.StringComparison]::OrdinalIgnoreCase))
                    {
                        throw "ZIP file/directory collision: $EntryName"
                    }
                }
            }
            $PathTypes[$NormalizedEntry] = $EntryType
        }
        if ($TopLevels.Count -ne 1 -or -not $TopLevels.Contains('RuntimeAssetImport'))
        {
            throw 'ZIP must contain exactly one top-level RuntimeAssetImport directory.'
        }

        foreach ($Entry in $Archive.Entries)
        {
            $EntryName = $Entry.FullName
            $RelativeEntryPath = $EntryName.Replace('/', [System.IO.Path]::DirectorySeparatorChar).TrimEnd('\', '/')
            $Destination = [System.IO.Path]::GetFullPath((Join-Path $ResolvedRoot $RelativeEntryPath))
            if (-not (Test-IsSameOrDescendantPath -Root $ResolvedRoot -Candidate $Destination) -or
                $Destination.Equals($ResolvedRoot, [System.StringComparison]::OrdinalIgnoreCase))
            {
                throw "ZIP entry escapes the extraction root: $EntryName"
            }
            if ($EntryName.EndsWith('/'))
            {
                [void][System.IO.Directory]::CreateDirectory($Destination)
                continue
            }
            [void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($Destination))
            $InputStream = $Entry.Open()
            $OutputStream = [System.IO.File]::Open($Destination, [System.IO.FileMode]::CreateNew)
            try
            {
                $InputStream.CopyTo($OutputStream)
            }
            finally
            {
                $InputStream.Dispose()
                $OutputStream.Dispose()
            }
        }
    }
    finally
    {
        $Archive.Dispose()
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

    $SourceSampleCandidate = if ($PSBoundParameters.ContainsKey('SampleRoot')) {
        $SampleRoot
    }
    else {
        Join-Path $PSScriptRoot '..\RuntimeAssetImportSample'
    }
    $SourceSampleRoot = Assert-SampleRepository `
        -RepositoryRoot ([System.IO.Path]::GetFullPath($SourceSampleCandidate)) -GitPath $GitPath
    $ProductHead = Invoke-GitValue -GitPath $GitPath -RepositoryRoot $PSScriptRoot `
        -Arguments @('rev-parse', 'HEAD')

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
            -Arguments @('clone', '--recursive', '--no-local', '--branch', 'main', '--single-branch',
                $SourceSampleRoot, $TempSampleRoot) `
            -WorkingDirectory $WorkRoot)
    $TempSampleBranch = Invoke-GitValue -GitPath $GitPath -RepositoryRoot $TempSampleRoot `
        -Arguments @('branch', '--show-current')
    $TempSampleHead = Invoke-GitValue -GitPath $GitPath -RepositoryRoot $TempSampleRoot `
        -Arguments @('rev-parse', 'HEAD')
    $TempSampleOriginMain = Invoke-GitValue -GitPath $GitPath -RepositoryRoot $TempSampleRoot `
        -Arguments @('rev-parse', 'origin/main')
    $TempSampleStatus = Invoke-GitValue -GitPath $GitPath -RepositoryRoot $TempSampleRoot `
        -Arguments @('status', '--porcelain', '--untracked-files=all')
    if ($TempSampleBranch -cne 'main' -or $TempSampleHead -cne $ExpectedSampleHead -or
        $TempSampleOriginMain -cne $ExpectedSampleHead -or
        -not [string]::IsNullOrWhiteSpace($TempSampleStatus))
    {
        throw "Temp Sample clone must be main at $ExpectedSampleHead with origin/main at the same SHA and clean status."
    }

    $PackageOutput = Join-Path $WorkRoot 'P'
    if ([string]::IsNullOrWhiteSpace($PackageZipPath))
    {
        $PackageArguments = @(
            '-NoProfile', '-File', (Join-Path $PSScriptRoot 'PackageForFab.ps1'),
            '-EngineVersion', $EngineVersion, '-OutputDirectory', $PackageOutput)
        if ($PSBoundParameters.ContainsKey('ReleaseToolsRoot'))
        {
            $PackageArguments += @('-ReleaseToolsRoot', $ReleaseToolsRoot)
        }
        if ($PSBoundParameters.ContainsKey('EngineRoot'))
        {
            $PackageArguments += @('-EngineRoot', $EngineRoot)
        }
        [void](Invoke-NativeCommand -FilePath $PwshPath -Arguments $PackageArguments)
    }
    $ArtifactSet = Resolve-ReleaseArtifactSet -RequestedZipPath $PackageZipPath `
        -OutputDirectory $PackageOutput -RequestedEngineVersion $EngineVersion
    Write-Host "Release ZIP validated: $($ArtifactSet.ZipPath)" -ForegroundColor Green

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $ExtractionRoot = Join-Path $WorkRoot 'E'
    Expand-ReleaseZipSafely -ZipPath $ArtifactSet.ZipPath -DestinationRoot $ExtractionRoot
    $StagedPluginRoot = Join-Path $ExtractionRoot 'RuntimeAssetImport'
    Assert-RequiredDirectory -Path $StagedPluginRoot
    Assert-RequiredFile -Path (Join-Path $StagedPluginRoot 'RuntimeAssetImport.uplugin')

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

    if ($PSBoundParameters.ContainsKey('EngineRoot'))
    {
        $EngineRoot = [System.IO.Path]::GetFullPath($EngineRoot)
    }
    else
    {
        $EngineResolverPath = Join-Path $TempSampleRoot 'UnrealBuildRunTestScript\Get-UEInstallPath.ps1'
        $EngineResult = Invoke-NativeCommand -FilePath $PowerShellPath -Arguments @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $EngineResolverPath, '-Version', $EngineVersion)
        $EngineRoot = $EngineResult.StandardOutput.Trim()
    }
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
    foreach ($RequiredRootBooleanField in @(
            'CompressedMetadataGuardValid',
            'OversizedFileTextureDenied',
            'OversizedMemoryTextureDenied'))
    {
        if ($SmokeResult.$RequiredRootBooleanField -ne $true)
        {
            throw "Packaged smoke root field is not true: $RequiredRootBooleanField"
        }
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
    Write-Host 'Packaged Shipping smoke passed for the baseline formats, external and embedded resources, material values, memory I/O denial, and compressed texture metadata guards.' -ForegroundColor Green
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
