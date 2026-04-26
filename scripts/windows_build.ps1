<#
.SYNOPSIS
    Build ffmpeg-converter Windows CLI using CMake + MSBuild and optionally FPC.

.DESCRIPTION
    Locates Visual Studio / MSBuild via vswhere.exe, optionally runs CMake
    configuration, then builds the specified target with MSBuild.

    Phase 4A: Also supports building the Pascal Windows CLI using FPC.
    Phase 4B: Also supports building the Pascal Windows GUI using Lazarus (lazbuild).

    Compatible with PowerShell 5.1 and later.

.PARAMETER Config
    Build configuration.  Release (default) or Debug.

.PARAMETER Clean
    Delete the build directory and reconfigure from scratch before building.

.PARAMETER Rebuild
    Force a full recompile of all sources (MSBuild /t:Rebuild).
    Does NOT reconfigure CMake — combine with -Clean if you need that too.

.PARAMETER NoConfigure
    Skip the CMake configure step even when the build directory is absent.
    Useful when you have run CMake manually and just want to recompile.

.PARAMETER Target
    MSBuild sub-project to build.
    Default : windows_cli  -> produces build-msvc\src\cli\Release\ffmpeg_converter.exe
    Other   : ALL_BUILD    -> builds everything
              INSTALL      -> installs to CMAKE_INSTALL_PREFIX

.PARAMETER BuildFPC
    Also build the Pascal Windows CLI (ffmpeg_converter_windows.exe) using FPC.
    Requires fpc.exe to be available on PATH.

.PARAMETER FPCOnly
    Build only the Pascal Windows CLI (ffmpeg_converter_windows.exe) using FPC.
    Skips CMake/MSBuild entirely.

.PARAMETER BuildGUI
    Also build the Pascal Windows GUI (ffmpeg_converter_gui.exe) using Lazarus.
    Requires lazbuild.exe to be available on PATH.

.PARAMETER GUIOnly
    Build only the Pascal Windows GUI (ffmpeg_converter_gui.exe) using Lazarus.
    Skips CMake/MSBuild entirely.

.PARAMETER Help
    Show this help message and exit.

.EXAMPLE
    # Normal incremental build (most common)
    .\scripts\windows_build.ps1

.EXAMPLE
    # Show help
    .\scripts\windows_build.ps1 -Help

.EXAMPLE
    # Clean build in Release (wipes build-msvc and reconfigures)
    .\scripts\windows_build.ps1 -Clean

.EXAMPLE
    # Clean Debug build
    .\scripts\windows_build.ps1 -Clean -Config Debug

.EXAMPLE
    # Force full recompile without touching CMake configuration
    .\scripts\windows_build.ps1 -Rebuild

.EXAMPLE
    # Build everything (all targets)
    .\scripts\windows_build.ps1 -Target ALL_BUILD

.EXAMPLE
    # Build C CLI + Pascal Windows CLI
    .\scripts\windows_build.ps1 -BuildFPC

.EXAMPLE
    # Build Pascal Windows CLI only (Phase 4A)
    .\scripts\windows_build.ps1 -FPCOnly

.EXAMPLE
    # Build Pascal Windows GUI only (Phase 4B)
    .\scripts\windows_build.ps1 -GUIOnly

.EXAMPLE
    # Build C CLI + Pascal Windows CLI + GUI
    .\scripts\windows_build.ps1 -BuildFPC -BuildGUI
#>

param(
    [ValidateSet('Release','Debug')]
    [string] $Config      = 'Release',

    [switch] $Clean,
    [switch] $Rebuild,
    [switch] $NoConfigure,
    [switch] $Help,
    [switch] $BuildFPC,
    [switch] $FPCOnly,
    [switch] $BuildGUI,
    [switch] $GUIOnly,

    [string] $Target      = 'windows_cli'
)

$ErrorActionPreference = 'Stop'

# -----------------------------------------------------------------------
#  -Help
# -----------------------------------------------------------------------
if ($Help) {
    Get-Help $MyInvocation.MyCommand.Definition
    exit 0
}

# -----------------------------------------------------------------------
#  Banner
# -----------------------------------------------------------------------
Write-Host ""
Write-Host "=== ffmpeg-converter Windows Build ===" -ForegroundColor White
Write-Host "  Config  : $Config"
Write-Host "  Target  : $Target"
if ($FPCOnly) { Write-Host "  Mode    : FPC only (Pascal Windows CLI)" -ForegroundColor Cyan }
if ($GUIOnly) { Write-Host "  Mode    : GUI only (Pascal Windows GUI)" -ForegroundColor Cyan }
if ($BuildFPC) { Write-Host "  BuildFPC: yes (will also build Pascal Windows CLI)" -ForegroundColor Cyan }
if ($BuildGUI) { Write-Host "  BuildGUI: yes (will also build Pascal Windows GUI via Lazarus)" -ForegroundColor Cyan }
if ($Clean)       { Write-Host "  Clean   : yes (build directory will be wiped)" -ForegroundColor Yellow }
if ($Rebuild)     { Write-Host "  Rebuild : yes (full recompile forced)" -ForegroundColor Yellow }
if ($NoConfigure) { Write-Host "  NoConfigure : yes (CMake step skipped)" }
Write-Host ""

# -----------------------------------------------------------------------
#  Paths
# -----------------------------------------------------------------------
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path -Parent $ScriptDir
$BuildDir  = Join-Path $RepoRoot 'build-msvc'

# -----------------------------------------------------------------------
#  Phase 4A: FPC build helper
# -----------------------------------------------------------------------
function Invoke-FPCBuild {
    Write-Host ""
    Write-Host "=== Phase 4A: Building Pascal Windows CLI ===" -ForegroundColor Cyan

    $FPC = Get-Command fpc -ErrorAction SilentlyContinue
    if (-not $FPC) {
        Write-Host "ERROR: fpc.exe not found on PATH." -ForegroundColor Red
        Write-Host "       Install Free Pascal from https://www.freepascal.org/" -ForegroundColor Red
        return $false
    }

    Write-Host "FPC     : $($FPC.Source)" -ForegroundColor Cyan

    $FPCOut = Join-Path $BuildDir 'fpc'
    if (-not (Test-Path $FPCOut)) {
        New-Item -ItemType Directory -Force -Path $FPCOut | Out-Null
    }

    $FPCArgs = @(
        "-Fu$RepoRoot\fpc\converter",
        "-Fu$RepoRoot\fpc\common",
        "-Fu$RepoRoot\fpc\json",
        "-Fu$RepoRoot\fpc\platform",
        "-Fu$RepoRoot\fpc\cli",
        "-FU$FPCOut",
        "-Ww",
        "-Werror",
        "$RepoRoot\fpc\cli\ffmpeg_converter_windows.lpr",
        "-o$FPCOut\ffmpeg_converter_windows.exe"
    )

    Write-Host "Compiling ffmpeg_converter_windows.lpr ..." -ForegroundColor Cyan
    & fpc @FPCArgs
    $FPCExit = $LASTEXITCODE
    Write-Host ""

    if ($FPCExit -eq 0) {
        Write-Host "FPC build succeeded." -ForegroundColor Green
        $OutExe = Join-Path $FPCOut 'ffmpeg_converter_windows.exe'
        if (Test-Path $OutExe) {
            Write-Host "Output  : $OutExe" -ForegroundColor Green
            Copy-Item $OutExe $RepoRoot -ErrorAction SilentlyContinue | Out-Null
        }
    } else {
        Write-Host "FPC build FAILED (exit $FPCExit)." -ForegroundColor Red
    }

    return ($FPCExit -eq 0)
}

# -----------------------------------------------------------------------
#  Phase 4B: Lazarus GUI build helper
# -----------------------------------------------------------------------
function Invoke-GUIBuild {
    Write-Host ""
    Write-Host "=== Phase 4B: Building Pascal Windows GUI ===" -ForegroundColor Cyan

    $LazBuild = Get-Command lazbuild -ErrorAction SilentlyContinue
    if (-not $LazBuild) {
        Write-Host "ERROR: lazbuild.exe not found on PATH." -ForegroundColor Red
        Write-Host "       Install Lazarus IDE from https://www.lazarus-ide.org/" -ForegroundColor Red
        return $false
    }

    Write-Host "lazbuild: $($LazBuild.Source)" -ForegroundColor Cyan

    $GUIDir  = Join-Path $RepoRoot 'fpc\gui'
    $LPIFile = Join-Path $GUIDir 'form.lpi'

    if (-not (Test-Path $LPIFile)) {
        Write-Host "ERROR: form.lpi not found at: $LPIFile" -ForegroundColor Red
        return $false
    }

    Write-Host "Compiling form.lpi (Lazarus GUI) ..." -ForegroundColor Cyan
    Push-Location $GUIDir
    try {
        & lazbuild --build-mode=default $LPIFile
        $LazExit = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    Write-Host ""

    if ($LazExit -eq 0) {
        Write-Host "Lazarus GUI build succeeded." -ForegroundColor Green
        $OutExe = Join-Path $GUIDir 'ffmpeg_converter_gui.exe'
        if (Test-Path $OutExe) {
            Write-Host "Output  : $OutExe" -ForegroundColor Green
            Copy-Item $OutExe $RepoRoot -ErrorAction SilentlyContinue | Out-Null
        }
    } else {
        Write-Host "Lazarus GUI build FAILED (exit $LazExit)." -ForegroundColor Red
    }

    return ($LazExit -eq 0)
}

# -----------------------------------------------------------------------
#  GUI-only mode: skip CMake/MSBuild
# -----------------------------------------------------------------------
if ($GUIOnly) {
    $ok = Invoke-GUIBuild
    exit $(if ($ok) { 0 } else { 1 })
}

# -----------------------------------------------------------------------
#  FPC-only mode: skip CMake/MSBuild
# -----------------------------------------------------------------------
if ($FPCOnly) {
    $ok = Invoke-FPCBuild
    exit $(if ($ok) { 0 } else { 1 })
}

# -----------------------------------------------------------------------
#  Locate vswhere.exe
# -----------------------------------------------------------------------
$VsWhereCandidates = @(
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
    "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
)
$VsWhere = $null
foreach ($c in $VsWhereCandidates) {
    if (Test-Path $c) { $VsWhere = $c; break }
}
if (-not $VsWhere) {
    Write-Host "ERROR: vswhere.exe not found. Install Visual Studio 2019 or later." -ForegroundColor Red
    exit 1
}

# -----------------------------------------------------------------------
#  Locate MSBuild via vswhere
# -----------------------------------------------------------------------
$VsInstallPath = (& $VsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null)
if (-not $VsInstallPath) {
    Write-Host "ERROR: No Visual Studio with MSBuild found." -ForegroundColor Red
    exit 1
}
$VsInstallPath = $VsInstallPath.Trim()

$MSBuild = Join-Path $VsInstallPath 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path $MSBuild)) {
    Write-Host "ERROR: MSBuild.exe not found at: $MSBuild" -ForegroundColor Red
    exit 1
}
Write-Host "MSBuild : $MSBuild" -ForegroundColor Cyan

# -----------------------------------------------------------------------
#  Determine CMake generator from VS version
#  vswhere catalog_productMajorVersion -> 17 = VS2022, 18 = VS2025 ...
#  vswhere catalog_productLineVersion  -> "2022", "2025" ...
# -----------------------------------------------------------------------
$Generator = $null

$VsMajorRaw = (& $VsWhere -latest -products * -property catalog_productMajorVersion 2>$null)
$VsYearRaw  = (& $VsWhere -latest -products * -property catalog_productLineVersion  2>$null)

if ($VsMajorRaw -and $VsYearRaw) {
    $Generator = "Visual Studio $($VsMajorRaw.Trim()) $($VsYearRaw.Trim())"
}

if (-not $Generator) {
    # Fallback: read generator from existing CMakeCache.txt
    $CacheFile = Join-Path $BuildDir 'CMakeCache.txt'
    if (Test-Path $CacheFile) {
        $GenLine = Select-String -Path $CacheFile -Pattern '^CMAKE_GENERATOR:INTERNAL=' | Select-Object -First 1
        if ($GenLine) {
            $Generator = $GenLine.Line.Split('=',2)[1].Trim()
        }
    }
}

if (-not $Generator) {
    $Generator = 'Visual Studio 17 2022'   # safe default
}

Write-Host "Generator : $Generator" -ForegroundColor Cyan

# -----------------------------------------------------------------------
#  Locate cmake.exe
# -----------------------------------------------------------------------
$CMakeCmd = Get-Command cmake -ErrorAction SilentlyContinue
if ($CMakeCmd) {
    $CMake = $CMakeCmd.Source
} else {
    # Try cmake bundled with VS
    $CMake = Join-Path $VsInstallPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (-not (Test-Path $CMake)) {
        Write-Host "ERROR: cmake not found on PATH and not bundled with VS." -ForegroundColor Red
        Write-Host "       Install cmake from https://cmake.org or add it to PATH." -ForegroundColor Red
        exit 1
    }
}
Write-Host "CMake   : $CMake" -ForegroundColor Cyan
Write-Host ""

# -----------------------------------------------------------------------
#  -Clean: remove build directory
# -----------------------------------------------------------------------
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Cleaning: $BuildDir" -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
    Write-Host ""
}

# -----------------------------------------------------------------------
#  CMake configure (only when needed)
# -----------------------------------------------------------------------
$NeedsConfigure = -not (Test-Path (Join-Path $BuildDir 'CMakeCache.txt'))

if ($NeedsConfigure -and $NoConfigure) {
    Write-Host "ERROR: Build directory not configured and -NoConfigure was given." -ForegroundColor Red
    Write-Host "       Run without -NoConfigure to let CMake configure it first." -ForegroundColor Red
    exit 1
}

if ($NeedsConfigure) {
    Write-Host "Configuring CMake with generator: $Generator" -ForegroundColor Cyan
    if (-not (Test-Path $BuildDir)) {
        New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    }
    Push-Location $BuildDir
    try {
        & $CMake .. -G $Generator -A x64
        if ($LASTEXITCODE -ne 0) {
            Write-Host "ERROR: CMake configure failed (exit $LASTEXITCODE)." -ForegroundColor Red
            exit $LASTEXITCODE
        }
    } finally {
        Pop-Location
    }
    Write-Host ""
} else {
    Write-Host "Build directory already configured -- skipping CMake." -ForegroundColor DarkGray
    Write-Host "  Use -Clean to force full reconfiguration." -ForegroundColor DarkGray
    Write-Host ""
}

# -----------------------------------------------------------------------
#  Resolve project / solution file
# -----------------------------------------------------------------------
$ProjectFile = Join-Path $BuildDir "src\cli\${Target}.vcxproj"
if (-not (Test-Path $ProjectFile)) {
    # Targets like ALL_BUILD live in the solution
    $ProjectFile = Join-Path $BuildDir 'ffmpeg_converter.sln'
    Write-Host "  .vcxproj not found for '$Target'; building via solution." -ForegroundColor DarkGray
}

if (-not (Test-Path $ProjectFile)) {
    Write-Host "ERROR: Neither $Target.vcxproj nor ffmpeg_converter.sln found in $BuildDir" -ForegroundColor Red
    exit 1
}

# -----------------------------------------------------------------------
#  MSBuild
# -----------------------------------------------------------------------
$MSBuildTarget = if ($Rebuild) { 'Rebuild' } else { 'Build' }

Write-Host "Building: $Target  [$Config / $MSBuildTarget]" -ForegroundColor Cyan
Write-Host "  $ProjectFile"
Write-Host ""

& $MSBuild $ProjectFile `
    /t:$MSBuildTarget `
    /p:Configuration=$Config `
    /p:Platform=x64 `
    /v:minimal `
    /nologo

$ExitCode = $LASTEXITCODE
Write-Host ""

if ($ExitCode -eq 0) {
    Write-Host "Build succeeded." -ForegroundColor Green
    $OutExe = Join-Path $BuildDir "src\cli\$Config\ffmpeg_converter.exe"
    if (Test-Path $OutExe) {
        Write-Host "Output  : $OutExe" -ForegroundColor Green
    }
} else {
    Write-Host "Build FAILED (exit $ExitCode)." -ForegroundColor Red
}

# -----------------------------------------------------------------------
#  Phase 4A: Optionally build Pascal Windows CLI with FPC
# -----------------------------------------------------------------------
if ($BuildFPC -and ($ExitCode -eq 0)) {
    $fpcOk = Invoke-FPCBuild
    if (-not $fpcOk) {
        $ExitCode = 1
    }
}

# -----------------------------------------------------------------------
#  Phase 4B: Optionally build Pascal Windows GUI with Lazarus
# -----------------------------------------------------------------------
if ($BuildGUI -and ($ExitCode -eq 0)) {
    $guiOk = Invoke-GUIBuild
    if (-not $guiOk) {
        $ExitCode = 1
    }
}

exit $ExitCode

