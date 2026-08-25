<#
.SYNOPSIS
    Build MegaExplorer and produce the distributable zip.

.DESCRIPTION
    The one fixed procedure for making a release artifact: build the Release
    binary, run CPack's ZIP generator over the install rules (which pull in Qt's
    DLLs and QML modules via windeployqt, FFmpeg's DLLs from vcpkg, and the MSVC
    runtime), then check the archive actually contains what a receiving machine
    needs before calling it done.

    The check is not ceremony: every failure mode this script exists to prevent
    -- a missing Qt DLL, a missing licence file -- produces a zip that builds
    and packages cleanly and then dies on someone else's desktop.

.PARAMETER Config
    Release (default) or Debug. A Debug zip needs the debug CRT, which is not
    redistributable, so it is for local checking only -- never ship one.

.PARAMETER SkipBuild
    Package whatever is already built. Fails if the binary is missing.

.EXAMPLE
    scripts\package.ps1
.EXAMPLE
    scripts\package.ps1 -SkipBuild
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release',

    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

# The `cmake` on PATH is Strawberry Perl's 3.29: too old for this MSVC, and it
# overwrites CMakeCache.txt before failing. Always the full path.
$CMake = if ($env:MEGAEXPLORER_CMAKE) { $env:MEGAEXPLORER_CMAKE } else { 'C:/Qt/Tools/CMake_64/bin/cmake.exe' }
if (-not (Test-Path $CMake)) { throw "cmake not found at $CMake (override with `$env:MEGAEXPLORER_CMAKE)" }
$CPack = Join-Path (Split-Path $CMake) 'cpack.exe'
if (-not (Test-Path $CPack)) { throw "cpack not found at $CPack" }

$configurePreset = 'msvc-debug'
$buildPreset = 'msvc-' + $Config.ToLower()
$buildDir = Join-Path $RepoRoot "build/$configurePreset"
$exePath = Join-Path $buildDir "$Config/MegaExplorer.exe"

if (-not $SkipBuild) {
    # A running MegaExplorer.exe holds its own .exe open; the link dies LNK1104.
    $running = Get-Process -Name MegaExplorer -ErrorAction SilentlyContinue
    if ($running) {
        $running | Stop-Process -Force
        $running | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue
    }

    if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
        Write-Host "configuring ($configurePreset)..." -ForegroundColor Cyan
        & $CMake --preset $configurePreset
        if ($LASTEXITCODE -ne 0) { throw "configure failed ($LASTEXITCODE)" }
    }

    Write-Host "building ($buildPreset)..." -ForegroundColor Cyan
    & $CMake --build --preset $buildPreset --target MegaExplorer
    if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
}
if (-not (Test-Path $exePath)) { throw "$exePath not found -- build first" }

# --------------------------------------------------------------------- package
$outDir = Join-Path $buildDir 'package'
Write-Host "packaging ($Config)..." -ForegroundColor Cyan
& $CPack --config (Join-Path $buildDir 'CPackConfig.cmake') -C $Config -B $outDir
if ($LASTEXITCODE -ne 0) { throw "cpack failed ($LASTEXITCODE)" }

$zip = Get-ChildItem -LiteralPath $outDir -Filter '*.zip' |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $zip) { throw "no zip produced in $outDir" }

# ----------------------------------------------------------------- verify
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($zip.FullName)
try {
    $entries = $archive.Entries | ForEach-Object { $_.FullName }
} finally {
    $archive.Dispose()
}

# One entry per thing that can go missing on its own: Qt's libraries and the
# platform plugin (windeployqt), the QML modules (its import scan), FFmpeg (the
# vcpkg glob), the MSVC runtime (InstallRequiredSystemLibraries), and the two
# text files a binary distribution is obliged to carry.
$required = @(
    '/MegaExplorer.exe',
    '/qt.conf',
    '/Qt6Core.dll',
    '/Qt6Quick.dll',
    '/platforms/qwindows.dll',
    '/qml/QtQuick/qmldir',
    '/avcodec-61.dll',
    '/VCRUNTIME140.dll',
    '/MSVCP140.dll',
    '/LICENSE',
    '/THIRD-PARTY-NOTICES.txt'
)
# OrdinalIgnoreCase: the redist ships its DLLs lower-cased, Qt ships its own
# capitalised, and neither is worth mirroring in the list above.
$missing = $required | Where-Object {
    $suffix = $_
    -not ($entries | Where-Object { $_.EndsWith($suffix, [StringComparison]::OrdinalIgnoreCase) })
}
if ($missing) {
    throw "zip is missing: $($missing -join ', ')"
}

$sizeMb = [math]::Round($zip.Length / 1MB, 1)
Write-Host ("{0} ({1} MB, {2} entries)" -f $zip.FullName, $sizeMb, $entries.Count) -ForegroundColor Green
