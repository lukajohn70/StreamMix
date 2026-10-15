# =============================================================================
# scripts/package_windows.ps1  — StreamMix Windows .msi packager
# =============================================================================
# Requirements:
#   • Qt6 installed (set Qt6_DIR or use Qt Online Installer)
#   • CMake 3.27+
#   • Visual Studio 2022 (MSVC) or MinGW
#   • WiX Toolset v4+ in PATH  (OR  windeployqt + Inno Setup as fallback)
# Usage:
#   .\scripts\package_windows.ps1 [-BuildType Release] [-Arch x64]
# =============================================================================
param(
    [string]$BuildType = "Release",
    [string]$Arch = "x64"
)
$ErrorActionPreference = "Stop"

$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir    = Split-Path -Parent $ScriptDir
$BuildDir   = Join-Path $RootDir "build-windows-$Arch"
$DistDir    = Join-Path $RootDir "dist\windows"

$AppName    = "StreamMix"
$VersionFile = Join-Path $RootDir "libstreammix\include\streammix\version.h"
$Version    = "1.0.0"
if (Test-Path $VersionFile) {
    $match = Select-String -Path $VersionFile -Pattern 'STREAMMIX_VERSION\s+"([^"]+)"'
    if ($match) { $Version = $match.Matches[0].Groups[1].Value }
}

Write-Host "══════════════════════════════════════════" -ForegroundColor Cyan
Write-Host "  StreamMix Windows Packager v$Version"   -ForegroundColor Cyan
Write-Host "  Build: $BuildType | Arch: $Arch"         -ForegroundColor Cyan
Write-Host "══════════════════════════════════════════" -ForegroundColor Cyan

# ── 1. CMake configure + build ───────────────────────────────────────────────
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$Generator = if ($Arch -eq "x64") { "Visual Studio 17 2022" } else { "MinGW Makefiles" }
$ArchArg   = if ($Arch -eq "x64") { @("-A", "x64") } else { @() }

cmake -S $RootDir -B $BuildDir `
    -G $Generator @ArchArg `
    -DCMAKE_BUILD_TYPE=$BuildType `
    -DCPACK_GENERATOR="WIX"

cmake --build $BuildDir --config $BuildType --parallel

# ── 2. windeployqt — bundle Qt DLLs ─────────────────────────────────────────
$ExePath = Get-ChildItem -Path $BuildDir -Filter "StreamMix.exe" -Recurse | Select-Object -First 1 -ExpandProperty FullName
if (-not $ExePath) {
    Write-Error "❌  StreamMix.exe not found in $BuildDir"
    exit 1
}

Write-Host "🔧  Running windeployqt..." -ForegroundColor Yellow
windeployqt --release --no-translations --no-system-d3d-compiler `
    --no-opengl-sw --compiler-runtime "$ExePath"

# ── 3. WiX MSI packaging via CPack ──────────────────────────────────────────
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

Write-Host "📦  Building MSI with CPack + WiX..." -ForegroundColor Yellow
Push-Location $BuildDir
    cpack -C $BuildType -G WIX --verbose
Pop-Location

$MsiFiles = Get-ChildItem -Path $BuildDir -Filter "*.msi" -Recurse
foreach ($msi in $MsiFiles) {
    $Dest = Join-Path $DistDir "${AppName}-${Version}-${Arch}.msi"
    Copy-Item -Path $msi.FullName -Destination $Dest -Force
    Write-Host ""
    Write-Host "✅  Package ready:" -ForegroundColor Green
    Write-Host "    $Dest"          -ForegroundColor Green
}

if (-not $MsiFiles) {
    # Fallback: ZIP deployment bundle
    Write-Host "⚠️   No MSI produced — creating ZIP bundle instead" -ForegroundColor Yellow
    $ZipOut = Join-Path $DistDir "${AppName}-${Version}-${Arch}.zip"
    $BinDir = Split-Path -Parent $ExePath
    Compress-Archive -Path "$BinDir\*" -DestinationPath $ZipOut -Force
    Write-Host "✅  ZIP bundle: $ZipOut" -ForegroundColor Green
}
