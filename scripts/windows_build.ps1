<#
.SYNOPSIS
    Build ffmpeg-converter Windows CLI using MSBuild + CMake.

.DESCRIPTION
    Locates Visual Studio / MSBuild via vswhere.exe, optionally configures
    the CMake build directory, then builds the specified target.

.PARAMETER Config
    Build configuration: Release (default) or Debug.

.PARAMETER Clean
    Delete the entire build directory and reconfigure before building.
    Implies a full reconfiguration + rebuild.

.PARAMETER Rebuild
    Force a full recompile of all sources (MSBuild /t:Rebuild).
    Does not reconfigure CMake.

.PARAMETER NoConfigure
    Skip the CMake configure step even if the build directory is missing.
    Useful for manual workflows where CMake was run separately.

.PARAMETER Target
    MSBuild project to build. Default: windows_cli (ffmpeg_converter.exe).
    Other useful values: ALL_BUILD, INSTALL.

.EXAMPLE
    # Normal incremental build
    .\scripts\windows_build.ps1

.EXAMPLE
    # Clean rebuild in Debug
    .\scripts\windows_build.ps1 -Clean -Config Debug

.EXAMPLE
    # Force recompile without reconfiguring
    .\scripts\windows_build.ps1 -Rebuild

.EXAMPLE
    # Build everything
    .\scripts\windows_build.ps1 -Target ALL_BUILD
#>

[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string] $Config       = 'Release',

    [switch] $Clean,
    [switch] $Rebuild,
    [switch] $NoConfigure,

    [string] $Target       = 'windows_cli'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# -----------------------------------------------------------------------
#  Resolve repository root (one level above this script)
# -----------------------------------------------------------------------
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path -Parent $ScriptDir
$BuildDir  = Join-Path $RepoRoot 'build-msvc'

# -----------------------------------------------------------------------
#  Locate vswhere → MSBuild
# -----------------------------------------------------------------------
$VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $VsWhere)) {
    $VsWhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
}
if (-not (Test-Path $VsWhere)) {
    Write-Error "vswhere.exe not found. Please install Visual Studio 2019 or later."
    exit 1
}

$VsInstallPath = & $VsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $VsInstallPath) {
    Write-Error "No Visual Studio installation with MSBuild found."
    exit 1
}

$MSBuild = Join-Path $VsInstallPath 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path $MSBuild)) {
    Write-Error "MSBuild.exe not found at: $MSBuild"
    exit 1
}

Write-Host "MSBuild : $MSBuild" -ForegroundColor Cyan

# -----------------------------------------------------------------------
#  Locate cmake
# -----------------------------------------------------------------------
$CMake = (Get-Command cmake -ErrorAction SilentlyContinue)?.Source
if (-not $CMake) {
    # Try the CMake bundled with VS
    $CMake = Join-Path $VsInstallPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (-not (Test-Path $CMake)) {
        Write-Error "cmake not found on PATH and not bundled with Visual Studio."
        exit 1
    }
}
Write-Host "CMake   : $CMake" -ForegroundColor Cyan

# -----------------------------------------------------------------------
#  -Clean: wipe build directory
# -----------------------------------------------------------------------
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "`nCleaning build directory: $BuildDir" -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

# -----------------------------------------------------------------------
#  CMake configure
# -----------------------------------------------------------------------
$NeedsConfigure = (-not (Test-Path (Join-Path $BuildDir 'CMakeCache.txt')))

if ($NeedsConfigure -and $NoConfigure) {
    Write-Error "Build directory '$BuildDir' is not configured and -NoConfigure was set. Run without -NoConfigure first."
    exit 1
}

if ($NeedsConfigure) {
    Write-Host "`nConfiguring CMake..." -ForegroundColor Cyan
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    Push-Location $BuildDir
    try {
        & $CMake .. -G "Visual Studio 17 2022" -A x64
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)" }
    } finally {
        Pop-Location
    }
} else {
    Write-Host "`nBuild directory already configured. Skipping CMake." -ForegroundColor DarkGray
    Write-Host "  Use -Clean to force reconfiguration." -ForegroundColor DarkGray
}

# -----------------------------------------------------------------------
#  MSBuild
# -----------------------------------------------------------------------
$ProjectFile = Join-Path $BuildDir "src\cli\${Target}.vcxproj"
if (-not (Test-Path $ProjectFile)) {
    # Fall back to solution for targets like ALL_BUILD
    $ProjectFile = Join-Path $BuildDir 'ffmpeg_converter.sln'
    Write-Host "Project file not found; building via solution: $ProjectFile" -ForegroundColor Yellow
}

$MSBuildTarget = if ($Rebuild) { 'Rebuild' } else { 'Build' }

Write-Host "`nBuilding target '$Target' ($Config / $MSBuildTarget)..." -ForegroundColor Cyan
Write-Host "  Project : $ProjectFile"
Write-Host ""

$MSBuildArgs = @(
    $ProjectFile,
    "/t:$MSBuildTarget",
    "/p:Configuration=$Config",
    "/p:Platform=x64",
    "/v:minimal",
    "/nologo"
)

& $MSBuild @MSBuildArgs
$ExitCode = $LASTEXITCODE

Write-Host ""
if ($ExitCode -eq 0) {
    $OutExe = Join-Path $BuildDir "src\cli\$Config\ffmpeg_converter.exe"
    Write-Host "Build succeeded." -ForegroundColor Green
    if (Test-Path $OutExe) {
        Write-Host "Output : $OutExe" -ForegroundColor Green
    }
} else {
    Write-Host "Build FAILED (exit $ExitCode)." -ForegroundColor Red
}

exit $ExitCode
