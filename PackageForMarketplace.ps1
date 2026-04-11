# PackageForMarketplace.ps1
# This script packages the RuntimeAssetImportPlugin for Unreal Engine Marketplace / Fab submission.

$PluginName = "RuntimeAssetImport"
$OutputDir = Join-Path $PSScriptRoot "PackedForMarketplace"
$ZipFile = Join-Path $PSScriptRoot "$($PluginName)_Marketplace.zip"

# 1. Clean up old output
if (Test-Path $OutputDir) { Remove-Item -Recurse -Force $OutputDir }
if (Test-Path $ZipFile) { Remove-Item -Force $ZipFile }

# 2. Create staging directory
New-Item -ItemType Directory -Path $OutputDir | Out-Null

# 3. Copy necessary folders and files
# Marketplace requirements: Source, Content, Resources, .uplugin are essential.
# We also include our prebuilt ThirdParty binaries.
$Includes = @("Source", "Content", "Resources", "*.uplugin", "README.md", "LICENSE")
$Excludes = @(".git*", ".github", "Intermediate", "Binaries", "Packed*", "Config", "*.bat", "*.sh", "agent-ruleset.json", "AGENTS.md", "CLAUDE.md")

Write-Host "Staging files for $PluginName..." -ForegroundColor Cyan

Get-ChildItem -Path $PSScriptRoot -File | ForEach-Object {
    $fileName = $_.Name
    $shouldInclude = $false
    foreach ($pattern in $Includes) {
        if ($fileName -like $pattern) { $shouldInclude = $true; break }
    }
    if ($shouldInclude) {
        Copy-Item $_.FullName -Destination $OutputDir
    }
}

$IncludeFolders = @("Source", "Content", "Resources")

Write-Host "Staging folders..." -ForegroundColor Cyan
foreach ($folder in $IncludeFolders) {
    $SrcPath = Join-Path $PSScriptRoot $folder
    if (Test-Path $SrcPath) {
        $DestPath = Join-Path $OutputDir $folder
        Write-Host "  Copying $folder..."
        robocopy $SrcPath $DestPath /MIR /XD $Excludes /XF $Excludes /R:3 /W:5 | Out-Null
    } else {
        Write-Warning "Folder not found: $folder"
    }
}

# 4. Final verification
if (!(Test-Path (Join-Path $OutputDir "$PluginName.uplugin"))) {
    Write-Error "Failed to find .uplugin in staging area!"
    exit 1
}

# 5. Create ZIP
Write-Host "Creating archive: $ZipFile" -ForegroundColor Green
Compress-Archive -Path "$OutputDir\*" -DestinationPath $ZipFile

Write-Host "Done! Package is ready for upload at: $ZipFile" -ForegroundColor Green
