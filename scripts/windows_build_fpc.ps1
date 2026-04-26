<#
.SYNOPSIS
    Build the Pascal (FPC/Lazarus) CLI and GUI for Windows, then stage all
    outputs and bundled DLLs into fpc\bin\.

.DESCRIPTION
    Automatically discovers fpc.exe and lazbuild.exe from the Lazarus
    installation directory (or PATH), builds the Pascal CLI and GUI, then
    copies the resulting executables together with the bundled runtime DLLs
    from src\platform\windows\bin\ into fpc\bin\.

    The staged directory is self-contained: the Pascal executables resolve
    ffmpeg, ffprobe, and mkvmerge by looking first in the directory they
    run from (fpc\bin\), then fall back to environment variables, and
    finally to the system PATH.

.PARAMETER CLIOnly
    Build only the Pascal CLI (ffmpeg_converter.exe).  Skip GUI.

.PARAMETER GUIOnly
    Build only the Pascal GUI (ffmpeg_converter_gui.exe).  Skip CLI.

.PARAMETER Clean
    Remove intermediate unit cache and Lazarus lib directories before
    building to force a full recompile.

.PARAMETER Help
    Show this help message and exit.

.EXAMPLE
    # Normal build: CLI + GUI, then stage to fpc\bin
    .\scripts\windows_build_fpc.ps1

.EXAMPLE
    # Build CLI only
    .\scripts\windows_build_fpc.ps1 -CLIOnly

.EXAMPLE
    # Build GUI only
    .\scripts\windows_build_fpc.ps1 -GUIOnly

.EXAMPLE
    # Force full recompile of both
    .\scripts\windows_build_fpc.ps1 -Clean

.EXAMPLE
    # Clean + GUI only
    .\scripts\windows_build_fpc.ps1 -Clean -GUIOnly
#>

param(
    [switch] $CLIOnly,
    [switch] $GUIOnly,
    [switch] $Clean,
    [switch] $Help
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
#  -Help
# ---------------------------------------------------------------------------
if ($Help) {
    Get-Help $MyInvocation.MyCommand.Definition
    exit 0
}

# ---------------------------------------------------------------------------
#  Paths
# ---------------------------------------------------------------------------
$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot   = Split-Path -Parent $ScriptDir
$FpcDir     = Join-Path $RepoRoot 'fpc'
$BinDir     = Join-Path $FpcDir   'bin'
$UnitsBase  = Join-Path $FpcDir   'build\.units'
$BundleDir  = Join-Path $RepoRoot 'src\platform\windows\bin'

# ---------------------------------------------------------------------------
#  Banner
# ---------------------------------------------------------------------------
Write-Host ''
Write-Host '=== ffmpeg-converter Pascal Build ===' -ForegroundColor White
if ($CLIOnly)      { Write-Host '  Mode  : CLI only'  -ForegroundColor Cyan }
elseif ($GUIOnly)  { Write-Host '  Mode  : GUI only'  -ForegroundColor Cyan }
else               { Write-Host '  Mode  : CLI + GUI' -ForegroundColor Cyan }
if ($Clean)        { Write-Host '  Clean : yes (intermediate files will be removed)' -ForegroundColor Yellow }
Write-Host ''

# ---------------------------------------------------------------------------
#  Auto-discover lazbuild.exe
#  Priority: PATH -> well-known install locations -> versioned C:\lazarus* glob
# ---------------------------------------------------------------------------
function Find-LazBuild {
    $cmd = Get-Command lazbuild.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $roots = @(
        'C:\lazarus',
        'C:\Lazarus',
        (Join-Path $env:ProgramFiles          'Lazarus'),
        (Join-Path ${env:ProgramFiles(x86)}   'Lazarus'),
        (Join-Path $env:LocalAppData          'Lazarus')
    )
    foreach ($r in $roots) {
        $p = Join-Path $r 'lazbuild.exe'
        if (Test-Path $p) { return $p }
    }

    # Versioned installs: C:\lazarus3.6\ etc.
    $hits = Get-Item 'C:\lazarus*\lazbuild.exe' -ErrorAction SilentlyContinue
    if ($hits) { return ($hits | Sort-Object FullName -Descending)[0].FullName }

    return $null
}

# ---------------------------------------------------------------------------
#  Auto-discover fpc.exe
#  Derives the path from the lazbuild location so both stay in sync with
#  the same Lazarus installation.
#  Priority: PATH -> <lazarusdir>\fpc\*\bin\x86_64-win64\fpc.exe
# ---------------------------------------------------------------------------
function Find-FPC([string] $LazDir) {
    $cmd = Get-Command fpc.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    if ($LazDir -and (Test-Path $LazDir)) {
        $pattern = Join-Path $LazDir 'fpc\*\bin\x86_64-win64\fpc.exe'
        $hits = Get-ChildItem $pattern -ErrorAction SilentlyContinue |
                Sort-Object FullName -Descending
        if ($hits) { return $hits[0].FullName }
    }

    return $null
}

# ---------------------------------------------------------------------------
#  Tool discovery
# ---------------------------------------------------------------------------
$LazBuildExe = Find-LazBuild
$LazDir      = if ($LazBuildExe) { Split-Path -Parent $LazBuildExe } else { $null }
$FpcExe      = Find-FPC $LazDir

Write-Host 'Tool discovery:' -ForegroundColor White
if ($FpcExe)      { Write-Host "  fpc.exe      : $FpcExe"      -ForegroundColor Cyan }
else              { Write-Host '  fpc.exe      : NOT FOUND'     -ForegroundColor Red  }
if ($LazBuildExe) { Write-Host "  lazbuild.exe : $LazBuildExe" -ForegroundColor Cyan }
else              { Write-Host '  lazbuild.exe : NOT FOUND'     -ForegroundColor Red  }
Write-Host ''

if (-not $GUIOnly -and -not $FpcExe) {
    Write-Host 'ERROR: fpc.exe not found.  Install Lazarus IDE (includes FPC).' -ForegroundColor Red
    Write-Host '       https://www.lazarus-ide.org/' -ForegroundColor Red
    exit 1
}
if (-not $CLIOnly -and -not $LazBuildExe) {
    Write-Host 'ERROR: lazbuild.exe not found.  Install Lazarus IDE.' -ForegroundColor Red
    Write-Host '       https://www.lazarus-ide.org/' -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------------------
#  -Clean: remove intermediate files
# ---------------------------------------------------------------------------
if ($Clean) {
    $cleanDirs = @(
        (Join-Path $UnitsBase 'cli'),
        (Join-Path $UnitsBase 'gui'),
        (Join-Path $FpcDir    'gui\lib')
    )
    foreach ($d in $cleanDirs) {
        if (Test-Path $d) {
            Write-Host "Cleaning: $d" -ForegroundColor Yellow
            Remove-Item -Recurse -Force $d
        }
    }
    Write-Host ''
}

# ---------------------------------------------------------------------------
#  Build-CLI
# ---------------------------------------------------------------------------
function Build-CLI {
    Write-Host '=== Building Pascal CLI ===' -ForegroundColor Cyan

    $UnitsDir = Join-Path $UnitsBase 'cli'
    New-Item -ItemType Directory -Force -Path $UnitsDir | Out-Null

    $OutExe  = Join-Path $FpcDir 'cli\ffmpeg_converter.exe'
    $LprFile = Join-Path $FpcDir 'cli\ffmpeg_converter_windows.lpr'

    if (-not (Test-Path $LprFile)) {
        Write-Host "ERROR: source not found: $LprFile" -ForegroundColor Red
        return $false
    }

    $FpcArgs = @(
        '-Mobjfpc', '-Sh', '-O2',
        "-Fu$FpcDir\converter",
        "-Fu$FpcDir\common",
        "-Fu$FpcDir\json",
        "-Fu$FpcDir\cli",
        "-Fu$FpcDir\platform",
        "-FU$UnitsDir",
        "-FE$FpcDir\cli",
        "-o$OutExe",
        $LprFile
    )

    Write-Host "Compiling ffmpeg_converter_windows.lpr ..."
    & $FpcExe @FpcArgs
    $rc = $LASTEXITCODE
    Write-Host ''

    if ($rc -eq 0) {
        Write-Host "CLI build succeeded." -ForegroundColor Green
        Write-Host "  Output: $OutExe" -ForegroundColor Green
        return $true
    }
    else {
        Write-Host "CLI build FAILED (exit $rc)." -ForegroundColor Red
        return $false
    }
}

# ---------------------------------------------------------------------------
#  Build-GUI
# ---------------------------------------------------------------------------
function Build-GUI {
    Write-Host '=== Building Pascal GUI ===' -ForegroundColor Cyan

    $LpiFile = Join-Path $FpcDir 'gui\form.lpi'
    if (-not (Test-Path $LpiFile)) {
        Write-Host "ERROR: form.lpi not found: $LpiFile" -ForegroundColor Red
        return $false
    }

    Write-Host "Compiling form.lpi via lazbuild ..."
    Push-Location $RepoRoot
    try {
        & $LazBuildExe -B $LpiFile
        $rc = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    Write-Host ''

    $OutExe = Join-Path $FpcDir 'gui\ffmpeg_converter_gui.exe'
    if ($rc -eq 0 -and (Test-Path $OutExe)) {
        Write-Host "GUI build succeeded." -ForegroundColor Green
        Write-Host "  Output: $OutExe" -ForegroundColor Green
        return $true
    }
    else {
        Write-Host "GUI build FAILED (exit $rc)." -ForegroundColor Red
        return $false
    }
}

# ---------------------------------------------------------------------------
#  Stage-ToBin  — copy executables + bundled DLLs to fpc\bin\
# ---------------------------------------------------------------------------
function Stage-ToBin([string[]] $Exes) {
    Write-Host ''
    Write-Host '=== Staging to fpc\bin ===' -ForegroundColor Cyan
    New-Item -ItemType Directory -Force -Path $BinDir | Out-Null

    foreach ($exe in $Exes) {
        if (Test-Path $exe) {
            Copy-Item $exe $BinDir -Force
            Write-Host "  Copied: $(Split-Path -Leaf $exe)" -ForegroundColor DarkGray
        }
    }

    if (Test-Path $BundleDir) {
        $bundled = Get-ChildItem $BundleDir
        foreach ($item in $bundled) {
            Copy-Item $item.FullName $BinDir -Force
        }
        Write-Host "  Copied: $($bundled.Count) bundled runtime files from src\platform\windows\bin\" -ForegroundColor DarkGray
    }
    else {
        Write-Host "WARNING: bundle dir not found: $BundleDir" -ForegroundColor Yellow
    }

    Write-Host ''
    Write-Host "Output directory: $BinDir" -ForegroundColor Green
    Get-ChildItem $BinDir -Filter '*.exe' | ForEach-Object {
        $sizeMB = [Math]::Round($_.Length / 1MB, 1)
        Write-Host "  $($_.Name)  ($sizeMB MB)" -ForegroundColor Green
    }
}

# ---------------------------------------------------------------------------
#  Main
# ---------------------------------------------------------------------------
$cliOk     = $true
$guiOk     = $true
$stagedExes = @()

if (-not $GUIOnly) {
    $cliOk = Build-CLI
    Write-Host ''
    if ($cliOk) {
        $stagedExes += Join-Path $FpcDir 'cli\ffmpeg_converter.exe'
    }
}

if (-not $CLIOnly) {
    $guiOk = Build-GUI
    if ($guiOk) {
        $stagedExes += Join-Path $FpcDir 'gui\ffmpeg_converter_gui.exe'
    }
}

if ($stagedExes.Count -gt 0) {
    Stage-ToBin $stagedExes
}

$allOk = $cliOk -and $guiOk
Write-Host ''
if ($allOk) {
    Write-Host 'All builds succeeded.' -ForegroundColor Green
}
else {
    Write-Host 'One or more builds FAILED.' -ForegroundColor Red
}

exit $(if ($allOk) { 0 } else { 1 })
