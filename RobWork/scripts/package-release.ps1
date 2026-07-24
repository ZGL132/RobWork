<#
.SYNOPSIS
Build and package a RobWork binary release.

.DESCRIPTION
Stages RobWork, RobWorkStudio, optional RobWorkSim, Qt runtime files, and
third-party DLLs into a release folder, writes a manifest, and creates a zip.

Examples:
  powershell -ExecutionPolicy Bypass -File .\scripts\package-release.ps1
  powershell -ExecutionPolicy Bypass -File .\scripts\package-release.ps1 -SkipBuild
  powershell -ExecutionPolicy Bypass -File .\scripts\package-release.ps1 -DryRun -AllowMissingRobWorkSim
  powershell -ExecutionPolicy Bypass -File .\scripts\package-release.ps1 -SkipConfigure -AllowMissingRobWorkSim
#>

[CmdletBinding()]
param(
    [string]$BuildDir = "build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release",
    [string]$Configuration = "Release",
    [string]$OutputDir = "dist",
    [string]$PackageName = "RobWorkProject",
    [string]$QtBin = "",
    [string]$VcpkgBin = "",
    [string]$VsDevCmd = "",
    [switch]$AllowMissingRobWorkSim,
    [switch]$SkipConfigure,
    [switch]$SkipBuild,
    [switch]$SkipQtDeploy,
    [switch]$SkipArchive,
    [switch]$DryRun
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
}

function Resolve-ExistingPath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction SilentlyContinue
    if (-not $resolved) {
        throw "$Description not found: $Path"
    }

    return $resolved.Path
}

function Read-CMakeCache {
    param([Parameter(Mandatory = $true)][string]$CachePath)

    $values = @{}
    foreach ($line in Get-Content -LiteralPath $CachePath) {
        if ($line -match "^(?<key>[^#/:=][^:=]*)(:(?<type>[^=]+))?=(?<value>.*)$") {
            $values[$Matches["key"]] = $Matches["value"]
        }
    }
    return $values
}

function Convert-ToAbsolutePath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$BasePath
    )

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $BasePath $Path))
}

function Invoke-Step {
    param(
        [Parameter(Mandatory = $true)][string]$Message,
        [Parameter(Mandatory = $true)][scriptblock]$Action
    )

    Write-Host "==> $Message"
    if (-not $DryRun) {
        & $Action
    }
}

function Test-NameMatches {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string[]]$Patterns
    )

    foreach ($pattern in $Patterns) {
        if ($Name -like $pattern) {
            return $true
        }
    }
    return $false
}

function Copy-Tree {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination,
        [string]$Filter = "*"
    )

    if (-not (Test-Path -LiteralPath $Source)) {
        return 0
    }

    $items = @(Get-ChildItem -LiteralPath $Source -Recurse -File -Filter $Filter -ErrorAction SilentlyContinue)
    Write-Host ("    {0} files: {1} -> {2}" -f $items.Count, $Source, $Destination)
    if ($DryRun) {
        return $items.Count
    }

    foreach ($item in $items) {
        $relative = $item.FullName.Substring($Source.Length).TrimStart("\", "/")
        $target = Join-Path $Destination $relative
        $targetDir = Split-Path -Parent $target
        New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
        Copy-Item -LiteralPath $item.FullName -Destination $target -Force
    }

    return $items.Count
}

function Copy-SelectedTree {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string[]]$Include
    )

    if (-not (Test-Path -LiteralPath $Source)) {
        return 0
    }

    $items = @(Get-ChildItem -LiteralPath $Source -Recurse -File -ErrorAction SilentlyContinue | Where-Object { Test-NameMatches -Name $_.Name -Patterns $Include })
    Write-Host ("    {0} files: {1} -> {2}" -f $items.Count, $Source, $Destination)
    if ($DryRun) {
        return $items.Count
    }

    foreach ($item in $items) {
        $relative = $item.FullName.Substring($Source.Length).TrimStart("\", "/")
        $target = Join-Path $Destination $relative
        $targetDir = Split-Path -Parent $target
        New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
        Copy-Item -LiteralPath $item.FullName -Destination $target -Force
    }

    return $items.Count
}

function Copy-FlatFiles {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string[]]$Include
    )

    if (-not (Test-Path -LiteralPath $Source)) {
        return 0
    }

    $items = @(Get-ChildItem -LiteralPath $Source -File -ErrorAction SilentlyContinue | Where-Object { Test-NameMatches -Name $_.Name -Patterns $Include })
    Write-Host ("    {0} files: {1} -> {2}" -f $items.Count, $Source, $Destination)
    if ($DryRun) {
        return $items.Count
    }

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    foreach ($item in $items) {
        Copy-Item -LiteralPath $item.FullName -Destination (Join-Path $Destination $item.Name) -Force
    }

    return $items.Count
}

function Get-GitRevision {
    param([Parameter(Mandatory = $true)][string]$RepoRoot)

    $oldCount = $env:GIT_CONFIG_COUNT
    $oldKey0 = $env:GIT_CONFIG_KEY_0
    $oldValue0 = $env:GIT_CONFIG_VALUE_0
    $oldKey1 = $env:GIT_CONFIG_KEY_1
    $oldValue1 = $env:GIT_CONFIG_VALUE_1
    try {
        $env:GIT_CONFIG_COUNT = "2"
        $env:GIT_CONFIG_KEY_0 = "safe.directory"
        $env:GIT_CONFIG_VALUE_0 = (Split-Path -Parent $RepoRoot).Replace("\", "/")
        $env:GIT_CONFIG_KEY_1 = "safe.directory"
        $env:GIT_CONFIG_VALUE_1 = $RepoRoot.Replace("\", "/")
        $revision = (& git -C $RepoRoot rev-parse --short HEAD 2>$null)
        if ($LASTEXITCODE -ne 0) {
            return ""
        }
        return ($revision | Select-Object -First 1)
    }
    catch {
        return ""
    }
    finally {
        $env:GIT_CONFIG_COUNT = $oldCount
        $env:GIT_CONFIG_KEY_0 = $oldKey0
        $env:GIT_CONFIG_VALUE_0 = $oldValue0
        $env:GIT_CONFIG_KEY_1 = $oldKey1
        $env:GIT_CONFIG_VALUE_1 = $oldValue1
    }
}

function Set-GitSafeDirectoryEnvironment {
    param([Parameter(Mandatory = $true)][string]$RepoRoot)

    $gitRoot = (Split-Path -Parent $RepoRoot).Replace("\", "/")
    $sourceRoot = $RepoRoot.Replace("\", "/")
    $env:GIT_CONFIG_COUNT = "2"
    $env:GIT_CONFIG_KEY_0 = "safe.directory"
    $env:GIT_CONFIG_VALUE_0 = $gitRoot
    $env:GIT_CONFIG_KEY_1 = "safe.directory"
    $env:GIT_CONFIG_VALUE_1 = $sourceRoot
}

function Get-FileCount {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return 0
    }
    return @(Get-ChildItem -LiteralPath $Path -Recurse -File -ErrorAction SilentlyContinue).Count
}

function Invoke-CMakeConfigure {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$BuildRoot,
        [Parameter(Mandatory = $true)]$Cache
    )

    $args = @("-S", $RepoRoot, "-B", $BuildRoot, "-DWITH_RWSIM=ON", "-DWITH_RWS=ON")
    if ($Cache.ContainsKey("CMAKE_GENERATOR") -and $Cache["CMAKE_GENERATOR"]) {
        $args += @("-G", $Cache["CMAKE_GENERATOR"])
    }
    foreach ($key in @("CMAKE_BUILD_TYPE", "CMAKE_PREFIX_PATH", "CMAKE_TOOLCHAIN_FILE", "VCPKG_TARGET_TRIPLET")) {
        if ($Cache.ContainsKey($key) -and $Cache[$key]) {
            $args += "-D$key=$($Cache[$key])"
        }
    }

    Write-Host ("    cmake {0}" -f ($args -join " "))
    & cmake @args
    if ($LASTEXITCODE -ne 0) {
        throw "cmake configure failed with exit code $LASTEXITCODE"
    }
}

function Test-CommandExists {
    param([Parameter(Mandatory = $true)][string]$Name)

    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

function Import-VsDevEnvironment {
    param([Parameter(Mandatory = $true)][string]$VsDevCmdPath)

    if (-not (Test-Path -LiteralPath $VsDevCmdPath)) {
        throw "VsDevCmd.bat not found: $VsDevCmdPath"
    }

    Write-Host "==> Import Visual Studio build environment"
    $command = "`"$VsDevCmdPath`" -arch=amd64 -host_arch=amd64 > nul && set"
    $lines = & cmd.exe /s /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "VsDevCmd.bat failed with exit code $LASTEXITCODE"
    }

    foreach ($line in $lines) {
        $index = $line.IndexOf("=")
        if ($index -le 0) {
            continue
        }
        $name = $line.Substring(0, $index)
        $value = $line.Substring($index + 1)
        Set-Item -Path "Env:$name" -Value $value
    }
}

$repoRoot = Resolve-RepoRoot
Set-GitSafeDirectoryEnvironment -RepoRoot $repoRoot
$buildRoot = Resolve-ExistingPath -Path (Convert-ToAbsolutePath -Path $BuildDir -BasePath $repoRoot) -Description "Build directory"
$cachePath = Resolve-ExistingPath -Path (Join-Path $buildRoot "CMakeCache.txt") -Description "CMake cache"
$cache = Read-CMakeCache -CachePath $cachePath

$needsBuildEnvironment = ((-not $SkipBuild) -or ((-not $SkipConfigure) -and (-not $AllowMissingRobWorkSim)))
if ((-not $DryRun) -and $needsBuildEnvironment -and (-not (Test-CommandExists -Name "cl.exe"))) {
    if ([string]::IsNullOrWhiteSpace($VsDevCmd)) {
        $candidateVsDevCmd = "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
        if (Test-Path -LiteralPath $candidateVsDevCmd) {
            $VsDevCmd = $candidateVsDevCmd
        }
    }
    if ($VsDevCmd) {
        Import-VsDevEnvironment -VsDevCmdPath $VsDevCmd
    }
    else {
        Write-Warning "cl.exe is not on PATH. Run from a VS x64 Developer PowerShell or pass -VsDevCmd."
    }
}

$robWorkBuild = Join-Path $buildRoot "RobWork"
$studioBuild = Join-Path $buildRoot "RobWorkStudio"
$simBuild = Join-Path $buildRoot "RobWorkSim"

Resolve-ExistingPath -Path (Join-Path $robWorkBuild "bin") -Description "RobWork bin directory" | Out-Null
Resolve-ExistingPath -Path (Join-Path $studioBuild "bin") -Description "RobWorkStudio bin directory" | Out-Null

$withRobWorkSim = $false
if ($cache.ContainsKey("WITH_RWSIM")) {
    $withRobWorkSim = ($cache["WITH_RWSIM"] -match "^(ON|TRUE|1)$")
}
$hasRobWorkSimBuild = (Test-Path -LiteralPath $simBuild)
$includeRobWorkSim = ($withRobWorkSim -and $hasRobWorkSimBuild)

if ((-not $AllowMissingRobWorkSim) -and (-not $includeRobWorkSim) -and (-not $SkipConfigure)) {
    Invoke-Step "Configure build with RobWorkSim enabled" {
        Invoke-CMakeConfigure -RepoRoot $repoRoot -BuildRoot $buildRoot -Cache $cache
    }
    if (-not $DryRun) {
        $cache = Read-CMakeCache -CachePath $cachePath
        $withRobWorkSim = ($cache.ContainsKey("WITH_RWSIM") -and ($cache["WITH_RWSIM"] -match "^(ON|TRUE|1)$"))
        $hasRobWorkSimBuild = (Test-Path -LiteralPath $simBuild)
        $includeRobWorkSim = ($withRobWorkSim -and $hasRobWorkSimBuild)
    }
    else {
        Write-Host "    Dry run: configure step was skipped; RobWorkSim staging will be validated after a real configure/build."
    }
}

if ((-not $AllowMissingRobWorkSim) -and (-not $includeRobWorkSim)) {
    if ($DryRun -and (-not $SkipConfigure)) {
        Write-Warning "RobWorkSim is not present in the current build yet. A real run will configure with -DWITH_RWSIM=ON before building."
    }
    else {
        throw "RobWorkSim is required, but this build does not contain it. CMake WITH_RWSIM=$($cache['WITH_RWSIM']); directory exists=$hasRobWorkSimBuild. Reconfigure with -DWITH_RWSIM=ON and rebuild before packaging."
    }
}

if ([string]::IsNullOrWhiteSpace($QtBin)) {
    if ($cache.ContainsKey("Qt6_DIR")) {
        $qtDir = $cache["Qt6_DIR"]
        $QtBin = [System.IO.Path]::GetFullPath((Join-Path $qtDir "..\..\..\bin"))
    }
    elseif ($env:QTDIR) {
        $QtBin = Join-Path $env:QTDIR "bin"
    }
}
if ($QtBin) {
    $QtBin = Convert-ToAbsolutePath -Path $QtBin -BasePath $repoRoot
}

if ([string]::IsNullOrWhiteSpace($VcpkgBin)) {
    $candidateVcpkg = Join-Path (Split-Path -Parent $repoRoot) "vcpkg\installed\x64-windows\bin"
    if (Test-Path -LiteralPath $candidateVcpkg) {
        $VcpkgBin = $candidateVcpkg
    }
}
if ($VcpkgBin) {
    $VcpkgBin = Convert-ToAbsolutePath -Path $VcpkgBin -BasePath $repoRoot
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outputRoot = Convert-ToAbsolutePath -Path $OutputDir -BasePath $repoRoot
$stageRoot = Join-Path $outputRoot "$PackageName-$timestamp"
$archivePath = Join-Path $outputRoot "$PackageName-$timestamp.zip"

Write-Host "RobWork release packaging"
Write-Host "  Source:       $repoRoot"
Write-Host "  Build:        $buildRoot"
Write-Host "  Output:       $outputRoot"
Write-Host "  Stage:        $stageRoot"
Write-Host "  Configuration:$Configuration"
Write-Host "  WITH_RWSIM:   $($cache['WITH_RWSIM'])"
Write-Host "  RobWorkSim:   $hasRobWorkSimBuild"
Write-Host "  Require sim:  $(-not $AllowMissingRobWorkSim)"
Write-Host "  Qt bin:       $QtBin"
Write-Host "  vcpkg bin:    $VcpkgBin"
Write-Host "  VsDevCmd:     $VsDevCmd"
if ($DryRun) {
    Write-Host "  Mode:         dry run"
}

if (-not $SkipBuild) {
    Invoke-Step "Build all configured targets" {
        & cmake --build $buildRoot --config $Configuration
        if ($LASTEXITCODE -ne 0) {
            throw "cmake build failed with exit code $LASTEXITCODE"
        }
    }
}

Invoke-Step "Prepare staging directory" {
    if (Test-Path -LiteralPath $stageRoot) {
        $resolvedStage = (Resolve-Path -LiteralPath $stageRoot).Path
        $resolvedOutput = [System.IO.Path]::GetFullPath($outputRoot)
        if (-not $resolvedStage.StartsWith($resolvedOutput, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove unexpected staging path: $resolvedStage"
        }
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $stageRoot | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $stageRoot "bin") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $stageRoot "lib") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $stageRoot "include") | Out-Null
}

$copied = [ordered]@{}
$copied["RobWorkBin"] = Copy-Tree -Source (Join-Path $robWorkBuild "bin") -Destination (Join-Path $stageRoot "bin")
$copied["RobWorkStudioBin"] = Copy-Tree -Source (Join-Path $studioBuild "bin") -Destination (Join-Path $stageRoot "bin")
$copied["RobWorkLib"] = Copy-Tree -Source (Join-Path $robWorkBuild "libs") -Destination (Join-Path $stageRoot "lib\RobWork")
$copied["RobWorkStudioLib"] = Copy-Tree -Source (Join-Path $studioBuild "libs") -Destination (Join-Path $stageRoot "lib\RobWorkStudio")
$copied["RobWorkStudioPlugins"] = Copy-Tree -Source (Join-Path $studioBuild "libs\plugins") -Destination (Join-Path $stageRoot "bin\plugins\RobWorkStudio")
$copied["RobWorkIncludes"] = Copy-SelectedTree -Source (Join-Path $repoRoot "RobWork\src") -Destination (Join-Path $stageRoot "include\RobWork") -Include @("*.hpp", "*.h")
$copied["RobWorkStudioIncludes"] = Copy-SelectedTree -Source (Join-Path $repoRoot "RobWorkStudio\src") -Destination (Join-Path $stageRoot "include\RobWorkStudio") -Include @("*.hpp", "*.h")
$copied["RobWorkCMake"] = Copy-SelectedTree -Source (Join-Path $repoRoot "RobWork\cmake") -Destination (Join-Path $stageRoot "cmake\RobWork") -Include @("*.cmake", "*.in")
$copied["RobWorkStudioCMake"] = Copy-SelectedTree -Source (Join-Path $repoRoot "RobWorkStudio\cmake") -Destination (Join-Path $stageRoot "cmake\RobWorkStudio") -Include @("*.cmake", "*.in")

if ($includeRobWorkSim) {
    $copied["RobWorkSimBin"] = Copy-Tree -Source (Join-Path $simBuild "bin") -Destination (Join-Path $stageRoot "bin")
    $copied["RobWorkSimLib"] = Copy-Tree -Source (Join-Path $simBuild "libs") -Destination (Join-Path $stageRoot "lib\RobWorkSim")
    $copied["RobWorkSimPlugins"] = Copy-Tree -Source (Join-Path $simBuild "libs\plugins") -Destination (Join-Path $stageRoot "bin\plugins\RobWorkSim")
    $copied["RobWorkSimIncludes"] = Copy-SelectedTree -Source (Join-Path $repoRoot "RobWorkSim\src") -Destination (Join-Path $stageRoot "include\RobWorkSim") -Include @("*.hpp", "*.h")
    $copied["RobWorkSimCMake"] = Copy-SelectedTree -Source (Join-Path $repoRoot "RobWorkSim\cmake") -Destination (Join-Path $stageRoot "cmake\RobWorkSim") -Include @("*.cmake", "*.in")
}
elseif ($AllowMissingRobWorkSim) {
    Write-Warning "RobWorkSim is not included. CMake WITH_RWSIM=$($cache['WITH_RWSIM']); directory exists=$hasRobWorkSimBuild."
}

$copied["RootDocs"] = Copy-FlatFiles -Source $repoRoot -Destination $stageRoot -Include @("LICENSE*", "NOTICE*", "README*", "CHANGELOG*")

if ($VcpkgBin -and (Test-Path -LiteralPath $VcpkgBin)) {
    $copied["VcpkgDlls"] = Copy-FlatFiles -Source $VcpkgBin -Destination (Join-Path $stageRoot "bin") -Include @("*.dll")
}

if (-not $SkipQtDeploy) {
    $windeployqt = if ($QtBin) { Join-Path $QtBin "windeployqt.exe" } else { "" }
    $studioExe = Join-Path $stageRoot "bin\RobWorkStudio.exe"
    if ($windeployqt -and (Test-Path -LiteralPath $windeployqt)) {
        Invoke-Step "Deploy Qt runtime with windeployqt" {
            if (-not (Test-Path -LiteralPath $studioExe)) {
                throw "RobWorkStudio.exe was not staged; cannot run windeployqt."
            }
            & $windeployqt --release --compiler-runtime --no-translations $studioExe
            if ($LASTEXITCODE -ne 0) {
                throw "windeployqt failed with exit code $LASTEXITCODE"
            }
        }
    }
    else {
        Write-Warning "windeployqt.exe was not found. Use -QtBin or -SkipQtDeploy."
    }
}

$gitRevision = Get-GitRevision -RepoRoot $repoRoot
$manifest = [ordered]@{
    packageName = $PackageName
    createdAt = (Get-Date).ToString("o")
    sourceRoot = $repoRoot
    buildDir = $buildRoot
    configuration = $Configuration
    gitRevision = $gitRevision
    cmakeGenerator = $cache["CMAKE_GENERATOR"]
    cmakeBuildType = $cache["CMAKE_BUILD_TYPE"]
    withRobWorkSim = $withRobWorkSim
    robWorkSimIncluded = $includeRobWorkSim
    qtBin = $QtBin
    vcpkgBin = $VcpkgBin
    copiedGroups = $copied
    fileCount = if ($DryRun) { 0 } else { Get-FileCount -Path $stageRoot }
}

Invoke-Step "Write manifest" {
    $manifestPath = Join-Path $stageRoot "manifest.json"
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
}

if (-not $SkipArchive) {
    Invoke-Step "Create zip archive" {
        New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
        if (Test-Path -LiteralPath $archivePath) {
            Remove-Item -LiteralPath $archivePath -Force
        }
        Compress-Archive -LiteralPath $stageRoot -DestinationPath $archivePath -Force
    }
}

if ($DryRun) {
    Write-Host "Dry run finished. No files were copied or archived."
}
else {
    Write-Host "Release staged at: $stageRoot"
    if (-not $SkipArchive) {
        Write-Host "Archive created:   $archivePath"
    }
}
