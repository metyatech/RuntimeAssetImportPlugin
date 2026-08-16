[CmdletBinding()]
param(
    [ValidatePattern('^5\.[0-9]+$')]
    [string]$EngineVersion = '5.8',

    [string]$ReleaseToolsRoot,

    [string]$EngineRoot,

    [string]$OutputDirectory,

    [switch]$KeepWorkingDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ExpectedToolCommit = 'c59540b1f562b3baf6f07ad6bf919888a0e410d4'
$ExpectedToolVersion = '0.4.0'

function ConvertTo-NativeArgument {
    param([AllowEmptyString()][string]$Argument)

    if ($Argument.Length -gt 0 -and $Argument -notmatch '[\s"]') {
        return $Argument
    }

    $builder = [System.Text.StringBuilder]::new()
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

function Invoke-CapturedProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [switch]$DisplayOutput
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    if ($null -ne $startInfo.PSObject.Properties['ArgumentList']) {
        foreach ($argument in $Arguments) {
            [void]$startInfo.ArgumentList.Add($argument)
        }
    }
    else {
        $quotedArguments = @($Arguments | ForEach-Object {
                ConvertTo-NativeArgument -Argument $_
            })
        $startInfo.Arguments = [string]::Join(' ', $quotedArguments)
    }

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Failed to start process: $FilePath"
    }
    $standardOutputTask = $process.StandardOutput.ReadToEndAsync()
    $standardErrorTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $standardOutput = $standardOutputTask.GetAwaiter().GetResult()
    $standardError = $standardErrorTask.GetAwaiter().GetResult()
    $exitCode = $process.ExitCode
    $process.Dispose()

    if ($DisplayOutput) {
        if (-not [string]::IsNullOrEmpty($standardOutput)) {
            [Console]::Out.Write($standardOutput)
        }
        if (-not [string]::IsNullOrEmpty($standardError)) {
            [Console]::Error.Write($standardError)
        }
    }

    return [pscustomobject][ordered]@{
        ExitCode       = $exitCode
        StandardOutput = $standardOutput
        StandardError  = $standardError
    }
}

function Get-GitCommandPath {
    $commands = @(Get-Command 'git.exe' -CommandType Application -ErrorAction SilentlyContinue)
    if ($commands.Count -eq 0) {
        throw 'git.exe was not found on PATH; install Git or add it to PATH.'
    }
    return [string]$commands[0].Source
}

function Invoke-Git {
    param(
        [Parameter(Mandatory = $true)][string]$GitPath,
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $result = Invoke-CapturedProcess -FilePath $GitPath -Arguments (@('-C', $RepositoryRoot) + $Arguments) `
        -WorkingDirectory $RepositoryRoot
    if ($result.ExitCode -ne 0) {
        throw "Git command failed with exit code $($result.ExitCode): git -C $RepositoryRoot $($Arguments -join ' ')`n$($result.StandardError.Trim())"
    }
    return $result.StandardOutput.Trim()
}

function Test-IsSameOrDescendantPath {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Candidate
    )

    $resolvedRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $resolvedCandidate = [System.IO.Path]::GetFullPath($Candidate).TrimEnd('\', '/')
    return $resolvedCandidate.Equals($resolvedRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
        $resolvedCandidate.StartsWith($resolvedRoot + [System.IO.Path]::DirectorySeparatorChar,
            [System.StringComparison]::OrdinalIgnoreCase)
}

function Resolve-ReleaseToolsRoot {
    param(
        [Parameter(Mandatory = $true)][string]$ProductRoot,
        [Parameter(Mandatory = $true)][bool]$ExplicitlyProvided,
        [string]$ExplicitRoot
    )

    if ($ExplicitlyProvided) {
        if ([string]::IsNullOrWhiteSpace($ExplicitRoot)) {
            throw 'ReleaseToolsRoot was explicitly provided but empty. Set -ReleaseToolsRoot to the fab-plugin-release-tools checkout.'
        }
        return [System.IO.Path]::GetFullPath($ExplicitRoot)
    }
    if (-not [string]::IsNullOrWhiteSpace($env:FAB_PLUGIN_RELEASE_TOOLS_ROOT)) {
        return [System.IO.Path]::GetFullPath($env:FAB_PLUGIN_RELEASE_TOOLS_ROOT)
    }
    return [System.IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $ProductRoot) 'fab-plugin-release-tools'))
}

function Assert-ReleaseToolsRoot {
    param(
        [Parameter(Mandatory = $true)][string]$ToolRoot,
        [Parameter(Mandatory = $true)][string]$ProductRoot,
        [Parameter(Mandatory = $true)][string]$GitPath
    )

    if (-not [System.IO.Directory]::Exists($ToolRoot)) {
        throw "fab-plugin-release-tools checkout was not found at '$ToolRoot'. Set -ReleaseToolsRoot, FAB_PLUGIN_RELEASE_TOOLS_ROOT, or place the checkout beside the product repository."
    }
    if (Test-IsSameOrDescendantPath -Root $ProductRoot -Candidate $ToolRoot) {
        throw "Release tools root must not be the product root or a directory below it: $ToolRoot"
    }

    $repositoryRoot = Invoke-Git -GitPath $GitPath -RepositoryRoot $ToolRoot -Arguments @('rev-parse', '--show-toplevel')
    $resolvedToolRoot = [System.IO.Path]::GetFullPath($ToolRoot).TrimEnd('\', '/')
    $normalizedRepositoryRoot = [System.IO.Path]::GetFullPath($repositoryRoot).TrimEnd('\', '/')
    if (-not $normalizedRepositoryRoot.Equals($resolvedToolRoot,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Release tools root must be the Git repository root. Git reported '$repositoryRoot' for '$ToolRoot'."
    }

    $head = Invoke-Git -GitPath $GitPath -RepositoryRoot $ToolRoot -Arguments @('rev-parse', 'HEAD')
    if ($head -cne $ExpectedToolCommit) {
        throw "Release tools HEAD mismatch. Expected $ExpectedToolCommit but found $head."
    }
    $status = Invoke-Git -GitPath $GitPath -RepositoryRoot $ToolRoot -Arguments @(
        'status', '--porcelain', '--untracked-files=all')
    if (-not [string]::IsNullOrWhiteSpace($status)) {
        throw "Release tools working tree must be clean: $ToolRoot"
    }

    foreach ($requiredFile in @(
            'Invoke-FabPluginRelease.ps1',
            'FabPluginReleaseTools.psd1',
            'FabPluginRelease.schema.json')) {
        if (-not [System.IO.File]::Exists((Join-Path $ToolRoot $requiredFile))) {
            throw "Required central release tool file is missing: $(Join-Path $ToolRoot $requiredFile)"
        }
    }
    $manifest = Import-PowerShellDataFile -LiteralPath (Join-Path $ToolRoot 'FabPluginReleaseTools.psd1')
    if ([string]$manifest.ModuleVersion -cne $ExpectedToolVersion) {
        throw "Release tools ModuleVersion mismatch. Expected $ExpectedToolVersion but found $($manifest.ModuleVersion)."
    }
}

$productRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$gitPath = $null
$childStarted = $false
try {
    $gitPath = Get-GitCommandPath
    $toolRoot = Resolve-ReleaseToolsRoot -ProductRoot $productRoot `
        -ExplicitlyProvided ($PSBoundParameters.ContainsKey('ReleaseToolsRoot')) -ExplicitRoot $ReleaseToolsRoot
    Assert-ReleaseToolsRoot -ToolRoot $toolRoot -ProductRoot $productRoot -GitPath $gitPath

    $configPath = Join-Path $productRoot 'FabPluginRelease.json'
    if (-not [System.IO.File]::Exists($configPath)) {
        throw "FabPluginRelease.json was not found at '$configPath'."
    }

    $pwshCommands = @(Get-Command 'pwsh.exe' -CommandType Application -ErrorAction SilentlyContinue)
    if ($pwshCommands.Count -eq 0) {
        throw 'pwsh.exe was not found on PATH; install PowerShell 7.4 or later and add it to PATH.'
    }
    $pwshPath = [string]$pwshCommands[0].Source

    $arguments = [System.Collections.Generic.List[string]]::new()
    $arguments.Add('-NoProfile')
    $arguments.Add('-File')
    $arguments.Add((Join-Path $toolRoot 'Invoke-FabPluginRelease.ps1'))
    $arguments.Add('-PluginPath')
    $arguments.Add($productRoot)
    $arguments.Add('-EngineVersion')
    $arguments.Add($EngineVersion)
    $arguments.Add('-ConfigPath')
    $arguments.Add($configPath)
    if ($PSBoundParameters.ContainsKey('EngineRoot')) {
        $arguments.Add('-EngineRoot')
        $arguments.Add($EngineRoot)
    }
    if ($PSBoundParameters.ContainsKey('OutputDirectory')) {
        $arguments.Add('-OutputDirectory')
        $arguments.Add($OutputDirectory)
    }
    if ($KeepWorkingDirectory) {
        $arguments.Add('-KeepWorkingDirectory')
    }

    $childStarted = $true
    $result = Invoke-CapturedProcess -FilePath $pwshPath -Arguments $arguments.ToArray() `
        -WorkingDirectory $productRoot -DisplayOutput
    exit $result.ExitCode
}
catch {
    Write-Error -ErrorRecord $_ -ErrorAction Continue
    if (-not $childStarted) {
        Write-Output 'FAB PLUGIN RELEASE: FAIL'
    }
    exit 1
}
