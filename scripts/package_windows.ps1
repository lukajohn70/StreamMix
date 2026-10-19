# =============================================================================
# scripts/package_windows.ps1  — StreamMix Windows .msi / .zip packager
# =============================================================================
# Requirements:
#   • Qt6 installed (set Qt6_DIR or use Qt Online Installer)
#   • CMake 3.27+
#   • Visual Studio 2022 (MSVC) or MinGW
#   • WiX Toolset v3/v4 in PATH (for MSI installer)
# Usage:
#   .\scripts\package_windows.ps1 [-BuildType Release] [-Arch x64] [-BuildDir <dir>]
# =============================================================================
param(
    [string]$BuildType = "Release",
    [string]$Arch = "x64",
    [string]$BuildDir = ""
)
$ErrorActionPreference = "Stop"

$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir    = Split-Path -Parent $ScriptDir

if (-not $BuildDir) {
    if (Test-Path (Join-Path $RootDir "build")) {
        $BuildDir = Join-Path $RootDir "build"
    } else {
        $BuildDir = Join-Path $RootDir "build-windows-$Arch"
    }
}
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
Write-Host "  BuildDir: $BuildDir"                     -ForegroundColor Cyan
Write-Host "══════════════════════════════════════════" -ForegroundColor Cyan

# ── 1. CMake configure + build (if StreamMix.exe not already built) ─────────
$ExePath = Get-ChildItem -Path $BuildDir -Filter "StreamMix.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName

if (-not $ExePath) {
    Write-Host "⚙️  Configuring and building StreamMix ($BuildType)..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

    $Generator = if ($Arch -eq "x64") { "Visual Studio 17 2022" } else { "MinGW Makefiles" }
    $ArchArg   = if ($Arch -eq "x64") { @("-A", "x64") } else { @() }
    $PrefixArg = @()
    if ($env:Qt6_DIR -or $env:FFMPEG_DIR) {
        $PrefixArg = @("-DCMAKE_PREFIX_PATH=$($env:Qt6_DIR);$($env:FFMPEG_DIR)")
    }

    cmake -S $RootDir -B $BuildDir `
        -G $Generator @ArchArg `
        -DCMAKE_BUILD_TYPE=$BuildType `
        @PrefixArg `
        -DCPACK_GENERATOR="WIX"

    cmake --build $BuildDir --config $BuildType --parallel

    $ExePath = Get-ChildItem -Path $BuildDir -Filter "StreamMix.exe" -Recurse | Select-Object -First 1 -ExpandProperty FullName
}

if (-not $ExePath) {
    Write-Error "❌  StreamMix.exe not found in $BuildDir"
    exit 1
}

$BinDir = Split-Path -Parent $ExePath
Write-Host "🎯  Found binary directory: $BinDir" -ForegroundColor Green

# Ensure streammix.dll is copied alongside StreamMix.exe
$DllCandidate = Get-ChildItem -Path $BuildDir -Filter "streammix.dll" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
if ($DllCandidate -and -not (Test-Path (Join-Path $BinDir "streammix.dll"))) {
    Write-Host "📦  Copying streammix.dll to $BinDir..." -ForegroundColor Yellow
    Copy-Item -Path $DllCandidate -Destination $BinDir -Force
}

# ── 2. windeployqt — bundle Qt DLLs & plugins ───────────────────────────────
Write-Host "🔧  Running windeployqt..." -ForegroundColor Yellow
$WinDeploy = "windeployqt"
if (-not (Get-Command windeployqt -ErrorAction SilentlyContinue)) {
    if (Test-Path "C:\Qt\6.6.3\msvc2019_64\bin\windeployqt.exe") {
        $WinDeploy = "C:\Qt\6.6.3\msvc2019_64\bin\windeployqt.exe"
    } elseif ($env:Qt6_DIR) {
        $candidate = Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $env:Qt6_DIR))) "bin\windeployqt.exe"
        if (Test-Path $candidate) { $WinDeploy = $candidate }
    }
}
& $WinDeploy --release --no-translations --no-system-d3d-compiler `
    --no-opengl-sw --compiler-runtime "$ExePath"

# Bundle FFmpeg DLLs if present
if ($env:FFMPEG_DIR -and (Test-Path "$env:FFMPEG_DIR\bin")) {
    Write-Host "📦  Bundling FFmpeg DLLs from $env:FFMPEG_DIR\bin..." -ForegroundColor Yellow
    Copy-Item -Path "$env:FFMPEG_DIR\bin\*.dll" -Destination $BinDir -Force
} elseif (Test-Path "C:\ffmpeg\bin") {
    Write-Host "📦  Bundling FFmpeg DLLs from C:\ffmpeg\bin..." -ForegroundColor Yellow
    Copy-Item -Path "C:\ffmpeg\bin\*.dll" -Destination $BinDir -Force
}

# ── 3. Packaging (Portable ZIP + MSI) ─────────────────────────────────────────
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

$ZipOut = Join-Path $DistDir "${AppName}-${Version}-${Arch}.zip"
Write-Host "📦  Creating Portable ZIP bundle..." -ForegroundColor Yellow
Compress-Archive -Path "$BinDir\*" -DestinationPath $ZipOut -Force
Write-Host "✅  ZIP bundle ready: $ZipOut" -ForegroundColor Green

Write-Host "📦  Attempting MSI build with CPack..." -ForegroundColor Yellow
try {
    Push-Location $BuildDir
    cpack -C $BuildType -G WIX
    Pop-Location
    $MsiFiles = Get-ChildItem -Path $BuildDir -Filter "*.msi" -Recurse
    foreach ($msi in $MsiFiles) {
        $Dest = Join-Path $DistDir "${AppName}-${Version}-${Arch}.msi"
        Copy-Item -Path $msi.FullName -Destination $Dest -Force
        Write-Host "✅  MSI ready: $Dest" -ForegroundColor Green
    }
} catch {
    Write-Host "⚠️  WiX MSI generation skipped: $($_.Exception.Message)" -ForegroundColor Yellow
}
