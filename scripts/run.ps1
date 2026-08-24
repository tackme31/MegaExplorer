<#
.SYNOPSIS
    Build MegaExplorer from the CLI and launch it, in Debug or Release.

.DESCRIPTION
    Wraps the whole CLI flow from CLAUDE.md's Build section: close a running
    instance, (re)configure if needed, build one build preset, then start the
    binary with Qt's and vcpkg's DLL directories on PATH.

    The configuration is not chosen by hand: it is read out of CMakePresets.json
    for the build preset being used, so an externally added preset works without
    editing this script.

.PARAMETER Config
    Debug (default) or Release. Shorthand for -Preset msvc-debug / msvc-release.

.PARAMETER Preset
    Build preset name, for anything that is not the two above. Overrides -Config.

.PARAMETER Target
    Build only these targets instead of everything the preset lists -- e.g.
    -Target MegaExplorer skips the two test binaries and megatool.

.PARAMETER Reconfigure
    Force the CMake configure step. Required after adding/removing a .qml file
    from QML_FILES, or moving the QML module to another target -- otherwise the
    stale qmlcache_loader.cpp aggregator breaks the link.

.PARAMETER Theme
    light | dark | system -- sets MEGAEXPLORER_COLOR_SCHEME for this run only,
    without touching the Windows theme setting.

.PARAMETER AppArgs
    Arguments forwarded to MegaExplorer.exe.

.EXAMPLE
    scripts\run.ps1
.EXAMPLE
    scripts\run.ps1 -Config Release -Theme dark
.EXAMPLE
    scripts\run.ps1 -NoRun -Reconfigure
.EXAMPLE
    scripts\run.ps1 -Target MegaExplorer
.EXAMPLE
    scripts\run.ps1 -Preset my-preset -AppArgs '--foo','bar'
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',

    [string]$Preset,

    [string[]]$Target = @(),

    [switch]$Reconfigure,
    [switch]$NoBuild,
    [switch]$NoRun,
    [switch]$Wait,

    [ValidateSet('light', 'dark', 'system')]
    [string]$Theme,

    [string[]]$AppArgs = @()
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

# The `cmake` on PATH is Strawberry Perl's 3.29: too old for this MSVC, and it
# overwrites CMakeCache.txt before failing. Always the full path.
$CMake = if ($env:MEGAEXPLORER_CMAKE) { $env:MEGAEXPLORER_CMAKE } else { 'C:/Qt/Tools/CMake_64/bin/cmake.exe' }
if (-not (Test-Path $CMake)) { throw "cmake not found at $CMake (override with `$env:MEGAEXPLORER_CMAKE)" }

# ------------------------------------------------------------ resolve presets
$buildPreset = if ($Preset) { $Preset } else { 'msvc-' + $Config.ToLower() }

$presetsFile = Join-Path $RepoRoot 'CMakePresets.json'
$presets = Get-Content -Raw -LiteralPath $presetsFile | ConvertFrom-Json
$bp = $presets.buildPresets | Where-Object { $_.name -eq $buildPreset }
if (-not $bp) {
    $known = ($presets.buildPresets | ForEach-Object { $_.name }) -join ', '
    throw "build preset '$buildPreset' is not in CMakePresets.json (known: $known)"
}

$configurePreset = $bp.configurePreset
$configuration = if ($bp.PSObject.Properties['configuration']) { $bp.configuration } else { $Config }

# binaryDir is named after the *configure* preset, so a Release build lands
# under build/msvc-debug/Release.
$buildDir = Join-Path $RepoRoot "build/$configurePreset"
$exePath = Join-Path $buildDir "$configuration/MegaExplorer.exe"

Write-Host "preset $buildPreset ($configuration) -> build/$configurePreset" -ForegroundColor Cyan

# --------------------------------------------------------------- close the app
# A running MegaExplorer.exe holds its own .exe open; the link dies LNK1104.
if (-not $NoBuild) {
    $running = Get-Process -Name MegaExplorer -ErrorAction SilentlyContinue
    if ($running) {
        $running | Stop-Process -Force
        $running | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue
        Write-Host 'closed running MegaExplorer.exe'
    }
}

# --------------------------------------------------------- configure and build
if (-not $NoBuild) {
    if ($Reconfigure -or -not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
        Write-Host "configuring ($configurePreset)..." -ForegroundColor Cyan
        & $CMake --preset $configurePreset
        if ($LASTEXITCODE -ne 0) { throw "configure failed ($LASTEXITCODE)" }
    }

    Write-Host "building ($buildPreset)..." -ForegroundColor Cyan
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $buildArgs = @('--build', '--preset', $buildPreset)
    foreach ($t in $Target) { $buildArgs += @('--target', $t) }
    & $CMake @buildArgs
    if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
    Write-Host ("build ok ({0:n0}s)" -f $sw.Elapsed.TotalSeconds) -ForegroundColor Green
}

if ($NoRun) { return }

# ---------------------------------------------------------------------- launch
if (-not (Test-Path $exePath)) { throw "$exePath not found -- build first" }

$qtBin = if ($env:MEGAEXPLORER_QT_DIR) { Join-Path $env:MEGAEXPLORER_QT_DIR 'bin' } else { 'C:/Qt/6.11.1/msvc2022_64/bin' }
# vcpkg keeps the debug DLLs in a separate subtree; Release needs the other one.
$vcpkgBin = Join-Path $buildDir ("vcpkg_installed/x64-windows-mega/" + $(if ($configuration -eq 'Debug') { 'debug/bin' } else { 'bin' }))
foreach ($dir in @($qtBin, $vcpkgBin)) {
    if (-not (Test-Path $dir)) { throw "DLL directory missing: $dir" }
}
$env:PATH = "$qtBin;$vcpkgBin;$env:PATH"

if ($Theme) {
    if ($Theme -eq 'system') { $env:MEGAEXPLORER_COLOR_SCHEME = $null }
    else { $env:MEGAEXPLORER_COLOR_SCHEME = $Theme }
}

Write-Host "launching $exePath" -ForegroundColor Cyan
$startArgs = @{ FilePath = $exePath; WorkingDirectory = (Split-Path $exePath) }
if ($AppArgs.Count -gt 0) { $startArgs.ArgumentList = $AppArgs }
if ($Wait) { $startArgs.Wait = $true; $startArgs.NoNewWindow = $true }
Start-Process @startArgs
